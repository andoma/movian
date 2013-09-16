/*
 *  OMX video output
 *  Copyright (C) 2013 Andreas Öman
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
#include "arch/rpi/rpi_video.h"


typedef struct rpi_video_display {
  omx_component_t *rvd_vrender;
  omx_component_t *rvd_vsched;

  omx_tunnel_t *rvd_tun_clock_vsched;
  omx_tunnel_t *rvd_tun_vdecoder_vsched;
  omx_tunnel_t *rvd_tun_vsched_vrender;

  int rvd_reconfigure;
  int64_t rvd_pts;

  glw_video_t *rvd_gv;

  media_codec_t *rvd_mc; // Current media codec

} rpi_video_display_t;


/**
 *
 */
static int
rvd_init(glw_video_t *gv)
{
  rpi_video_display_t *rvd = calloc(1, sizeof(rpi_video_display_t));
  rvd->rvd_pts = PTS_UNSET;
  rvd->rvd_gv = gv;
  gv->gv_aux = rvd;
  return 0;
}


/**
 *
 */
static int64_t
rvd_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  rpi_video_display_t *rvd = gv->gv_aux;

  if(rvd->rvd_vsched && rvd->rvd_reconfigure) {
    rvd->rvd_reconfigure = 0;

    if(rvd->rvd_tun_vsched_vrender)
      omx_tunnel_destroy(rvd->rvd_tun_vsched_vrender);
    
    rvd->rvd_tun_vsched_vrender =
      omx_tunnel_create(rvd->rvd_vsched, 11, rvd->rvd_vrender, 90,
			"vsched -> vrender");

    omx_set_state(rvd->rvd_vrender, OMX_StateExecuting);


    OMX_CONFIG_DISPLAYREGIONTYPE dr;
    OMX_INIT_STRUCTURE(dr);
    dr.nPortIndex = 90;
    dr.set = OMX_DISPLAY_SET_LAYER;
    dr.layer = 3;
    omxchk(OMX_SetConfig(rvd->rvd_vrender->oc_handle,
			 OMX_IndexConfigDisplayRegion, &dr));
  }
  return rvd->rvd_pts;
}


/**
 *
 */
static void
buffer_mark(omx_component_t *oc, void *ptr)
{
  rpi_video_display_t *rvd = oc->oc_opaque;
  glw_video_t *gv = rvd->rvd_gv;
  media_pipe_t *mp = gv->gv_mp;
  video_decoder_t *vd = gv->gv_vd;
  const media_buf_meta_t *mbm = ptr;

  rvd->rvd_pts = mbm->mbm_pts;

  hts_mutex_lock(&mp->mp_mutex);
  vd->vd_reorder_current = mbm;
  hts_cond_signal(&mp->mp_video.mq_avail);
  hts_mutex_unlock(&mp->mp_mutex);
}

/**
 *
 */
static void
vsched_port_settings_changed(omx_component_t *oc)
{
  rpi_video_display_t *rvd = oc->oc_opaque;
  rvd->rvd_reconfigure = 1;
}


/**
 *
 */
static void
rvd_reset(glw_video_t *gv)
{
  rpi_video_display_t *rvd = gv->gv_aux;

  omx_tunnel_destroy(rvd->rvd_tun_clock_vsched);

  omx_flush_port(rvd->rvd_vsched, 10);
  omx_flush_port(rvd->rvd_vsched, 11);

  omx_flush_port(rvd->rvd_vrender, 90);

  if(rvd->rvd_tun_vsched_vrender != NULL) 
    omx_tunnel_destroy(rvd->rvd_tun_vsched_vrender);
  
  if(rvd->rvd_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(rvd->rvd_tun_vdecoder_vsched);


  omx_set_state(rvd->rvd_vrender,  OMX_StateIdle);
  omx_set_state(rvd->rvd_vsched,   OMX_StateIdle);

  omx_set_state(rvd->rvd_vrender,  OMX_StateLoaded);
  omx_set_state(rvd->rvd_vsched,   OMX_StateLoaded);

  omx_component_destroy(rvd->rvd_vrender);
  omx_component_destroy(rvd->rvd_vsched);

  if(rvd->rvd_mc != NULL)
    media_codec_deref(rvd->rvd_mc);

  free(rvd);
}


/**
 *
 */
static void
rvd_render(glw_video_t *gv, glw_rctx_t *rc)
{
}


/**
 *
 */
static void
rvd_blackout(glw_video_t *gv)
{
  rpi_video_display_t *rvd = gv->gv_aux;
  omx_flush_port(rvd->rvd_vsched, 10);
  omx_flush_port(rvd->rvd_vsched, 11);
  omx_flush_port(rvd->rvd_vrender, 90);
}


static int rvd_set_codec(media_codec_t *mc, glw_video_t *gv);

/**
 * Tunneled OMX
 */
static glw_video_engine_t glw_video_rvd = {
  .gve_type = 'omx',
  .gve_newframe = rvd_newframe,
  .gve_render   = rvd_render,
  .gve_reset    = rvd_reset,
  .gve_init     = rvd_init,
  .gve_set_codec= rvd_set_codec,
  .gve_blackout = rvd_blackout,
};

GLW_REGISTER_GVE(glw_video_rvd);

/**
 *
 */
static int
rvd_set_codec(media_codec_t *mc, glw_video_t *gv)
{
  media_pipe_t *mp = gv->gv_mp;

  glw_video_configure(gv, &glw_video_rvd);

  rpi_video_display_t *rvd = gv->gv_aux;
  rpi_video_codec_t *rvc = mc->opaque;
  

  if(rvd->rvd_vrender == NULL) {

    rvd->rvd_vrender = omx_component_create("OMX.broadcom.video_render",
					    NULL, NULL);
    rvd->rvd_vsched  = omx_component_create("OMX.broadcom.video_scheduler",
					    NULL, NULL);

    rvd->rvd_vsched->oc_opaque = rvd;
    rvd->rvd_vrender->oc_opaque = rvd;

    gv->gv_vd->vd_render_component = rvd->rvd_vrender;
       
    omx_enable_buffer_marks(rvd->rvd_vrender);

    rvd->rvd_tun_clock_vsched =
      omx_tunnel_create(omx_get_clock(mp), 81, rvd->rvd_vsched, 12,
			"clock -> vsched");
    
    rvd->rvd_vsched->oc_port_settings_changed_cb =
      vsched_port_settings_changed;

    rvd->rvd_vrender->oc_event_mark_cb = buffer_mark;

  }
  omx_set_state(rvd->rvd_vrender, OMX_StateIdle);

  if(rvd->rvd_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(rvd->rvd_tun_vdecoder_vsched);
      
  if(rvd->rvd_mc != NULL)
    media_codec_deref(rvd->rvd_mc);

  rvd->rvd_mc = media_codec_ref(mc);

  rvd->rvd_tun_vdecoder_vsched =
    omx_tunnel_create(rvc->rvc_decoder, 131, rvd->rvd_vsched, 10,
		      "vdecoder -> vsched");

  omx_set_state(rvd->rvd_vsched,  OMX_StateExecuting);
  return 0;
}
