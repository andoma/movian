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

#include <CoreVideo/CVOpenGLESTextureCache.h>
#include <CoreVideo/CVPixelBuffer.h>

#define NUM_SURFACES 4

#include "video/video_decoder.h"
#include "video/video_playback.h"

typedef struct gvv_aux {
  CVOpenGLESTextureCacheRef gvv_tex_cache_ref;
} gvv_aux_t;


typedef struct reap_task {
  glw_video_reap_task_t hdr;
  GLuint tex[2];
} reap_task_t;

extern CVEAGLContext ios_get_gles_context(glw_root_t *gr);

/**
 *
 */
static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  if(t->tex[0] != 0)
    glDeleteTextures(2, t->tex);
}


/**
 *
 */
static void
surface_release_buffers(glw_video_surface_t *gvs)
{
  for(int i = 0; i < 2; i++) {
    if(gvs->gvs_data[i] != NULL) {
      CFRelease(gvs->gvs_data[i]);
      gvs->gvs_data[i] = NULL;
    }
  }
  if(gvs->gvs_opaque != NULL) {
    CFRelease(gvs->gvs_opaque);
    gvs->gvs_opaque = NULL;
  }
}


/**
 *
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  reap_task_t *t = glw_video_add_reap_task(gv, sizeof(reap_task_t), do_reap);
  t->tex[0] = gvs->gvs_texture.textures[0];
  t->tex[1] = gvs->gvs_texture.textures[1];
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
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, b->textures[1]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, b->textures[0]);
}



static const float cmatrix_ITUR_BT_601[16] = {
  1.164400,   1.164400, 1.164400, 0,
  0.000000,  -0.391800, 2.017200, 0,
  1.596000,  -0.813000, 0.000000, 0,
  -0.874190,   0.531702,-1.085616, 1
};

static const float cmatrix_ITUR_BT_709[16] = {
  1.164400,  1.164400,  1.164400, 0,
  0.000000, -0.213200,  2.112400, 0,
  1.792700, -0.532900,  0.000000, 0,
  -0.972926,  0.301453, -1.133402, 1
};

static const float cmatrix_SMPTE_240M[16] = {
  1.164400,  1.164400,  1.164400, 0,
  0.000000, -0.257800,  2.078700, 0,
  1.793900, -0.542500,  0.000000, 0,
  -0.973528,  0.328659, -1.116486, 1
};



/**
 *
 */
static void
gv_color_matrix_set(glw_video_t *gv, const struct frame_info *fi)
{
  const float *f;
  
  switch(fi->fi_color_space) {
    case COLOR_SPACE_BT_709:
      f = cmatrix_ITUR_BT_709;
      break;
      
    case COLOR_SPACE_BT_601:
      f = cmatrix_ITUR_BT_601;
      break;
      
    case COLOR_SPACE_SMPTE_240M:
      f = cmatrix_SMPTE_240M;
      break;
      
    default:
      f = fi->fi_height < 720 ? cmatrix_ITUR_BT_601 : cmatrix_ITUR_BT_709;
      break;
  }
  
  memcpy(gv->gv_cmatrix_tgt, f, sizeof(float) * 16);
}


static void
gv_color_matrix_update(glw_video_t *gv)
{
  int i;
  for(i = 0; i < 16; i++)
    gv->gv_cmatrix_cur[i] = (gv->gv_cmatrix_cur[i] * 3.0f +
                             gv->gv_cmatrix_tgt[i]) / 4.0f;
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
  gv->gv_gpa.gpa_load_uniforms = glw_video_opengl_load_uniforms;
  gv->gv_gpa.gpa_load_texture = load_texture;
  
  CVOpenGLESTextureCacheCreate(NULL, NULL, gr->gr_private, NULL, &gvv->gvv_tex_cache_ref);
  
  TRACE(TRACE_DEBUG, "GLW", "Using zero copy video renderer");
  
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
  gv_color_matrix_update(gv);

  return glw_video_newframe_blend(gv, vd, flags, &surface_release, 0);
}


/**
 *
 */
