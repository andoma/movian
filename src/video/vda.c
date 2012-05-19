#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFString.h>

#include <VideoDecodeAcceleration/VDADecoder.h>

#include "showtime.h"
#include "vda.h"
#include "video_decoder.h"
#include "video_settings.h"

LIST_HEAD(vda_frame_list, vda_frame);

/**
 *
 */
typedef struct vda_frame {
  LIST_ENTRY(vda_frame) vf_link;
  CVImageBufferRef vf_buf;
  int64_t vf_pts;
  int vf_duration;
  uint8_t vf_epoch;
  uint8_t vf_send_pts;
} vda_frame_t;


/**
 *
 */
typedef struct vda_decoder {
  VDADecoder vdad_decoder;
  media_codec_t *vdad_mc;
  video_decoder_t *vdad_vd;

  hts_mutex_t vdad_mutex;
  struct vda_frame_list vdad_frames;
  int64_t vdad_max_ts;
  int64_t vdad_flush_to;

  int vdad_estimated_duration;
  int64_t vdad_last_pts;

} vda_decoder_t;


/**
 *
 */
static int
vf_cmp(const vda_frame_t *a, const vda_frame_t *b)
{
  if(a->vf_pts < b->vf_pts)
    return -1;
  if(a->vf_pts > b->vf_pts)
    return 1;
  return 0;
}


/**
 *
 */
static void
emit_frame(vda_decoder_t *vdad, vda_frame_t *vf)
{
  AVFrame frame;
  int i;
  CGSize siz;

  CVPixelBufferLockBaseAddress(vf->vf_buf, 0);

  for(i = 0; i < 3; i++ ) {
    frame.data[i] = CVPixelBufferGetBaseAddressOfPlane(vf->vf_buf, i);
    frame.linesize[i] = CVPixelBufferGetBytesPerRowOfPlane(vf->vf_buf, i);
  }
  
  if(vdad->vdad_last_pts != AV_NOPTS_VALUE && vf->vf_pts != AV_NOPTS_VALUE) {
    int64_t d = vf->vf_pts - vdad->vdad_last_pts;

    if(d > 1000 && d < 1000000)
      vdad->vdad_estimated_duration = d;
  }

  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));

  siz = CVImageBufferGetEncodedSize(vf->vf_buf);
  fi.width = siz.width;
  fi.height = siz.height;

  fi.duration = vf->vf_duration > 10000 ? vf->vf_duration : vdad->vdad_estimated_duration;

  siz = CVImageBufferGetDisplaySize(vf->vf_buf);
  fi.dar.num = siz.width;
  fi.dar.den = siz.height;

  fi.pix_fmt = PIX_FMT_YUV420P;
  fi.pts = vf->vf_pts;
  fi.color_space = -1;
  fi.epoch = vf->vf_epoch;

  video_decoder_t *vd = vdad->vdad_vd;

  vd->vd_estimated_duration = fi.duration; // For bitrate calculations

  if(fi.duration > 0)
    video_deliver_frame(vd, FRAME_BUFFER_TYPE_LIBAV_FRAME, &frame,
			&fi, vf->vf_send_pts);

  CVPixelBufferUnlockBaseAddress(vf->vf_buf, 0);
  vdad->vdad_last_pts = vf->vf_pts;
}

/**
 *
 */
static void
destroy_frame(vda_frame_t *vf)
{
  CFRelease(vf->vf_buf);
  LIST_REMOVE(vf, vf_link);
  free(vf);
}


/**
 *
 */
static void
destroy_frames(vda_decoder_t *vdad)
{
  vda_frame_t *vf;
  while((vf = LIST_FIRST(&vdad->vdad_frames)) != NULL)
    destroy_frame(vf);
}

/**
 *
 */
