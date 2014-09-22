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

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "glw_video_common.h"

#include <CoreVideo/CVOpenGLTextureCache.h>

#define NUM_SURFACES 4

#include "video/video_decoder.h"
#include "video/video_playback.h"
#include "arch/osx/osx_c.h"

typedef struct gvv_aux {
  CVOpenGLTextureCacheRef gvv_tex_cache_ref;
} gvv_aux_t;


typedef struct reap_task {
  glw_video_reap_task_t hdr;
  GLuint tex;
} reap_task_t;


/**
 *
 */
static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  if(t->tex != 0)
    glDeleteTextures(1, &t->tex);
}


/**
 *
 */
static void
surface_release_buffers(glw_video_surface_t *gvs)
{
  if(gvs->gvs_data[0] != NULL) {
    CVOpenGLTextureRelease(gvs->gvs_data[0]);
    gvs->gvs_data[0] = NULL;
  }

  if(gvs->gvs_data[1] != NULL) {
    CVOpenGLTextureRelease(gvs->gvs_data[1]);
    gvs->gvs_data[1] = NULL;
  }

}

/**
 *
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  reap_task_t *t = glw_video_add_reap_task(gv, sizeof(reap_task_t), do_reap);
  t->tex = gvs->gvs_texture.textures[0];
  surface_release_buffers(gvs);
}


/**
 *
 */
static void
gvv_reset(glw_video_t *gv)
{
  for(int i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(gv, &gv->gv_surfaces[i]);

  gvv_aux_t *gvv = gv->gv_aux;

  if(gvv == NULL)
    return;

  gv->gv_aux = NULL;

  CFRelease(gvv->gvv_tex_cache_ref);
  free(gvv);
}


/**
 *
 */
static void
surface_init(glw_video_t *gv, glw_video_surface_t *gvs)
{
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
static void
surface_release(glw_video_t *gv, glw_video_surface_t *gvs,
                struct glw_video_surface_queue *fromqueue)
{
  assert(gvs != gv->gv_sa);
  assert(gvs != gv->gv_sb);

  surface_release_buffers(gvs);

  TAILQ_REMOVE(fromqueue, gvs, gvs_link);

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
}


static void
load_texture(glw_root_t *gr, glw_program_t *gp, void *aux,
             const glw_backend_texture_t *b, int num)

{
  glEnable(b->gltype);
  glBindTexture(b->gltype, b->textures[0]);
}


/**
 *
 */
static int
gvv_init(glw_video_t *gv)
{
  glw_root_t *gr = gv->w.glw_root;

  gvv_aux_t *gvv = calloc(1, sizeof(gvv_aux_t));
  gv->gv_aux = gvv;

  gv->gv_gpa.gpa_aux = gv;
  gv->gv_gpa.gpa_load_texture = load_texture;

  CVOpenGLTextureCacheCreate(NULL, NULL,
                             osx_get_cgl_context(gr),
                             osx_get_cgl_pixel_format(gr),
                             0, &gvv->gvv_tex_cache_ref);

  TRACE(TRACE_DEBUG, "VDA", "Using zero copy video renderer");

  make_surfaces_available(gv);
  return 0;
}



/**
 *
 */
static int64_t
gvv_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  hts_mutex_assert(&gv->gv_surface_mutex);

  glw_video_surface_t *gvs;

  while((gvs = TAILQ_FIRST(&gv->gv_parked_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_parked_queue, gvs, gvs_link);
    surface_init(gv, gvs);
  }

  glw_need_refresh(gv->w.glw_root, 0);

  return glw_video_newframe_blend(gv, vd, flags, &surface_release, 0);
}


/**
 *
 */
static void
upload_texture(glw_video_t *gv, glw_video_surface_t *gvs)
{
  gvv_aux_t *gvv = gv->gv_aux;
  if(gvs->gvs_data[0] == NULL)
    return;

  assert(gvs->gvs_data[1] == NULL);

  CVOpenGLTextureCacheFlush(gvv->gvv_tex_cache_ref, 0);

  CVImageBufferRef img = gvs->gvs_data[0];

  CVReturn r =
    CVOpenGLTextureCacheCreateTextureFromImage(NULL,
                                               gvv->gvv_tex_cache_ref,
                                               img,
                                               0,
                                               (CVOpenGLTextureRef *)&gvs->gvs_data[1]);


  if(r)
    return;

  CVOpenGLTextureRelease(gvs->gvs_data[0]);
  gvs->gvs_data[0] = NULL;

  gvs->gvs_texture.gltype      = CVOpenGLTextureGetTarget(gvs->gvs_data[1]);
  gvs->gvs_texture.textures[0] = CVOpenGLTextureGetName(gvs->gvs_data[1]);
  gvs->gvs_texture.width = gvs->gvs_width[0];
  gvs->gvs_texture.height = gvs->gvs_height[0];
}


/**
 *
 */
static void
gvv_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_video_surface_t *sa = gv->gv_sa;

  if(sa == NULL)
    return;

  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];

  upload_texture(gv, sa);

  glw_renderer_vtx_st(&gv->gv_quad,  0, 0,            gv->gv_height);
  glw_renderer_vtx_st(&gv->gv_quad,  1, gv->gv_width, gv->gv_height);
  glw_renderer_vtx_st(&gv->gv_quad,  2, gv->gv_width, 0);
  glw_renderer_vtx_st(&gv->gv_quad,  3, 0,            0);

  glw_renderer_draw(&gv->gv_quad, gv->w.glw_root, rc,
                    &sa->gvs_texture,
                    NULL, NULL, NULL,
                    rc->rc_alpha * gv->w.glw_alpha, 0, &gv->gv_gpa);
}

/**
 *
 */
static int
gvv_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  CVImageBufferRef img = (CVImageBufferRef)fi->fi_data[0];
  glw_video_surface_t *s;

  glw_video_configure(gv, gve);

  if((s = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return -1;

  assert(s->gvs_data[0] == NULL);

  CVOpenGLTextureRetain(img);
  s->gvs_data[0] = img;
  s->gvs_width[0] = fi->fi_width;
  s->gvs_height[0] = fi->fi_height;
  glw_video_put_surface(gv, s, fi->fi_pts, fi->fi_epoch, fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_vda = {
  .gve_type = 'VDA',
  .gve_init_on_ui_thread = 1,
  .gve_newframe = gvv_newframe,
  .gve_render   = gvv_render,
  .gve_reset    = gvv_reset,
  .gve_init     = gvv_init,
  .gve_deliver  = gvv_deliver,
};

GLW_REGISTER_GVE(glw_video_vda);
