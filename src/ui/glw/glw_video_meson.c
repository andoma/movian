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

  glw_rect_t mvd_rect;

} meson_video_display_t;


static hts_mutex_t meson_display_global_mutex;

//static meson_video_display_t *current_mvd;

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
  meson_video_display_t *mvd = gv->gv_aux;
#if 0
  hts_mutex_lock(&meson_display_global_mutex);

  if(gv->gv_activation == 0) {
    if(current_mvd != mvd) {
      if(current_mvd == NULL) {
	TRACE(TRACE_DEBUG, "GLW", "%s is primary video", gv->gv_name);
	current_mvd = mvd;
      }
    }
  } else {
    if(current_mvd == mvd) {
      TRACE(TRACE_DEBUG, "GLW", "%s is no longer primary video", gv->gv_name);
      current_mvd = NULL;
    }
  }

  hts_mutex_unlock(&meson_display_global_mutex);
#endif
  return mvd->mvd_pts;
}


/**
 *
 */
static void
mvd_reset(glw_video_t *gv)
{
  meson_video_display_t *mvd = gv->gv_aux;
#if 0
  hts_mutex_lock(&meson_display_global_mutex);
  if(current == mvd)
    current = NULL;
  hts_mutex_unlock(&meson_display_global_mutex);
#endif
  free(mvd);
}


/**
 *
 */
static void
mvd_render(glw_video_t *gv, glw_rctx_t *rc)
{
  meson_video_display_t *mvd = gv->gv_aux;

  if(gv->gv_activation >= 2)
    return;

  hts_mutex_lock(&meson_display_global_mutex);

  if(memcmp(&mvd->mvd_rect, &gv->gv_rect, sizeof(glw_rect_t))) {
    mvd->mvd_rect = gv->gv_rect;
    FILE *fp = fopen("/sys/class/video/axis", "w");
    if(fp != NULL) {
      fprintf(fp, "%d %d %d %d",
	      gv->gv_rect.x1,
	      gv->gv_rect.y1,
	      gv->gv_rect.x2,
	      gv->gv_rect.y2);
      fclose(fp);
    }
  }
  hts_mutex_unlock(&meson_display_global_mutex);
}


/**
 *
 */
static void
mvd_blackout(glw_video_t *gv)
{
}


static int mvd_set_codec(media_codec_t *mc, glw_video_t *gv);

/**
 * Meson video
 */
static glw_video_engine_t glw_video_mvd = {
  .gve_type     = 'mesn',
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
  glw_video_configure(gv, &glw_video_mvd);
  return 0;
}

