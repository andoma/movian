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
  t->tex = gvs->gvs_textures[0];
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


/**
 *
 */
static int
gvv_init(glw_video_t *gv)
{
  glw_root_t *gr = gv->w.glw_root;

  gvv_aux_t *gvv = calloc(1, sizeof(gvv_aux_t));
  gv->gv_aux = gvv;

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

  return glw_video_newframe_blend(gv, vd, flags, &surface_release);
}


const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};



/**
 *  Video widget render
 */
static void
render_video_quad(glw_video_t *gv, glw_video_surface_t *gvs, glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &gv->w.glw_root->gr_be;
  int type = CVOpenGLTextureGetTarget(gvs->gvs_data[1]);

  glEnable(type);
  glBindTexture(type, CVOpenGLTextureGetName(gvs->gvs_data[1]));

  glw_load_program(gbr, NULL);

  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(projection);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(glw_mtx_get(rc->rc_mtx));

  glBegin(GL_QUADS);
  glTexCoord2i(0, gvs->gvs_height[0]);
  glVertex3i(-1, -1, 0);

  glTexCoord2i(gvs->gvs_width[0], gvs->gvs_height[0]);
  glVertex3i(1, -1, 0);

  glTexCoord2i(gvs->gvs_width[0], 0);
  glVertex3i(1, 1, 0);

  glTexCoord2i(0, 0);
  glVertex3i(-1, 1, 0);

  glEnd();

  glDisable(type);
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

  if(rc->rc_alpha > 0.98f)
    glDisable(GL_BLEND);
  else
    glEnable(GL_BLEND);

  render_video_quad(gv, sa, rc);

  glEnable(GL_BLEND);
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