static void
vda_callback(void *aux, CFDictionaryRef frame_info, OSStatus status, 
	     uint32_t infoFlags, CVImageBufferRef buf)
{
  vda_decoder_t *vdad = aux;
  CFNumberRef ref;
  vda_frame_t *vf;
  uint8_t skip;
  if(buf == NULL)
    return;

  ref = CFDictionaryGetValue(frame_info, CFSTR("skip"));
  CFNumberGetValue(ref, kCFNumberSInt8Type, &skip);

  if(skip)
    return;

  vf = malloc(sizeof(vda_frame_t));
  ref = CFDictionaryGetValue(frame_info, CFSTR("pts"));
  CFNumberGetValue(ref, kCFNumberSInt64Type, &vf->vf_pts);

  ref = CFDictionaryGetValue(frame_info, CFSTR("duration"));
  CFNumberGetValue(ref, kCFNumberSInt32Type, &vf->vf_duration);

  ref = CFDictionaryGetValue(frame_info, CFSTR("epoch"));
  CFNumberGetValue(ref, kCFNumberSInt8Type, &vf->vf_epoch);

  ref = CFDictionaryGetValue(frame_info, CFSTR("send_pts"));
  CFNumberGetValue(ref, kCFNumberSInt8Type, &vf->vf_send_pts);


  vf->vf_buf = buf;
  CFRetain(buf);

  hts_mutex_lock(&vdad->vdad_mutex);

  LIST_INSERT_SORTED(&vdad->vdad_frames, vf, vf_link, vf_cmp);

  if(vdad->vdad_max_ts != AV_NOPTS_VALUE) {
    if(vf->vf_pts > vdad->vdad_max_ts) {
      vdad->vdad_flush_to = vdad->vdad_max_ts;
      vdad->vdad_max_ts = vf->vf_pts;
    }
  } else {
    vdad->vdad_max_ts = vf->vf_pts;
  }
  hts_mutex_unlock(&vdad->vdad_mutex);
}
  

/**
 *
 */
static void
vda_decode(struct media_codec *mc, struct video_decoder *vd,
	   struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  vda_decoder_t *vdad = mc->opaque;
  CFDictionaryRef user_info;
  CFDataRef coded_frame;
  const int num_kvs = 6;
  CFStringRef keys[num_kvs];
  CFNumberRef values[num_kvs];
  const int keyframe = mb->mb_keyframe;
  const int send_pts = mb->mb_send_pts;
  vda_frame_t *vf;
  int i;
  uint8_t skip = mb->mb_skip;

  if(vd->vd_do_flush) {
    VDADecoderFlush(vdad->vdad_decoder, 1);
    hts_mutex_lock(&vdad->vdad_mutex);
    destroy_frames(vdad);
    vdad->vdad_max_ts = AV_NOPTS_VALUE;
    vdad->vdad_flush_to = AV_NOPTS_VALUE;
    vdad->vdad_last_pts = AV_NOPTS_VALUE;
    vd->vd_do_flush = 0;
    hts_mutex_unlock(&vdad->vdad_mutex);
  }

  vdad->vdad_vd = vd;

  coded_frame = CFDataCreate(kCFAllocatorDefault, mb->mb_data, mb->mb_size);


  keys[0] = CFSTR("pts");
  keys[1] = CFSTR("duration");
  keys[2] = CFSTR("keyframe");
  keys[3] = CFSTR("epoch");
  keys[4] = CFSTR("send_pts");
  keys[5] = CFSTR("skip");

  values[0] = CFNumberCreate(NULL, kCFNumberSInt64Type, &mb->mb_pts);
  values[1] = CFNumberCreate(NULL, kCFNumberSInt32Type, &mb->mb_duration);
  values[2] = CFNumberCreate(NULL, kCFNumberSInt32Type, &keyframe);
  values[3] = CFNumberCreate(NULL, kCFNumberSInt8Type, &mb->mb_epoch);
  values[4] = CFNumberCreate(NULL, kCFNumberSInt8Type, &send_pts);
  values[5] = CFNumberCreate(NULL, kCFNumberSInt8Type, &skip);

  user_info = CFDictionaryCreate(kCFAllocatorDefault,
				 (const void **)keys,
				 (const void **)values,
				 num_kvs,
				 &kCFTypeDictionaryKeyCallBacks,
				 &kCFTypeDictionaryValueCallBacks);
  for(i = 0; i < num_kvs; i++)
    CFRelease(values[i]);
  uint32_t flags = 0;

  VDADecoderDecode(vdad->vdad_decoder, flags, coded_frame, user_info);
  CFRelease(user_info);
  CFRelease(coded_frame);

  prop_set_string(mq->mq_prop_codec, "h264 (VDA)");
 
  hts_mutex_lock(&vdad->vdad_mutex);

  if(vdad->vdad_flush_to != AV_NOPTS_VALUE) {
    while((vf = LIST_FIRST(&vdad->vdad_frames)) != NULL) {
      if(vdad->vdad_flush_to < vf->vf_pts)
	break;
      LIST_REMOVE(vf, vf_link);
      hts_mutex_unlock(&vdad->vdad_mutex);
      emit_frame(vdad, vf);
      hts_mutex_lock(&vdad->vdad_mutex);
      CFRelease(vf->vf_buf);
      free(vf);
    }
  }
  hts_mutex_unlock(&vdad->vdad_mutex);
}


