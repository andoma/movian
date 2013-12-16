/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "showtime.h"
#include "video/video_decoder.h"
#include "omx.h"
#include "rpi_video.h"

static char omx_enable_mpg2;
static char omx_enable_vp8;
static char omx_enable_vp6;
static char omx_enable_mjpeg;



/**
 *
 */
static void
rpi_video_port_settings_changed(omx_component_t *oc)
{
  TRACE(TRACE_DEBUG, "VideoCore", "Video decoder output port settings changed");
  media_codec_t *mc = oc->oc_opaque;
  media_pipe_t *mp = mc->mp;
  hts_mutex_lock(&mp->mp_mutex);
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = MB_CTRL_RECONFIGURE;
  mb->mb_cw = media_codec_ref(mc);
  mb_enq(mp, &mp->mp_video, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
rpi_codec_decode(struct media_codec *mc, struct video_decoder *vd,
		 struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  rpi_video_codec_t *rvc = mc->opaque;
  const void *data = mb->mb_data;
  size_t len       = mb->mb_size;

  if(mc->codec_id == CODEC_ID_MPEG4) {

    if(mb->mb_size <= 7)
      return;

    int frame_type = 0;
    const uint8_t *d = data;
    if(d[0] == 0x00 && d[1] == 0x00 && d[2] == 0x01 && d[3] == 0xb6)
      frame_type = d[4] >> 6;

    if(frame_type == 2)
      rvc->rvc_b_frames = 100;

    if(rvc->rvc_b_frames)
      rvc->rvc_b_frames--;

    if(mb->mb_pts == PTS_UNSET && mb->mb_dts != PTS_UNSET &&
       (!rvc->rvc_b_frames || frame_type == 2)) // 2 == B frame
      mb->mb_pts = mb->mb_dts;
  }

  media_buf_meta_t *mbm = &vd->vd_reorder[vd->vd_reorder_ptr];
  *mbm = mb->mb_meta;
  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;

  while(len > 0) {
    OMX_BUFFERHEADERTYPE *buf = omx_get_buffer(rvc->rvc_decoder);
    if(buf == NULL)
      return;
    buf->nOffset = 0;
    buf->nFilledLen = MIN(len, buf->nAllocLen);
    memcpy(buf->pBuffer, data, buf->nFilledLen);
    buf->nFlags = 0;

    if(vd->vd_render_component) {
      buf->hMarkTargetComponent = vd->vd_render_component;
      buf->pMarkData = mbm;
      mbm = NULL;
    }

    if(rvc->rvc_last_epoch != mb->mb_epoch) {
      buf->nFlags |= OMX_BUFFERFLAG_DISCONTINUITY;
      rvc->rvc_last_epoch = mb->mb_epoch;
    }

    if(len <= buf->nAllocLen)
      buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    data += buf->nFilledLen;
    len  -= buf->nFilledLen;

    if(mb->mb_pts != PTS_UNSET)
      buf->nTimeStamp = omx_ticks_from_s64(mb->mb_pts);
    else {
      buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
      buf->nTimeStamp = omx_ticks_from_s64(0);
    }

    if(mb->mb_skip)
      buf->nFlags |= OMX_BUFFERFLAG_DECODEONLY;

    omxchk(OMX_EmptyThisBuffer(rvc->rvc_decoder->oc_handle, buf));
  }
  if(rvc->rvc_name_set)
    return;
  rvc->rvc_name_set = 1;
  prop_set_string(mq->mq_prop_codec, rvc->rvc_name);
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
  hts_cond_destroy(&rvc->rvc_avail_cond);
  free(rvc);
}


/**
 *
 */
static void
rpi_codec_reconfigure(struct media_codec *mc)
{
  media_pipe_t *mp = mc->mp;
  mp->mp_set_video_codec('omx', mc, mp->mp_video_frame_opaque);
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

  case CODEC_ID_H263:
    fmt = OMX_VIDEO_CodingH263;
    name = "h263 (VideoCore)";
    break;

  case CODEC_ID_MPEG4:
    fmt = OMX_VIDEO_CodingMPEG4;
    name = "MPEG-4 (VideoCore)";
    break;

  case CODEC_ID_H264:
    fmt = OMX_VIDEO_CodingAVC;
    name = "h264 (VideoCore)";
    break;

  case CODEC_ID_MPEG2VIDEO:
    if(!omx_enable_mpg2)
      return 1;
    fmt = OMX_VIDEO_CodingMPEG2;
    name = "MPEG2 (VideoCore)";
    break;

  case CODEC_ID_VP8:
    if(!omx_enable_vp8)
      return 1;
    fmt = OMX_VIDEO_CodingVP8;
    name = "VP8 (VideoCore)";
    break;

  case CODEC_ID_VP6F:
  case CODEC_ID_VP6A:
    if(!omx_enable_vp6)
      return 1;
    fmt = OMX_VIDEO_CodingVP6;
    name = "VP6 (VideoCore)";
    break;

  case CODEC_ID_MJPEG:
  case CODEC_ID_MJPEGB:
    if(!omx_enable_mjpeg)
      return 1;
    fmt = OMX_VIDEO_CodingMJPEG;
    name = "MJPEG (VideoCore)";
    break;

#if 0
  case CODEC_ID_VC1:
  case CODEC_ID_WMV3:
    if(mcp->extradata_size == 0)
      return 1;

    mc->decode = vc1_pt_decode;
    return 0;
#endif

  default:
    return 1;
  }

  rpi_video_codec_t *rvc = calloc(1, sizeof(rpi_video_codec_t));

  hts_cond_init(&rvc->rvc_avail_cond, &mp->mp_mutex);

  omx_component_t *d = omx_component_create("OMX.broadcom.video_decode",
					    &mp->mp_mutex,
					    &rvc->rvc_avail_cond);

  if(d == NULL) {
    hts_cond_destroy(&rvc->rvc_avail_cond);
    free(rvc);
    return 1;
  }

  rvc->rvc_decoder = d;
  d->oc_port_settings_changed_cb = rpi_video_port_settings_changed;
  d->oc_opaque = mc;

  omx_set_state(d, OMX_StateIdle);

  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130; 
  format.eCompressionFormat = fmt;
  omxchk(OMX_SetParameter(d->oc_handle,
			  OMX_IndexParamVideoPortFormat, &format));

  OMX_PARAM_BRCMVIDEODECODEERRORCONCEALMENTTYPE ec;
  OMX_INIT_STRUCTURE(ec);
  ec.bStartWithValidFrame = OMX_FALSE;
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

  omx_enable_buffer_marks(d);

  mc->opaque = rvc;
  mc->close  = rpi_codec_close;
  mc->decode = rpi_codec_decode;
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
  char buf[64];
  vc_gencmd(buf, sizeof(buf), "codec_enabled MPG2");
  TRACE(TRACE_INFO, "VideoCore", "%s", buf);
  omx_enable_mpg2 = !strcmp(buf, "MPG2=enabled");

  vc_gencmd(buf, sizeof(buf), "codec_enabled VP8");
  TRACE(TRACE_INFO, "VideoCore", "%s", buf);
  omx_enable_vp8 = !strcmp(buf, "VP8=enabled");

  vc_gencmd(buf, sizeof(buf), "codec_enabled VP6");
  TRACE(TRACE_INFO, "VideoCore", "%s", buf);
  omx_enable_vp6 = !strcmp(buf, "VP6=enabled");

  vc_gencmd(buf, sizeof(buf), "codec_enabled MJPG");
  TRACE(TRACE_INFO, "VideoCore", "%s", buf);
  omx_enable_mjpeg = !strcmp(buf, "MJPG=enabled");


}


REGISTER_CODEC(rpi_codec_init, rpi_codec_create, 100);
