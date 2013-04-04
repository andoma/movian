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

omx_component_t *the_renderer;


typedef struct rpi_video_display {
  omx_component_t *rvd_vrender;
  omx_component_t *rvd_vsched;

  omx_tunnel_t *rvd_tun_clock_vsched;
  omx_tunnel_t *rvd_tun_vdecoder_vsched;
  omx_tunnel_t *rvd_tun_vsched_vrender;

  int64_t rvd_delta;
  int rvd_epoch;

} rpi_video_display_t;


/**
 *
 */
static int
rvd_init(glw_video_t *gv)
{
  rpi_video_display_t *rvd = calloc(1, sizeof(rpi_video_display_t));
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

  int64_t pts = omx_get_media_time(gv->gv_mp->mp_extra);

  if(rvd->rvd_vsched && rvd->rvd_vsched->oc_port_settings_changed) {
    rvd->rvd_vsched->oc_port_settings_changed = 0;
    rvd->rvd_tun_vsched_vrender =
      omx_tunnel_create(rvd->rvd_vsched, 11, rvd->rvd_vrender, 90);
    omx_set_state(rvd->rvd_vrender, OMX_StateExecuting);


    OMX_CONFIG_DISPLAYREGIONTYPE dr;
    OMX_INIT_STRUCTURE(dr);
    dr.nPortIndex = 90;
    dr.set = OMX_DISPLAY_SET_LAYER;
    dr.layer = 3;
    omxchk(OMX_SetConfig(rvd->rvd_vrender->oc_handle,
			 OMX_IndexConfigDisplayRegion, &dr));
  }

  video_decoder_set_current_time(gv->gv_vd, pts,
				 rvd->rvd_epoch, rvd->rvd_delta);

  return pts;
}



/**
 *
 */
static void
rvd_reset(glw_video_t *gv)
{
  rpi_video_display_t *rvd = gv->gv_aux;

  omx_flush_port(rvd->rvd_vsched, 10);
  omx_flush_port(rvd->rvd_vsched, 11);

  omx_flush_port(rvd->rvd_vrender, 90);

  if(rvd->rvd_tun_vsched_vrender != NULL) 
    omx_tunnel_destroy(rvd->rvd_tun_vsched_vrender);
  
  if(rvd->rvd_tun_vdecoder_vsched != NULL)
    omx_tunnel_destroy(rvd->rvd_tun_vdecoder_vsched);

  omx_tunnel_destroy(rvd->rvd_tun_clock_vsched);

  omx_set_state(rvd->rvd_vrender,  OMX_StateIdle);
  omx_set_state(rvd->rvd_vsched,   OMX_StateIdle);

  omx_set_state(rvd->rvd_vrender,  OMX_StateLoaded);
  omx_set_state(rvd->rvd_vsched,   OMX_StateLoaded);

  omx_component_destroy(rvd->rvd_vrender);
  omx_component_destroy(rvd->rvd_vsched);
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
  printf("Flusing video display\n");

  omx_flush_port(rvd->rvd_vsched, 10);
  omx_flush_port(rvd->rvd_vsched, 11);
  omx_flush_port(rvd->rvd_vrender, 90);
}


static void rvd_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 * Tunneled OMX
 */
static glw_video_engine_t glw_video_rvd = {
  .gve_type = 'omx',
  .gve_newframe = rvd_newframe,
  .gve_render   = rvd_render,
  .gve_reset    = rvd_reset,
  .gve_init     = rvd_init,
  .gve_deliver  = rvd_deliver,
  .gve_blackout = rvd_blackout,
};

GLW_REGISTER_GVE(glw_video_rvd);

/**
 *
 */
static void
rvd_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  media_pipe_t *mp = gv->gv_mp;

  glw_video_configure(gv, &glw_video_rvd);

  rpi_video_display_t *rvd = gv->gv_aux;

  if(fi->fi_data[0]) {

    rvd->rvd_vrender = omx_component_create("OMX.broadcom.video_render",
					    NULL, NULL);
    rvd->rvd_vsched  = omx_component_create("OMX.broadcom.video_scheduler",
					    NULL, NULL);
    rvd->rvd_tun_clock_vsched =
      omx_tunnel_create(mp->mp_extra, 81, rvd->rvd_vsched, 12);
    
    rvd->rvd_tun_vdecoder_vsched =
      omx_tunnel_create((void *)fi->fi_data[0], 131, rvd->rvd_vsched, 10);
    omx_set_state(rvd->rvd_vsched,  OMX_StateExecuting);
    omx_set_state(rvd->rvd_vrender, OMX_StateIdle);
  }

  if(fi->fi_drive_clock) {
    rvd->rvd_epoch = fi->fi_epoch;
    rvd->rvd_delta = fi->fi_delta;
  }
}
