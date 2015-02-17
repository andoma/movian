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
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "main.h"
#include "glw_video_common.h"

#define NUM_SURFACES 4

#include "video/video_decoder.h"
#include "video/video_playback.h"


/**
 *
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  if(gvs->gvs_ref_aux != NULL) {
    gvs->gvs_ref_release(gvs->gvs_ref_aux);
    gvs->gvs_ref_aux = NULL;
  }

  memset(gvs, 0, sizeof(glw_video_surface_t));
}


/**
 *
 */
static void
tex_reset(glw_video_t *gv)
{
  for(int i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(gv, &gv->gv_surfaces[i]);
}


/**
 *
 */
static void
surface_init(glw_video_t *gv, glw_video_surface_t *gvs)
{
  gvs->gvs_uploaded = 0;
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
}

/**
 *
 */
static void
make_surfaces_available(glw_video_t *gv)
{
  for(int i = 0; i < NUM_SURFACES; i++) {
    glw_video_surface_t *gvs = &gv->gv_surfaces[i];
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  }
}


/**
 *
 */
static int
tex_init(glw_video_t *gv)
{
  gv->gv_gpa.gpa_aux = gv;
  gv->gv_gpa.gpa_load_uniforms = glw_video_opengl_load_uniforms;
  gv->gv_gpa.gpa_load_texture = NULL;

  gv->gv_planes = 1;

  memset(gv->gv_cmatrix_cur, 0, sizeof(float) * 16);
  make_surfaces_available(gv);
  return 0;
}


/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  struct glw_video_surface_queue *fromqueue)
{
  assert(gvs != gv->gv_sa);
  assert(gvs != gv->gv_sb);

  TAILQ_REMOVE(fromqueue, gvs, gvs_link);

  if(gvs->gvs_ref_aux != NULL) {
    gvs->gvs_ref_release(gvs->gvs_ref_aux);
    gvs->gvs_ref_aux = NULL;
  }

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
}


/**
 *
 */
static int64_t
tex_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  hts_mutex_assert(&gv->gv_surface_mutex);

  glw_video_surface_t *gvs;

  while((gvs = TAILQ_FIRST(&gv->gv_parked_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_parked_queue, gvs, gvs_link);
    surface_init(gv, gvs);
  }

  glw_need_refresh(gv->w.glw_root, 0);

  return glw_video_newframe_blend(gv, vd, flags, &gv_surface_pixmap_release, 1);
}


/**
 *
 */
static void
tex_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa = gv->gv_sa, *sb = gv->gv_sb;
  glw_program_t *gp;
  glw_backend_root_t *gbr = &gr->gr_be;

  if(sa == NULL)
    return;

  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];

  glw_renderer_vtx_st(&gv->gv_quad,  0, 0, 1);
  glw_renderer_vtx_st(&gv->gv_quad,  1, 1, 1);
  glw_renderer_vtx_st(&gv->gv_quad,  2, 1, 0);
  glw_renderer_vtx_st(&gv->gv_quad,  3, 0, 0);

  if(sb != NULL) {
    // Two pictures that should be mixed

    gp = gbr->gbr_rgb2rgb_2f;

    glw_renderer_vtx_st2(&gv->gv_quad, 0, 0, 1);
    glw_renderer_vtx_st2(&gv->gv_quad, 1, 1, 1);
    glw_renderer_vtx_st2(&gv->gv_quad, 2, 1, 0);
    glw_renderer_vtx_st2(&gv->gv_quad, 3, 0, 0);

  } else {

    // One picture

    gp = gbr->gbr_rgb2rgb_1f;
  }

  gv->gv_gpa.gpa_prog = gp;

  glw_renderer_draw(&gv->gv_quad, gr, rc,
                    &sa->gvs_texture,
                    sb != NULL ? &sb->gvs_texture : NULL,
                    NULL, NULL,
                    rc->rc_alpha * gv->w.glw_alpha, 0, &gv->gv_gpa);
}


/**
 *
 */
static void
tex_blackout(glw_video_t *gv)
{
  memset(gv->gv_cmatrix_tgt, 0, sizeof(float) * 16);
}


/**
 *
 */
static int
tex_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  glw_video_surface_t *s;

  glw_video_configure(gv, gve);

  if((s = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return -1;

  s->gvs_ref_aux = fi->fi_ref_aux;
  s->gvs_ref_release = fi->fi_ref_release;
  s->gvs_texture.gltype      = fi->fi_u32[0];
  s->gvs_texture.textures[0] = fi->fi_u32[1];
  s->gvs_texture.width       = fi->fi_width;
  s->gvs_texture.height      = fi->fi_height;
  s->gvs_width[0]            = fi->fi_width;
  s->gvs_height[0]           = fi->fi_height;

  glw_video_put_surface(gv, s, fi->fi_pts, fi->fi_epoch, fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_tex = {
  .gve_type = 'tex',
  .gve_newframe = tex_newframe,
  .gve_render   = tex_render,
  .gve_reset    = tex_reset,
  .gve_init     = tex_init,
  .gve_deliver  = tex_deliver,
  .gve_blackout = tex_blackout,
};

GLW_REGISTER_GVE(glw_video_tex);
