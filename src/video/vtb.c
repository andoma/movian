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

#include <VideoToolbox/VideoToolbox.h>

#include "main.h"
#include "media/media.h"
#include "video_decoder.h"
#include "video_settings.h"
#include "h264_annexb.h"


LIST_HEAD(vtb_frame_list, vtb_frame);

/**
 *
 */
typedef struct vtb_frame {
  LIST_ENTRY(vtb_frame) vf_link;
  CVPixelBufferRef vf_buf;
  media_buf_meta_t vf_mbm;
} vtb_frame_t;



/**
 *
 */
typedef struct vtb_decoder {
  VTDecompressionSessionRef vtbd_session;
  CMVideoFormatDescriptionRef vtbd_fmt;

  hts_mutex_t vtbd_mutex;
  video_decoder_t *vtbd_vd;

  struct vtb_frame_list vtbd_frames;
  int64_t vtbd_max_ts;
  int64_t vtbd_flush_to;
  int64_t vtbd_last_pts;
  int vtbd_estimated_duration;
  int vtbd_pixel_format;
} vtb_decoder_t;


/**
 *
 */
static void
destroy_frame(vtb_frame_t *vf)
{
  CFRelease(vf->vf_buf);
  LIST_REMOVE(vf, vf_link);
  free(vf);
}


/**
 *
 */
static void
destroy_frames(vtb_decoder_t *vtbd)
{
  vtb_frame_t *vf;
  while((vf = LIST_FIRST(&vtbd->vtbd_frames)) != NULL)
    destroy_frame(vf);
}


/**
 *
 */
static void
emit_frame(vtb_decoder_t *vtbd, vtb_frame_t *vf, media_queue_t *mq)
{
  CGSize siz;

  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));

  if(vtbd->vtbd_last_pts != PTS_UNSET && vf->vf_mbm.mbm_pts != PTS_UNSET) {
    int64_t d = vf->vf_mbm.mbm_pts - vtbd->vtbd_last_pts;

    if(d > 1000 && d < 1000000)
      vtbd->vtbd_estimated_duration = d;
  }

  siz = CVImageBufferGetDisplaySize(vf->vf_buf);
  fi.fi_dar_num = siz.width;
  fi.fi_dar_den = siz.height;

  fi.fi_pts = vf->vf_mbm.mbm_pts;
  fi.fi_color_space = -1;
  fi.fi_epoch = vf->vf_mbm.mbm_epoch;
  fi.fi_drive_clock = vf->vf_mbm.mbm_drive_clock;
  fi.fi_user_time = vf->vf_mbm.mbm_user_time;
  fi.fi_vshift = 1;
  fi.fi_hshift = 1;
  fi.fi_duration = vf->vf_mbm.mbm_duration > 10000 ? vf->vf_mbm.mbm_duration : vtbd->vtbd_estimated_duration;

  siz = CVImageBufferGetEncodedSize(vf->vf_buf);
  fi.fi_width = siz.width;
  fi.fi_height = siz.height;


  video_decoder_t *vd = vtbd->vtbd_vd;
  vd->vd_estimated_duration = fi.fi_duration; // For bitrate calculations

  switch(vtbd->vtbd_pixel_format) {
    case kCVPixelFormatType_420YpCbCr8Planar:
      fi.fi_type = 'YUVP';

      CVPixelBufferLockBaseAddress(vf->vf_buf, 0);

      for(int i = 0; i < 3; i++ ) {
        fi.fi_data[i]  = CVPixelBufferGetBaseAddressOfPlane(vf->vf_buf, i);
        fi.fi_pitch[i] = CVPixelBufferGetBytesPerRowOfPlane(vf->vf_buf, i);
      }

      if(fi.fi_duration > 0)
        video_deliver_frame(vd, &fi);

      CVPixelBufferUnlockBaseAddress(vf->vf_buf, 0);
      break;

    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
      fi.fi_type = 'CVPB';
      fi.fi_data[0] = (void *)vf->vf_buf;
      if(fi.fi_duration > 0)
        video_deliver_frame(vd, &fi);
      break;
  }



  vtbd->vtbd_last_pts = vf->vf_mbm.mbm_pts;

  char fmt[64];
  snprintf(fmt, sizeof(fmt), "h264 (VTB) %d x %d", fi.fi_width, fi.fi_height);
  prop_set_string(mq->mq_prop_codec, fmt);
}



/**
 *
 */
static int
vf_cmp(const vtb_frame_t *a, const vtb_frame_t *b)
{
  if(a->vf_mbm.mbm_epoch < b->vf_mbm.mbm_epoch)
    return -1;
  if(a->vf_mbm.mbm_epoch > b->vf_mbm.mbm_epoch)
    return 1;
  if(a->vf_mbm.mbm_pts < b->vf_mbm.mbm_pts)
    return -1;
  if(a->vf_mbm.mbm_pts > b->vf_mbm.mbm_pts)
    return 1;
  return 0;
}


/**
 *
 */