static void
upload_texture(glw_video_t *gv, glw_video_surface_t *gvs)
{
  CVReturn r;
  gvv_aux_t *gvv = gv->gv_aux;
  if(gvs->gvs_opaque == NULL)
    return;
  
  
  CVOpenGLESTextureCacheFlush(gvv->gvv_tex_cache_ref, 0);
  
  CVImageBufferRef img = gvs->gvs_opaque;
  
 
  r = CVOpenGLESTextureCacheCreateTextureFromImage(NULL,
                                                   gvv->gvv_tex_cache_ref,
                                                   img,
                                                   NULL,
                                                   GL_TEXTURE_2D, GL_LUMINANCE,
                                                   gvs->gvs_width[0], gvs->gvs_height[0],
                                                   GL_LUMINANCE, GL_UNSIGNED_BYTE, 0,
                                                   (CVOpenGLESTextureRef *)&gvs->gvs_data[0]);
  
  if(r) {
    printf("fail %d\n", r);
    return;
  }
  
  r = CVOpenGLESTextureCacheCreateTextureFromImage(NULL,
                                                   gvv->gvv_tex_cache_ref,
                                                   img,
                                                   NULL,
                                                   GL_TEXTURE_2D, GL_LUMINANCE_ALPHA,
                                                   gvs->gvs_width[1], gvs->gvs_height[1],
                                                   GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 1,
                                                   (CVOpenGLESTextureRef *)&gvs->gvs_data[1]);

  if(r) {
    printf("fail %d\n", r);
    return;
  }

  gvs->gvs_texture.gltype      = GL_TEXTURE_2D;
  gvs->gvs_texture.textures[0] = CVOpenGLESTextureGetName(gvs->gvs_data[0]);
  gvs->gvs_texture.textures[1] = CVOpenGLESTextureGetName(gvs->gvs_data[1]);
  
  glBindTexture(GL_TEXTURE_2D, gvs->gvs_texture.textures[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  
  glBindTexture(GL_TEXTURE_2D, gvs->gvs_texture.textures[1]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  
  
  
  
  gvs->gvs_texture.width  = gvs->gvs_width[0];
  gvs->gvs_texture.height = gvs->gvs_height[0];

  CFRelease(gvs->gvs_opaque);
  gvs->gvs_opaque = NULL;
}


/**
 *
 */
static void
gvv_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_video_surface_t *sa = gv->gv_sa;
  glw_video_surface_t *sb = NULL;
  glw_root_t *gr = gv->w.glw_root;
  glw_program_t *gp;
  glw_backend_root_t *gbr = &gr->gr_be;
  
  if(sa == NULL)
    return;
  
  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];
  
  upload_texture(gv, sa);
  
  glw_renderer_vtx_st(&gv->gv_quad,  0, 0, 1);
  glw_renderer_vtx_st(&gv->gv_quad,  1, 1, 1);
  glw_renderer_vtx_st(&gv->gv_quad,  2, 1, 0);
  glw_renderer_vtx_st(&gv->gv_quad,  3, 0, 0);
  
  gp = gbr->gbr_yc2rgb_1f;
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
static int
gvv_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  CVImageBufferRef img = (CVImageBufferRef)fi->fi_data[0];
  glw_video_surface_t *s;
  
  glw_video_configure(gv, gve);
  
  if((s = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return -1;
  
  gv_color_matrix_set(gv, fi);

  assert(s->gvs_opaque == NULL);
  
  CFRetain(img);
  s->gvs_opaque = img;
  s->gvs_width[0] = fi->fi_width;
  s->gvs_height[0] = fi->fi_height;
  s->gvs_width[1] = fi->fi_width >> 1;
  s->gvs_height[1] = fi->fi_height >> 1;
  s->gvs_width[2] = fi->fi_width >> 1;
  s->gvs_height[2] = fi->fi_height >> 1;
  glw_video_put_surface(gv, s, fi->fi_pts, fi->fi_epoch, fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_cvpb = {
  .gve_type = 'CVPB',
  .gve_init_on_ui_thread = 1,
  .gve_newframe = gvv_newframe,
  .gve_render   = gvv_render,
  .gve_reset    = gvv_reset,
  .gve_init     = gvv_init,
  .gve_deliver  = gvv_deliver,
};

GLW_REGISTER_GVE(glw_video_cvpb);
