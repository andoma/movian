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

typedef struct meson_video_display {

  int64_t mvd_pts;

  glw_video_t *mvd_gv;

} meson_video_display_t;


/**
 *
 */
static int
mvd_init(glw_video_t *gv)
{
  meson_video_display_t *mvd = calloc(1, sizeof(meson_video_display_t));

  mvd->mvd_pts = PTS_UNSET;

  mvd->mvd_gv = gv;
  gv->gv_aux = mvd;
  return 0;
}


/**
 *
 */
static int64_t
mvd_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  return mvd->mvd_pts;
}


/**
 *
 */
static void
vsched_port_settings_changed(omx_component_t *oc)
{
  meson_video_display_t *mvd = oc->oc_opaque;
  mvd->mvd_reconfigure = 1;
}


/**
 *
 */
static void
mvd_reset(glw_video_t *gv)
{
  meson_video_display_t *mvd = gv->gv_aux;

  omx_tunnel_destroy(mvd->mvd_tun_clock_vsched);

  omx_flush_port(mvd->mvd_vsched, 10);
  omx_flush_port(mvd->mvd_vsched, 11);

  omx_flush_port(mvd->mvd_vrender, 90);

  if(mvd->mvd_tun_vsched_vrender != NULL) 
    omx_tunnel_destroy(mvd->mvd_tun_vsched_vrender);
  
  if(mvd->mvd_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(mvd->mvd_tun_vdecoder_vsched);


  omx_set_state(mvd->mvd_vrender,  OMX_StateIdle);
  omx_set_state(mvd->mvd_vsched,   OMX_StateIdle);

  omx_set_state(mvd->mvd_vrender,  OMX_StateLoaded);
  omx_set_state(mvd->mvd_vsched,   OMX_StateLoaded);

  omx_component_destroy(mvd->mvd_vrender);
  omx_component_destroy(mvd->mvd_vsched);

  if(mvd->mvd_mc != NULL)
    media_codec_deref(mvd->mvd_mc);

  free(mvd);
}


/**
 *
 */
static void
mvd_render(glw_video_t *gv, glw_rctx_t *rc)
{
}


/**
 *
 */
static void
mvd_blackout(glw_video_t *gv)
{
  meson_video_display_t *mvd = gv->gv_aux;
  omx_flush_port(mvd->mvd_vsched, 10);
  omx_flush_port(mvd->mvd_vsched, 11);
  omx_flush_port(mvd->mvd_vrender, 90);
}


static int mvd_set_codec(media_codec_t *mc, glw_video_t *gv);

/**
 * Tunneled OMX
 */
static glw_video_engine_t glw_video_mvd = {
  .gve_type = 'omx',
  .gve_newframe = mvd_newframe,
  .gve_render   = mvd_render,
  .gve_reset    = mvd_reset,
  .gve_init     = mvd_init,
  .gve_set_codec= mvd_set_codec,
  .gve_blackout = mvd_blackout,
};

GLW_REGISTER_GVE(glw_video_mvd);

/**
 *
 */
static int
mvd_set_codec(media_codec_t *mc, glw_video_t *gv)
{
  media_pipe_t *mp = gv->gv_mp;

  glw_video_configure(gv, &glw_video_mvd);

  meson_video_display_t *mvd = gv->gv_aux;
  meson_video_codec_t *rvc = mc->opaque;
  

  if(mvd->mvd_vrender == NULL) {

    mvd->mvd_vrender = omx_component_create("OMX.broadcom.video_render",
					    NULL, NULL);
    mvd->mvd_vsched  = omx_component_create("OMX.broadcom.video_scheduler",
					    NULL, NULL);

    mvd->mvd_vsched->oc_opaque = mvd;
    mvd->mvd_vrender->oc_opaque = mvd;

    gv->gv_vd->vd_render_component = mvd->mvd_vrender;
       
    omx_enable_buffer_marks(mvd->mvd_vrender);

    mvd->mvd_tun_clock_vsched =
      omx_tunnel_create(omx_get_clock(mp), 81, mvd->mvd_vsched, 12,
			"clock -> vsched");
    
    mvd->mvd_vsched->oc_port_settings_changed_cb =
      vsched_port_settings_changed;

    mvd->mvd_vrender->oc_event_mark_cb = buffer_mark;

  }
  omx_set_state(mvd->mvd_vrender, OMX_StateIdle);

  if(mvd->mvd_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(mvd->mvd_tun_vdecoder_vsched);
      
  if(mvd->mvd_mc != NULL)
    media_codec_deref(mvd->mvd_mc);

  mvd->mvd_mc = media_codec_ref(mc);

  mvd->mvd_tun_vdecoder_vsched =
    omx_tunnel_create(rvc->rvc_decoder, 131, mvd->mvd_vsched, 10,
		      "vdecoder -> vsched");

  omx_set_state(mvd->mvd_vsched,  OMX_StateExecuting);
  return 0;
}