static void
picture_out(void *decompressionOutputRefCon,
            void *sourceFrameRefCon,
            OSStatus status,
            VTDecodeInfoFlags infoFlags,
            CVPixelBufferRef imageBuffer,
            CMTime pts,
            CMTime duration)
{
  media_buf_meta_t *mbm = sourceFrameRefCon;
  vtb_decoder_t *vtbd = decompressionOutputRefCon;

  if(imageBuffer == NULL)
    return; // No frame, typically from kVTDecodeFrame_DoNotOutputFrame

  vtb_frame_t *vf = malloc(sizeof(vtb_frame_t));
  vf->vf_mbm = *mbm;
  vf->vf_buf = imageBuffer;
  CFRetain(imageBuffer);

  hts_mutex_lock(&vtbd->vtbd_mutex);

  LIST_INSERT_SORTED(&vtbd->vtbd_frames, vf, vf_link, vf_cmp, vtb_frame_t);

  if(vtbd->vtbd_max_ts != PTS_UNSET) {
    if(vf->vf_mbm.mbm_pts > vtbd->vtbd_max_ts) {
      vtbd->vtbd_flush_to = vtbd->vtbd_max_ts;
      vtbd->vtbd_max_ts = vf->vf_mbm.mbm_pts;
    }
  } else {
    vtbd->vtbd_max_ts = vf->vf_mbm.mbm_pts;
  }
  hts_mutex_unlock(&vtbd->vtbd_mutex);
}



/**
 *
 */
static void
vtb_decode(struct media_codec *mc, struct video_decoder *vd,
	   struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  vtb_decoder_t *vtbd = mc->opaque;
  VTDecodeInfoFlags infoflags;
  int flags = kVTDecodeFrame_EnableAsynchronousDecompression | kVTDecodeFrame_EnableTemporalProcessing;
  OSStatus status;
  CMBlockBufferRef block_buf;
  CMSampleBufferRef sample_buf;

  vtbd->vtbd_vd = vd;

  status =
    CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                       mb->mb_data, mb->mb_size,
                                       kCFAllocatorNull,
                                       NULL, 0, mb->mb_size, 0, &block_buf);
  if(status) {
    TRACE(TRACE_ERROR, "VTB", "Data buffer allocation error %d", status);
    return;
  }

  CMSampleTimingInfo ti;

  ti.duration              = CMTimeMake(mb->mb_duration, 1000000);
  ti.presentationTimeStamp = CMTimeMake(mb->mb_pts, 1000000);
  ti.decodeTimeStamp       = CMTimeMake(mb->mb_dts, 1000000);

  status =
    CMSampleBufferCreate(kCFAllocatorDefault,
                         block_buf, TRUE, 0, 0, vtbd->vtbd_fmt,
                         1, 1, &ti, 0, NULL, &sample_buf);

  CFRelease(block_buf);
  if(status) {
    TRACE(TRACE_ERROR, "VTB", "Sample buffer allocation error %d", status);
    return;
  }

  void *frame_opaque = &vd->vd_reorder[vd->vd_reorder_ptr];
  copy_mbm_from_mb(frame_opaque, mb);
  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;

  if(mb->mb_skip)
    flags |= kVTDecodeFrame_DoNotOutputFrame;

  status =
    VTDecompressionSessionDecodeFrame(vtbd->vtbd_session, sample_buf, flags,
                                      frame_opaque, &infoflags);
  CFRelease(sample_buf);
  if(status) {
    TRACE(TRACE_ERROR, "VTB", "Decoding error %d", status);
  }

  hts_mutex_lock(&vtbd->vtbd_mutex);

  if(vtbd->vtbd_flush_to != PTS_UNSET) {
    vtb_frame_t *vf;
    while((vf = LIST_FIRST(&vtbd->vtbd_frames)) != NULL) {
      if(vtbd->vtbd_flush_to < vf->vf_mbm.mbm_pts)
	break;
      LIST_REMOVE(vf, vf_link);
      hts_mutex_unlock(&vtbd->vtbd_mutex);
      emit_frame(vtbd, vf, mq);
      hts_mutex_lock(&vtbd->vtbd_mutex);
      CFRelease(vf->vf_buf);
      free(vf);
    }
  }
  hts_mutex_unlock(&vtbd->vtbd_mutex);
}


/**
 *
 */
static void
vtb_flush(struct media_codec *mc, struct video_decoder *vd)
{
  vtb_decoder_t *vtbd = mc->opaque;
  VTDecompressionSessionWaitForAsynchronousFrames(vtbd->vtbd_session);
  hts_mutex_lock(&vtbd->vtbd_mutex);
  destroy_frames(vtbd);
  vtbd->vtbd_max_ts   = PTS_UNSET;
  vtbd->vtbd_flush_to = PTS_UNSET;
  vtbd->vtbd_last_pts = PTS_UNSET;
  hts_mutex_unlock(&vtbd->vtbd_mutex);
}


/**
 *
 */
static void
vtb_close(struct media_codec *mc)
{
  vtb_decoder_t *vtbd = mc->opaque;
  VTDecompressionSessionWaitForAsynchronousFrames(vtbd->vtbd_session);
  destroy_frames(vtbd);

  VTDecompressionSessionInvalidate(vtbd->vtbd_session);
  CFRelease(vtbd->vtbd_session);

  CFRelease(vtbd->vtbd_fmt);
  free(vtbd);
}


