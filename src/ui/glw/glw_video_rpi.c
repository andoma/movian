/*
 *  VDPAU accelerated OpenGL video engine
 *  Copyright (C) 2010 Andreas Öman
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
 */
#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "showtime.h"
#include "glw_video_common.h"

#include "video/video_settings.h"
#include "arch/rpi/omx.h"


typedef struct rpi_video {
  omx_component_t *rv_vdecoder;
  omx_component_t *rv_vrender;
  omx_component_t *rv_vsched;

  omx_tunnel_t *rv_tun_clock_vsched;
  omx_tunnel_t *rv_tun_vdecoder_vsched;
  omx_tunnel_t *rv_tun_vsched_vrender;

} rpi_video_t;


/**
 *
 */
static int
rpi_video_init(glw_video_t *gv, rpi_video_t *rv, media_pipe_t *mp)
{

  rv->rv_vdecoder = omx_component_create("OMX.broadcom.video_decode",
					 &gv->gv_surface_mutex,
					 &gv->gv_avail_queue_cond);
  rv->rv_vrender  = omx_component_create("OMX.broadcom.video_render",
					 NULL, NULL);
  rv->rv_vsched   = omx_component_create("OMX.broadcom.video_scheduler",
					 NULL, NULL);

  if(rv->rv_vdecoder == NULL ||
     rv->rv_vrender  == NULL ||
     rv->rv_vsched   == NULL) {
    return 1;
  }

  rv->rv_tun_clock_vsched =
    omx_tunnel_create(mp->mp_extra, 81, rv->rv_vsched, 12);


  omx_set_state(rv->rv_vdecoder, OMX_StateIdle);

  OMX_VIDEO_PARAM_PORTFORMATTYPE format;
  OMX_INIT_STRUCTURE(format);
  format.nPortIndex = 130; 
  format.eCompressionFormat = OMX_VIDEO_CodingAVC;

  omxchk(OMX_SetParameter(rv->rv_vdecoder->oc_handle,
			  OMX_IndexParamVideoPortFormat, &format));


  omx_alloc_buffers(rv->rv_vdecoder, 130);
  omx_set_state(rv->rv_vdecoder, OMX_StateExecuting);
  return 0;
}


/**
 *
 */
static int64_t
rpi_video_newframe(glw_video_t *gv, video_decoder_t *vd0, int flags)
{
  int64_t pts = PTS_UNSET;
  return pts;
}



/**
 *
 */
static void
rpi_h264_reset(glw_video_t *gv)
{
  rpi_video_t *rv = gv->gv_aux;

  omx_flush_port(rv->rv_vdecoder, 130);
  omx_flush_port(rv->rv_vdecoder, 131);

  omx_flush_port(rv->rv_vsched, 10);
  omx_flush_port(rv->rv_vsched, 11);

  omx_flush_port(rv->rv_vrender, 90);

  omx_wait_buffers(rv->rv_vdecoder);

  if(rv->rv_tun_vsched_vrender != NULL) 
    omx_tunnel_destroy(rv->rv_tun_vsched_vrender);
  
  if(rv->rv_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(rv->rv_tun_vdecoder_vsched);

  omx_tunnel_destroy(rv->rv_tun_clock_vsched);

  omx_set_state(rv->rv_vrender,  OMX_StateIdle);
  omx_set_state(rv->rv_vsched,   OMX_StateIdle);
  omx_set_state(rv->rv_vdecoder, OMX_StateIdle);

  omx_release_buffers(rv->rv_vdecoder, 130);

  omx_set_state(rv->rv_vrender,  OMX_StateLoaded);
  omx_set_state(rv->rv_vsched,   OMX_StateLoaded);
  omx_set_state(rv->rv_vdecoder, OMX_StateLoaded);

  omx_component_destroy(rv->rv_vrender);
  omx_component_destroy(rv->rv_vsched);
  omx_component_destroy(rv->rv_vdecoder);
  free(rv);
}


/**
 *
 */
static int
rpi_h264_init(glw_video_t *gv)
{
  rpi_video_t *rv = calloc(1, sizeof(rpi_video_t));
  gv->gv_aux = rv;
  rpi_video_init(gv, rv, gv->gv_mp);

  
  return 0;
}

/**
 *
 */
static void
rpi_video_render(glw_video_t *gv, glw_rctx_t *rc)
{

}

static void h264_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 * Raw h264 packets
 */
static glw_video_engine_t glw_video_h264 = {
  .gve_type = 'h264',
  .gve_newframe = rpi_video_newframe,
  .gve_render = rpi_video_render,
  .gve_reset = rpi_h264_reset,
  .gve_init = rpi_h264_init,
  .gve_deliver = h264_deliver,
};

GLW_REGISTER_GVE(glw_video_h264);

/**
 *
 */
static void
h264_deliver(const frame_info_t *fi, glw_video_t *gv)
{

  if(glw_video_configure(gv, &glw_video_h264, NULL, NULL, 0, 0, 0))
    return;

  rpi_video_t *rv = gv->gv_aux;

  const void *data = fi->fi_data[0];
  size_t len       = fi->fi_pitch[0];

  while(len > 0) {
    OMX_BUFFERHEADERTYPE *buf = omx_get_buffer_locked(rv->rv_vdecoder);
    pthread_mutex_unlock(&gv->gv_surface_mutex);
    buf->nOffset = 0;
    buf->nFilledLen = MIN(len, buf->nAllocLen);
    memcpy(buf->pBuffer, data, buf->nFilledLen);
    
   
    if(len <= buf->nAllocLen) {
      buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    }

    data += buf->nFilledLen;
    len  -= buf->nFilledLen;
    buf->nTimeStamp = omx_ticks_from_s64(fi->fi_pts);

    if(fi->fi_pitch[1])
      buf->nFlags |= OMX_BUFFERFLAG_DECODEONLY;

    if(rv->rv_vdecoder->oc_port_settings_changed) {
      rv->rv_vdecoder->oc_port_settings_changed = 0;
      rv->rv_tun_vdecoder_vsched =
	omx_tunnel_create(rv->rv_vdecoder, 131, rv->rv_vsched, 10);
      omx_set_state(rv->rv_vsched,  OMX_StateExecuting);
      omx_set_state(rv->rv_vrender, OMX_StateIdle);
    }

    if(rv->rv_vsched->oc_port_settings_changed) {
      rv->rv_vsched->oc_port_settings_changed = 0;
      rv->rv_tun_vsched_vrender =
	omx_tunnel_create(rv->rv_vsched, 11, rv->rv_vrender, 90);
      omx_set_state(rv->rv_vrender, OMX_StateExecuting);



      OMX_CONFIG_DISPLAYREGIONTYPE dr;
      OMX_INIT_STRUCTURE(dr);
      dr.nPortIndex = 90;
      dr.set = OMX_DISPLAY_SET_LAYER;
      dr.layer = 3;
      omxchk(OMX_SetConfig(rv->rv_vrender->oc_handle,
			   OMX_IndexConfigDisplayRegion, &dr));
    }
    omxchk(OMX_EmptyThisBuffer(rv->rv_vdecoder->oc_handle, buf));
    pthread_mutex_lock(&gv->gv_surface_mutex);
  }  
}
