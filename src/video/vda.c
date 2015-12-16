/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFString.h>

#include <VideoDecodeAcceleration/VDADecoder.h>

#include "main.h"
#include "media/media.h"
#include "video_decoder.h"
#include "video_settings.h"
#include "h264_annexb.h"

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
  uint8_t vf_drive_clock;
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

  int vdad_width;
  int vdad_height;

  int vdad_zero_copy;

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
emit_frame(vda_decoder_t *vdad, vda_frame_t *vf, media_queue_t *mq)
{
  CGSize siz;

  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));

  if(vdad->vdad_last_pts != PTS_UNSET && vf->vf_pts != PTS_UNSET) {
    int64_t d = vf->vf_pts - vdad->vdad_last_pts;

    if(d > 1000 && d < 1000000)
      vdad->vdad_estimated_duration = d;
  }

  siz = CVImageBufferGetDisplaySize(vf->vf_buf);
  fi.fi_dar_num = siz.width;
  fi.fi_dar_den = siz.height;

  fi.fi_pts = vf->vf_pts;
  fi.fi_color_space = -1;
  fi.fi_epoch = vf->vf_epoch;
  fi.fi_drive_clock = vf->vf_drive_clock;
  fi.fi_vshift = 1;
  fi.fi_hshift = 1;
  fi.fi_duration = vf->vf_duration > 10000 ? vf->vf_duration : vdad->vdad_estimated_duration;

  siz = CVImageBufferGetEncodedSize(vf->vf_buf);
  fi.fi_width = siz.width;
  fi.fi_height = siz.height;


  video_decoder_t *vd = vdad->vdad_vd;
  vd->vd_estimated_duration = fi.fi_duration; // For bitrate calculations

  if(vdad->vdad_zero_copy) {

    fi.fi_type = 'VDA';
    fi.fi_data[0] = (void *)vf->vf_buf;
    if(fi.fi_duration > 0)
      video_deliver_frame(vd, &fi);

  } else {

    fi.fi_type = 'YUVP';

    CVPixelBufferLockBaseAddress(vf->vf_buf, 0);

    for(int i = 0; i < 3; i++ ) {
      fi.fi_data[i] = CVPixelBufferGetBaseAddressOfPlane(vf->vf_buf, i);
      fi.fi_pitch[i] = CVPixelBufferGetBytesPerRowOfPlane(vf->vf_buf, i);
    }


    if(fi.fi_duration > 0)
      video_deliver_frame(vd, &fi);

    CVPixelBufferUnlockBaseAddress(vf->vf_buf, 0);

  }

  vdad->vdad_last_pts = vf->vf_pts;

  char fmt[64];
  snprintf(fmt, sizeof(fmt), "h264 (VDA) %d x %d", fi.fi_width, fi.fi_height);
  prop_set_string(mq->mq_prop_codec, fmt);
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

  ref = CFDictionaryGetValue(frame_info, CFSTR("drive_clock"));
  CFNumberGetValue(ref, kCFNumberSInt8Type, &vf->vf_drive_clock);


  vf->vf_buf = buf;
  CFRetain(buf);

  hts_mutex_lock(&vdad->vdad_mutex);

  LIST_INSERT_SORTED(&vdad->vdad_frames, vf, vf_link, vf_cmp, vda_frame_t);

  if(vdad->vdad_max_ts != PTS_UNSET) {
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
  const int drive_clock = mb->mb_drive_clock;
  vda_frame_t *vf;
  int i;
  uint8_t skip = mb->mb_skip;

  vdad->vdad_vd = vd;

  coded_frame = CFDataCreate(kCFAllocatorDefault, mb->mb_data, mb->mb_size);


  keys[0] = CFSTR("pts");
  keys[1] = CFSTR("duration");
  keys[2] = CFSTR("keyframe");
  keys[3] = CFSTR("epoch");
  keys[4] = CFSTR("drive_clock");
  keys[5] = CFSTR("skip");

  values[0] = CFNumberCreate(NULL, kCFNumberSInt64Type, &mb->mb_pts);
  values[1] = CFNumberCreate(NULL, kCFNumberSInt32Type, &mb->mb_duration);
  values[2] = CFNumberCreate(NULL, kCFNumberSInt32Type, &keyframe);
  values[3] = CFNumberCreate(NULL, kCFNumberSInt8Type, &mb->mb_epoch);
  values[4] = CFNumberCreate(NULL, kCFNumberSInt8Type, &drive_clock);
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


  hts_mutex_lock(&vdad->vdad_mutex);

  if(vdad->vdad_flush_to != PTS_UNSET) {
    while((vf = LIST_FIRST(&vdad->vdad_frames)) != NULL) {
      if(vdad->vdad_flush_to < vf->vf_pts)
	break;
      LIST_REMOVE(vf, vf_link);
      hts_mutex_unlock(&vdad->vdad_mutex);
      emit_frame(vdad, vf, mq);
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
vda_flush(struct media_codec *mc, struct video_decoder *vd)
{
  vda_decoder_t *vdad = mc->opaque;

  VDADecoderFlush(vdad->vdad_decoder, 1);
  hts_mutex_lock(&vdad->vdad_mutex);
  destroy_frames(vdad);
  vdad->vdad_max_ts   = PTS_UNSET;
  vdad->vdad_flush_to = PTS_UNSET;
  vdad->vdad_last_pts = PTS_UNSET;
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
static int
video_vda_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
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
  int zero_copy = 1;

  if(mc->codec_id != AV_CODEC_ID_H264 || !video_settings.video_accel)
    return 1;

  if(mcp == NULL || mcp->extradata == NULL || mcp->extradata_size == 0 ||
     ((const uint8_t *)mcp->extradata)[0] != 1)
    return h264_annexb_to_avc(mc, mp, &video_vda_codec_create);

  const int pixfmt = zero_copy ?
    kCVPixelFormatType_422YpCbCr8 :
    kCVPixelFormatType_420YpCbCr8Planar;

  const int avc1 = 'avc1';

  ci = CFDictionaryCreateMutable(kCFAllocatorDefault,
				 4,
				 &kCFTypeDictionaryKeyCallBacks,
				 &kCFTypeDictionaryValueCallBacks);

  height   = CFNumberCreate(NULL, kCFNumberSInt32Type, &mcp->height);
  width    = CFNumberCreate(NULL, kCFNumberSInt32Type, &mcp->width);
  format   = CFNumberCreate(NULL, kCFNumberSInt32Type, &avc1);
  avc_data = CFDataCreate(NULL, mcp->extradata, mcp->extradata_size);

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
  vdad->vdad_zero_copy = zero_copy;
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
  vdad->vdad_max_ts   = PTS_UNSET;
  vdad->vdad_flush_to = PTS_UNSET;
  vdad->vdad_last_pts = PTS_UNSET;
  vdad->vdad_mc = mc;
  mc->opaque = vdad;
  mc->decode = vda_decode;
  mc->close = vda_close;
  mc->flush = vda_flush;

  TRACE(TRACE_DEBUG, "VDA", "Opened decoder");
  return 0;
}

REGISTER_CODEC(NULL, video_vda_codec_create, 100);