/**
 *
 */
static void
dict_set_int32(CFMutableDictionaryRef dict, CFStringRef key, int value)
{
  CFNumberRef num = CFNumberCreate(NULL, kCFNumberSInt32Type, &value);
  CFDictionarySetValue(dict, key, num);
  CFRelease(num);
}


/**
 *
 */
static int
video_vtb_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
		       media_pipe_t *mp)
{
  OSStatus status;

  if(!video_settings.video_accel)
    return 1;

  switch(mc->codec_id) {
  case AV_CODEC_ID_H264:
    break;
  default:
    return 1;
  }


  if(mcp == NULL || mcp->extradata == NULL || mcp->extradata_size == 0 ||
     ((const uint8_t *)mcp->extradata)[0] != 1)
    return h264_annexb_to_avc(mc, mp, &video_vtb_codec_create);

  CFMutableDictionaryRef config_dict =
    CFDictionaryCreateMutable(kCFAllocatorDefault,
                              2,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);

  CFDictionarySetValue(config_dict,
                       kCVImageBufferChromaLocationBottomFieldKey,
                       kCVImageBufferChromaLocation_Left);

  CFDictionarySetValue(config_dict,
                       kCVImageBufferChromaLocationTopFieldKey,
                       kCVImageBufferChromaLocation_Left);

  // Setup extradata
  CFMutableDictionaryRef extradata_dict =
    CFDictionaryCreateMutable(kCFAllocatorDefault,
                              1,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);


  CFDataRef extradata = CFDataCreate(kCFAllocatorDefault, mcp->extradata,
                                     mcp->extradata_size);
  CFDictionarySetValue(extradata_dict, CFSTR("avcC"), extradata);
  CFRelease(extradata);
  CFDictionarySetValue(config_dict,
                       kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms,
                       extradata_dict);
  CFRelease(extradata_dict);

#if !TARGET_OS_IPHONE
  // Enable and force HW accelration
  CFDictionarySetValue(config_dict,
                       kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder,
                       kCFBooleanTrue);

  CFDictionarySetValue(config_dict,
                       kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder,
                       kCFBooleanTrue);
#endif

  CMVideoFormatDescriptionRef fmt;

  status = CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                          kCMVideoCodecType_H264,
                                          mcp->width,
                                          mcp->height,
                                          config_dict,
                                          &fmt);
  if(status) {
    TRACE(TRACE_DEBUG, "VTB", "Unable to create description %d", status);
    return 1;
  }


  CFMutableDictionaryRef surface_dict =
    CFDictionaryCreateMutable(kCFAllocatorDefault,
                              2,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);

  CFDictionarySetValue(surface_dict,
#if !TARGET_OS_IPHONE
                       kCVPixelBufferOpenGLCompatibilityKey,
#else
                       kCVPixelBufferOpenGLESCompatibilityKey,
#endif
                       kCFBooleanTrue);

  vtb_decoder_t *vtbd = calloc(1, sizeof(vtb_decoder_t));

  dict_set_int32(surface_dict, kCVPixelBufferWidthKey, mcp->width);
  dict_set_int32(surface_dict, kCVPixelBufferHeightKey, mcp->height);

#if TARGET_OS_IPHONE
    vtbd->vtbd_pixel_format = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
#else
    vtbd->vtbd_pixel_format = kCVPixelFormatType_420YpCbCr8Planar;
#endif

  dict_set_int32(surface_dict, kCVPixelBufferPixelFormatTypeKey,
                 vtbd->vtbd_pixel_format);

  int linewidth = mcp->width;

  switch(vtbd->vtbd_pixel_format) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
      linewidth *= 2;
      break;
  }

  dict_set_int32(surface_dict, kCVPixelBufferBytesPerRowAlignmentKey, linewidth);

  VTDecompressionOutputCallbackRecord cb = {
    .decompressionOutputCallback = picture_out,
    .decompressionOutputRefCon = vtbd
  };

  /* create decompression session */
  status = VTDecompressionSessionCreate(kCFAllocatorDefault,
                                        fmt,
                                        config_dict,
                                        surface_dict,
                                        &cb,
                                        &vtbd->vtbd_session);

  CFRelease(config_dict);
  CFRelease(surface_dict);

  if(status) {
    TRACE(TRACE_DEBUG, "VTB", "Failed to open -- %d", status);
    CFRelease(fmt);
    return 1;

  }
  vtbd->vtbd_fmt = fmt;
  vtbd->vtbd_max_ts   = PTS_UNSET;
  vtbd->vtbd_flush_to = PTS_UNSET;
  vtbd->vtbd_last_pts = PTS_UNSET;

  mc->opaque = vtbd;
  mc->decode = vtb_decode;
  mc->close = vtb_close;
  mc->flush = vtb_flush;

  TRACE(TRACE_DEBUG, "VTB", "Opened decoder");
  return 0;
}

REGISTER_CODEC(NULL, video_vtb_codec_create, 10);