/**
 *
 */
static void
vda_close(struct media_codec *mc)
{
  vda_decoder_t *vdad = mc->opaque;
  VDADecoderFlush(vdad->vdad_decoder, 0);  
  VDADecoderDestroy(vdad->vdad_decoder);
  destroy_frames(vdad);
}


/**
 *
 */
int
video_vda_codec_create(media_codec_t *mc, enum CodecID id,
		       AVCodecContext *ctx, media_codec_params_t *mcp,
		       media_pipe_t *mp)
{
  OSStatus status = kVDADecoderNoErr;
  CFNumberRef height;
  CFNumberRef width;
  CFNumberRef format;
  CFDataRef avc_data;
  CFMutableDictionaryRef ci;
  CFMutableDictionaryRef ba;
  CFMutableDictionaryRef isp;
  CFNumberRef cv_pix_fmt;


  if(!video_settings.vda)
    return 1;
  
  const int pixfmt = kCVPixelFormatType_420YpCbCr8Planar;
  const int avc1 = 'avc1';

  if(id != CODEC_ID_H264)
    return 1;

  ci = CFDictionaryCreateMutable(kCFAllocatorDefault,
				 4,
				 &kCFTypeDictionaryKeyCallBacks,
				 &kCFTypeDictionaryValueCallBacks);

  height   = CFNumberCreate(NULL, kCFNumberSInt32Type, &mcp->height);
  width    = CFNumberCreate(NULL, kCFNumberSInt32Type, &mcp->width);
  format   = CFNumberCreate(NULL, kCFNumberSInt32Type, &avc1);
  avc_data = CFDataCreate(NULL, ctx->extradata, ctx->extradata_size);

  CFDictionarySetValue(ci, kVDADecoderConfiguration_Height, height);
  CFDictionarySetValue(ci, kVDADecoderConfiguration_Width, width);
  CFDictionarySetValue(ci, kVDADecoderConfiguration_SourceFormat, format);
  CFDictionarySetValue(ci, kVDADecoderConfiguration_avcCData, avc_data);

  ba = CFDictionaryCreateMutable(NULL,
				 2,
				 &kCFTypeDictionaryKeyCallBacks,
				 &kCFTypeDictionaryValueCallBacks);
  isp = CFDictionaryCreateMutable(NULL,
				  0,
				  &kCFTypeDictionaryKeyCallBacks,
				  &kCFTypeDictionaryValueCallBacks);
  cv_pix_fmt      = CFNumberCreate(NULL,
				   kCFNumberSInt32Type,
				   &pixfmt);

  CFDictionarySetValue(ba, kCVPixelBufferPixelFormatTypeKey, cv_pix_fmt);
  CFDictionarySetValue(ba, kCVPixelBufferIOSurfacePropertiesKey, isp);
  
  vda_decoder_t *vdad = calloc(1, sizeof(vda_decoder_t));
  

  status = VDADecoderCreate(ci, ba, (void *)vda_callback,
			    vdad, &vdad->vdad_decoder);

  CFRelease(height);
  CFRelease(width);
  CFRelease(format);
  CFRelease(avc_data);
  CFRelease(ci);
  CFRelease(isp);
  CFRelease(cv_pix_fmt);
  CFRelease(ba);

  if(kVDADecoderNoErr != status) {
    free(vdad);
    return 1;
  }

  hts_mutex_init(&vdad->vdad_mutex);
  vdad->vdad_max_ts = AV_NOPTS_VALUE;
  vdad->vdad_flush_to = AV_NOPTS_VALUE;
  vdad->vdad_last_pts = AV_NOPTS_VALUE;
  vdad->vdad_mc = mc;
  mc->opaque = vdad;
  mc->decode = vda_decode;
  mc->close = vda_close;

  TRACE(TRACE_INFO, "VDA", "Opened decoder");
  
  return 0;
}

