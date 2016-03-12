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
#include <stdio.h>
#include <assert.h>

#include <bcm_host.h>

#include "video/video_decoder.h"
#include "omx.h"
#include "rpi_video.h"
#include "rpi.h"
#include "misc/minmax.h"

static char omx_enable_mpg2;
static char omx_enable_vp8;
static char omx_enable_vp6;
static char omx_enable_mjpeg;
static char omx_enable_wvc1;
static char omx_enable_h263;
static char omx_enable_h264;
static char omx_enable_theora;
static char omx_enable_mpeg4;



/**
 *
 */
static void
rpi_video_port_settings_changed(omx_component_t *oc)
{
  media_codec_t *mc = oc->oc_opaque;
  const rpi_video_codec_t *rvc = mc->opaque;
  media_pipe_t *mp = mc->mp;
  frame_info_t *fi = calloc(1, sizeof(frame_info_t));


  int sar_num = 1;
  int sar_den = 1;

  if(rvc->rvc_sar_num && rvc->rvc_sar_den) {

    sar_num = rvc->rvc_sar_num;
    sar_den = rvc->rvc_sar_den;

  } else {

    OMX_CONFIG_POINTTYPE pixel_aspect;
    OMX_INIT_STRUCTURE(pixel_aspect);
    pixel_aspect.nPortIndex = 131;
    if(OMX_GetParameter(oc->oc_handle, OMX_IndexParamBrcmPixelAspectRatio,
			&pixel_aspect) == OMX_ErrorNone) {
      sar_num = pixel_aspect.nX ?: sar_num;
      sar_den = pixel_aspect.nY ?: sar_den;
    }
  }

  OMX_CONFIG_INTERLACETYPE interlace;
  OMX_INIT_STRUCTURE(interlace);
  interlace.nPortIndex = 131;
  if(OMX_GetConfig(oc->oc_handle, OMX_IndexConfigCommonInterlace,
                   &interlace) == OMX_ErrorNone) {

    switch(interlace.eMode) {
    default:
      break;
    case OMX_InterlaceFieldsInterleavedUpperFirst:
      fi->fi_tff = 1;
      fi->fi_interlaced = 1;
      TRACE(TRACE_DEBUG, "VideoCore", "Interlaced picture top-field-first");
      break;
    case OMX_InterlaceFieldsInterleavedLowerFirst:
      fi->fi_interlaced = 1;
      TRACE(TRACE_DEBUG, "VideoCore", "Interlaced picture bottom-field-first");
      break;
    }
  }

  OMX_PARAM_PORTDEFINITIONTYPE port_image;
  OMX_INIT_STRUCTURE(port_image);
  port_image.nPortIndex = 131;

  if(OMX_GetParameter(oc->oc_handle, OMX_IndexParamPortDefinition,
		      &port_image) == OMX_ErrorNone) {
    char codec_info[64];
    snprintf(codec_info, sizeof(codec_info), "%s %dx%d%c (VideoCore)", 
	     rvc->rvc_name,
	     port_image.format.video.nFrameWidth,
	     port_image.format.video.nFrameHeight,
             fi->fi_interlaced ? 'i' : 'p');
    prop_set_string(mp->mp_video.mq_prop_codec, codec_info);
    TRACE(TRACE_DEBUG, "VideoCore",
	  "Video decoder output port settings changed to %s (SAR: %d:%d)",
	  codec_info, sar_num, sar_den);

    fi->fi_width   = port_image.format.video.nFrameWidth;
    fi->fi_height  = port_image.format.video.nFrameHeight;
  }
  fi->fi_dar_num = sar_num * fi->fi_width;
  fi->fi_dar_den = sar_den * fi->fi_height;


  hts_mutex_lock(&mp->mp_mutex);
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = MB_CTRL_RECONFIGURE;
  mb->mb_frame_info = fi;
  mb->mb_dtor = media_buf_dtor_frame_info;

  mb->mb_cw = media_codec_ref(mc);
  mb_enq(mp, &mp->mp_video, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static int
rpi_codec_decode_locked(struct media_codec *mc, struct video_decoder *vd,
                        struct media_queue *mq, struct media_buf *mb)
{
  rpi_video_codec_t *rvc = mc->opaque;
  const void *data = mb->mb_data;
  size_t len       = mb->mb_size;
  int is_bframe = 0;

  omx_component_t *oc = rvc->rvc_decoder;

  if(oc->oc_avail_bytes < len) {
    oc->oc_need_bytes = len;
    return 1;
  }

  switch(mc->codec_id) {

  case AV_CODEC_ID_MPEG4:

    if(mb->mb_size <= 7)
      return 0;

    int frame_type = 0;
    const uint8_t *d = data;
    if(d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x01 && d[3] == 0xb6)
      frame_type = d[4] >> 6;

    if(frame_type == 2)
      is_bframe = 1;
    break;

  case AV_CODEC_ID_H264:
    h264_parser_decode_data(&rvc->rvc_h264_parser, mb->mb_data, mb->mb_size);
    if(rvc->rvc_h264_parser.slice_type_nos == SLICE_TYPE_B)
      is_bframe = 1;
    break;
  }

  media_buf_meta_t *mbm = &vd->vd_reorder[vd->vd_reorder_ptr];
  copy_mbm_from_mb(mbm, mb);
  mbm->mbm_pts = video_decoder_infer_pts(mbm, vd, is_bframe);

  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;

  int domark = 1;

  OMX_BUFFERHEADERTYPE *q = NULL, **pq = &q, *buf;
#if 0
  printf("FRAME\t");
  if(mb->mb_pts != PTS_UNSET) {
    static int64_t last_pts;
    printf("PTS %lld (%lld)", mb->mb_pts, mb->mb_pts - last_pts);
    last_pts = mb->mb_pts;
  }
  if(mb->mb_dts != PTS_UNSET) {
    static int64_t last_dts;
    printf("   DTS %lld (%lld)", mb->mb_dts, mb->mb_dts - last_dts);
    last_dts = mb->mb_dts;
  }
  printf("\n");
#endif
  while(len > 0) {
    buf = oc->oc_avail;
    assert(buf != NULL);

    oc->oc_inflight_buffers++;
    oc->oc_avail_bytes -= buf->nAllocLen;
    oc->oc_avail = buf->pAppPrivate;

    buf->nOffset = 0;
    buf->nFilledLen = MIN(len, buf->nAllocLen);
    memcpy(buf->pBuffer, data, buf->nFilledLen);
    buf->nFlags = 0;

    if(vd->vd_render_component && domark) {
      buf->hMarkTargetComponent = vd->vd_render_component;
      buf->pMarkData = mbm;
      domark = 0;
    }

    if(rvc->rvc_last_epoch != mbm->mbm_epoch) {
      buf->nFlags |= OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_DISCONTINUITY;
      rvc->rvc_last_epoch = mbm->mbm_epoch;
    }

    if(len <= buf->nAllocLen)
      buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    data += buf->nFilledLen;
    len  -= buf->nFilledLen;

    if(mbm->mbm_pts != PTS_UNSET)
      buf->nTimeStamp = omx_ticks_from_s64(mbm->mbm_pts);
    else {
      buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
      buf->nTimeStamp = omx_ticks_from_s64(0);
    }

    if(mbm->mbm_skip)
      buf->nFlags |= OMX_BUFFERFLAG_DECODEONLY;

    // Enqueue on temporary stack queue

    buf->pAppPrivate = *pq;
    *pq = buf;
    pq = (OMX_BUFFERHEADERTYPE **)&buf->pAppPrivate;
  }

  hts_mutex_unlock(&vd->vd_mp->mp_mutex);

  while(q != NULL) {
    buf = q;
    q = q->pAppPrivate;
    omxchk(OMX_EmptyThisBuffer(rvc->rvc_decoder->oc_handle, buf));
  }

  hts_mutex_lock(&vd->vd_mp->mp_mutex);

  return 0;
}


/**
 *
 */
static void
rpi_codec_flush(struct media_codec *mc, struct video_decoder *vd)
{
  rpi_video_codec_t *rvc = mc->opaque;

  omx_flush_port(rvc->rvc_decoder, 130);
  omx_flush_port(rvc->rvc_decoder, 131);
}


/**
 *
 */
static void
rpi_codec_close(struct media_codec *mc)
{
  rpi_video_codec_t *rvc = mc->opaque;

  omx_flush_port(rvc->rvc_decoder, 130);
  omx_flush_port(rvc->rvc_decoder, 131);

  omx_wait_buffers(rvc->rvc_decoder);

  omx_set_state(rvc->rvc_decoder, OMX_StateIdle);
  omx_release_buffers(rvc->rvc_decoder, 130);
  omx_set_state(rvc->rvc_decoder, OMX_StateLoaded);

  omx_component_destroy(rvc->rvc_decoder);

  h264_parser_fini(&rvc->rvc_h264_parser);
  free(rvc);
}


/**
 *
 */
static void
rpi_codec_reconfigure(struct media_codec *mc, const frame_info_t *fi)
{
  media_pipe_t *mp = mc->mp;
  mp->mp_set_video_codec('omx', mc, mp->mp_video_frame_opaque, fi);
}


/**
 *
 */
static int
rpi_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
		 media_pipe_t *mp)
{
  int fmt;
  const char *name = NULL;

  switch(mc->codec_id) {

  case AV_CODEC_ID_H263:
    if(!omx_enable_h263)
      return 1;
    fmt = OMX_VIDEO_CodingH263;
    name = "h263";
    break;

  case AV_CODEC_ID_MPEG4:
    if(!omx_enable_mpeg4)
      return 1;
    fmt = OMX_VIDEO_CodingMPEG4;
    name = "MPEG-4";
    break;

  case AV_CODEC_ID_H264:
    if(!omx_enable_h264)
      return 1;
    fmt = OMX_VIDEO_CodingAVC;
    name = "h264";
    break;

  case AV_CODEC_ID_MPEG1VIDEO:
  case AV_CODEC_ID_MPEG2VIDEO:
    if(!omx_enable_mpg2)
      return 1;
    fmt = OMX_VIDEO_CodingMPEG2;
    name = "MPEG2";
    break;

  case AV_CODEC_ID_VP8:
    if(!omx_enable_vp8)
      return 1;
    fmt = OMX_VIDEO_CodingVP8;
    name = "VP8";
    break;

  case AV_CODEC_ID_VP6F:
  case AV_CODEC_ID_VP6A:
    if(!omx_enable_vp6)
      return 1;
    fmt = OMX_VIDEO_CodingVP6;
    name = "VP6";
    break;

  case AV_CODEC_ID_MJPEG:
  case AV_CODEC_ID_MJPEGB:
    if(!omx_enable_mjpeg)
      return 1;
    fmt = OMX_VIDEO_CodingMJPEG;
    name = "MJPEG";
    break;

  case AV_CODEC_ID_VC1:
  case AV_CODEC_ID_WMV3:
    if(!omx_enable_wvc1)
      return 1;

    if(mcp == NULL || mcp->extradata_size == 0)
      return 1;

    fmt = OMX_VIDEO_CodingWMV;
    name = "VC1";
    break;

  case AV_CODEC_ID_THEORA:
    if(!omx_enable_theora)
      return 1;

    fmt = OMX_VIDEO_CodingTheora;
    name = "Theora";
    break;

  default:
    return 1;
  }

  rpi_video_codec_t *rvc = calloc(1, sizeof(rpi_video_codec_t));

  omx_component_t *d = omx_component_create("OMX.broadcom.video_decode",
					    &mp->mp_mutex,
					    &mp->mp_video.mq_avail);

  if(d == NULL) {
    free(rvc);
    return 1;
  }

  if(mcp != NULL) {
    rvc->rvc_sar_num = mcp->sar_num;
    rvc->rvc_sar_den = mcp->sar_den;
  }

  rvc->rvc_decoder = d;
  d->oc_port_settings_changed_cb = rpi_video_port_settings_changed;
  d->oc_opaque = mc;

  omx_set_state(d, OMX_StateIdle);

  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130; 
  format.eCompressionFormat = fmt;

  format.xFramerate = 25 * (1<<16);
  if(mcp != NULL && mcp->frame_rate_num && mcp->frame_rate_den)
    format.xFramerate =(65536ULL * mcp->frame_rate_num) / mcp->frame_rate_den;

  TRACE(TRACE_DEBUG, "OMX", "Frame rate set to %2.3f",
	format.xFramerate / 65536.0f);

  omxchk(OMX_SetParameter(d->oc_handle,
			  OMX_IndexParamVideoPortFormat, &format));


  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = 130;

  if(OMX_GetParameter(d->oc_handle, OMX_IndexParamPortDefinition,
		      &portParam) == OMX_ErrorNone) {

    if(mcp != NULL) {
      portParam.format.video.nFrameWidth  = mcp->width;
      portParam.format.video.nFrameHeight = mcp->height;
    }

    OMX_SetParameter(d->oc_handle, OMX_IndexParamPortDefinition, &portParam);
  }


  OMX_CONFIG_REQUESTCALLBACKTYPE notification;
  OMX_INIT_STRUCTURE(notification);
  notification.nPortIndex = 131;
  notification.nIndex = OMX_IndexParamBrcmPixelAspectRatio;
  notification.bEnable = OMX_TRUE;
  OMX_SetParameter(d->oc_handle, OMX_IndexParamPortDefinition, &notification);

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ec;
  OMX_INIT_STRUCTURE(ec);

  ec.bStartWithValidFrame = OMX_TRUE;
  omxchk(OMX_SetParameter(d->oc_handle,
			  OMX_IndexParamBrcmVideoDecodeErrorConcealment, &ec));

  
  OMX_CONFIG_BOOLEANTYPE bt;
  OMX_INIT_STRUCTURE(bt);
  bt.bEnabled = 1;
  omxchk(OMX_SetConfig(d->oc_handle,
		       OMX_IndexParamBrcmInterpolateMissingTimestamps, &bt));
  
  omx_alloc_buffers(d, 130);
  omx_set_state(d, OMX_StateExecuting);

  if(mcp != NULL && mcp->extradata_size) {

    hts_mutex_lock(&mp->mp_mutex);
    OMX_BUFFERHEADERTYPE *buf = omx_get_buffer_locked(rvc->rvc_decoder);
    hts_mutex_unlock(&mp->mp_mutex);
    buf->nOffset = 0;
    buf->nFilledLen = mcp->extradata_size;
    memcpy(buf->pBuffer, mcp->extradata, buf->nFilledLen);
    buf->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    omxchk(OMX_EmptyThisBuffer(rvc->rvc_decoder->oc_handle, buf));
  }

  if(mc->codec_id == AV_CODEC_ID_H264) {
    h264_parser_init(&rvc->rvc_h264_parser,
		     mcp ? mcp->extradata : NULL,
		     mcp ? mcp->extradata_size : 0);
  }


  omx_enable_buffer_marks(d);

  mc->opaque = rvc;
  mc->close  = rpi_codec_close;
  mc->decode_locked = rpi_codec_decode_locked;
  mc->flush  = rpi_codec_flush;
  mc->reconfigure = rpi_codec_reconfigure;
  rvc->rvc_name = name;
  return 0;
}


/**
 *
 */
static void
rpi_codec_init(void)
{
  omx_enable_mpg2   = rpi_is_codec_enabled("MPG2");
  omx_enable_vp6    = rpi_is_codec_enabled("VP6");
  omx_enable_vp8    = rpi_is_codec_enabled("VP8");
  omx_enable_mjpeg  = rpi_is_codec_enabled("MJPG");
  omx_enable_wvc1   = rpi_is_codec_enabled("WVC1");
  omx_enable_h263   = rpi_is_codec_enabled("H263");
  omx_enable_h264   = rpi_is_codec_enabled("H264");
  omx_enable_theora = rpi_is_codec_enabled("THRA");
  omx_enable_mpeg4  = rpi_is_codec_enabled("MPG4");
}


REGISTER_CODEC(rpi_codec_init, rpi_codec_create, 100);
