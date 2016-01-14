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


typedef struct reap_task {
  glw_video_reap_task_t hdr;
  GLuint tex[3];

  int planes;

} reap_task_t;

/**
 *
 */
static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  if(t->tex[0] != 0)
    glDeleteTextures(t->planes, t->tex);
}


/**
 *
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  reap_task_t *t = glw_video_add_reap_task(gv, sizeof(reap_task_t), do_reap);
  t->planes = gv->gv_planes;

  for(int i = 0; i < gv->gv_planes; i++)
    t->tex[i] = gvs->gvs_texture.textures[i];

  av_frame_free(&gvs->gvs_frame);

  memset(gvs, 0, sizeof(glw_video_surface_t));
}


/**
 *
 */
static void
yuvp_reset(glw_video_t *gv)
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
  if(!gvs->gvs_texture.textures[0])
    glGenTextures(gv->gv_planes, gvs->gvs_texture.textures);
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
void
glw_video_opengl_load_uniforms(glw_root_t *gr, glw_program_t *gp, void *args,
                               const glw_render_job_t *rj)
{
  glw_video_t *gv = args;

  if(gp->gp_uniform_blend != -1)
    glUniform1f(gp->gp_uniform_blend, gv->gv_blend);

  if(gp->gp_uniform_colormtx != -1)
    glUniformMatrix4fv(gp->gp_uniform_colormtx, 1, GL_FALSE,
                       gv->gv_cmatrix_cur);
}


/**
 *
 */
static void
load_texture_yuv(glw_root_t *gr, glw_program_t *gp, void *args,
                 const glw_backend_texture_t *t, int num)
{
  if(num == 1) {

    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, t->textures[2]);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, t->textures[1]);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, t->textures[0]);
    glActiveTexture(GL_TEXTURE0); // We must exit with unit 0 active

  } else {
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, t->textures[2]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, t->textures[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t->textures[0]);
  }
}


/**
 *
 */
static int
yuvp_init(glw_video_t *gv)
{
  gv->gv_gpa.gpa_aux = gv;
  gv->gv_gpa.gpa_load_uniforms = glw_video_opengl_load_uniforms;
  gv->gv_gpa.gpa_load_texture = load_texture_yuv;

  gv->gv_planes = 3;

  gv->gv_tex_internal_format = GL_LUMINANCE;
  gv->gv_tex_format = GL_LUMINANCE;
  gv->gv_tex_type = GL_UNSIGNED_BYTE;
  gv->gv_tex_bytes_per_pixel = 1;

  memset(gv->gv_cmatrix_cur, 0, sizeof(float) * 16);
  make_surfaces_available(gv);
  return 0;
}


/**
 *  Texture loader
 */
static void
gv_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}


/**
 *
 */
static GLuint
gv_tex_get(glw_video_surface_t *gvs, int plane)
{
  return gvs->gvs_texture.textures[plane];
}


/**
 *
 */
static void
gv_surface_pixmap_upload(glw_video_surface_t *gvs, const glw_video_t *gv)
{
  if(gvs->gvs_uploaded)
    return;

  gvs->gvs_uploaded = 1;

  AVFrame *f = gvs->gvs_frame;

  for(int i = 0; i < gv->gv_planes; i++) {

    glBindTexture(GL_TEXTURE_2D, gv_tex_get(gvs, i));
    gv_set_tex_meta();

#ifdef GL_UNPACK_ROW_LENGTH
    glPixelStorei(GL_UNPACK_ROW_LENGTH, f->linesize[i]);
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, gv->gv_tex_internal_format,
                 f->linesize[i], gvs->gvs_height[i],
                 0, gv->gv_tex_format, gv->gv_tex_type,
                 f->data[i]);
  }

#ifdef GL_UNPACK_ROW_LENGTH
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
#endif

  gvs->gvs_tex_width = (float)gvs->gvs_width[0] / (float)f->linesize[0];

  av_frame_free(&gvs->gvs_frame);
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

  if(gvs->gvs_uploaded)
    gvs->gvs_uploaded = 0;

  av_frame_free(&gvs->gvs_frame);

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
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
static int64_t
yuvp_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  hts_mutex_assert(&gv->gv_surface_mutex);

  glw_video_surface_t *gvs;

  while((gvs = TAILQ_FIRST(&gv->gv_parked_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_parked_queue, gvs, gvs_link);
    surface_init(gv, gvs);
  }

  glw_need_refresh(gv->w.glw_root, 0);

  gv_color_matrix_update(gv);
  return glw_video_newframe_blend(gv, vd, flags, &gv_surface_pixmap_release, 1);
}


/**
 *
 */
static void
yuvp_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa = gv->gv_sa, *sb = gv->gv_sb;
  glw_program_t *gp;
  glw_backend_root_t *gbr = &gr->gr_be;
  float x;

  if(sa == NULL)
    return;

  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];

  // Upload textures

  gv_surface_pixmap_upload(sa, gv);

  const float yshift_a = (-0.5 * sa->gvs_yshift) / (float)sa->gvs_height[0];


  x = sa->gvs_tex_width;

  glw_renderer_vtx_st(&gv->gv_quad,  0, 0, 1 + yshift_a);
  glw_renderer_vtx_st(&gv->gv_quad,  1, x, 1 + yshift_a);
  glw_renderer_vtx_st(&gv->gv_quad,  2, x, 0 + yshift_a);
  glw_renderer_vtx_st(&gv->gv_quad,  3, 0, 0 + yshift_a);

  if(sb != NULL) {
    // Two pictures that should be mixed
    gv_surface_pixmap_upload(sb, gv);

    if(gv->gv_planes == 3)
      gp = gbr->gbr_yuv2rgb_2f;
    else
      gp = gbr->gbr_rgb2rgb_2f;

    const float yshift_b = (-0.5 * sb->gvs_yshift) / (float)sb->gvs_height[0];
    x = sb->gvs_tex_width;

    glw_renderer_vtx_st2(&gv->gv_quad, 0, 0, 1 + yshift_b);
    glw_renderer_vtx_st2(&gv->gv_quad, 1, x, 1 + yshift_b);
    glw_renderer_vtx_st2(&gv->gv_quad, 2, x, 0 + yshift_b);
    glw_renderer_vtx_st2(&gv->gv_quad, 3, 0, 0 + yshift_b);

  } else {

    // One picture

    if(gv->gv_planes == 3)
      gp = gbr->gbr_yuv2rgb_1f;
    else
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
yuvp_blackout(glw_video_t *gv)
{
  memset(gv->gv_cmatrix_tgt, 0, sizeof(float) * 16);
}


/**
 *
 */
static AVFrame *
make_avframe_from_frameinfo(const frame_info_t *fi, int half_y, int shift)
{
  const int h = fi->fi_height >> half_y;

  AVFrame *f = av_frame_alloc();
  f->format = AV_PIX_FMT_YUV420P;
  f->width = fi->fi_width;
  f->height = h;
  av_frame_get_buffer(f, 32);


  // Chrome linesize must be exactly half of Luma linesize or the
  // texture cordinates won't scale correctly
  f->linesize[2] = f->linesize[1] = f->linesize[0] / 2;
  assert(f->linesize[2] >= fi->fi_pitch[2]);

  const int p0 = fi->fi_pitch[0] << half_y;
  const int p1 = fi->fi_pitch[1] << half_y;
  const int p2 = fi->fi_pitch[2] << half_y;

  const void *s0 = fi->fi_data[0] + shift * fi->fi_pitch[0];
  const void *s1 = fi->fi_data[1] + shift * fi->fi_pitch[1];
  const void *s2 = fi->fi_data[2] + shift * fi->fi_pitch[2];

  for(int y = 0; y < h; y++)
    memcpy(f->data[0] + y * f->linesize[0], s0 + p0 * y, fi->fi_pitch[0]);

  for(int y = 0; y < h / 2; y++)
    memcpy(f->data[1] + y * f->linesize[1], s1 + p1 * y, fi->fi_pitch[1]);

  for(int y = 0; y < h / 2; y++)
    memcpy(f->data[2] + y * f->linesize[2], s2 + p2 * y, fi->fi_pitch[2]);

  return f;
}


/**
 *
 */
static int
yuvp_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  int hvec[3], wvec[3];
  int hshift = fi->fi_hshift, vshift = fi->fi_vshift;
  glw_video_surface_t *s;
  int64_t pts = fi->fi_pts;
  int variable_linesize = 0;
  int interlaced = fi->fi_interlaced;
#ifdef GL_UNPACK_ROW_LENGTH
  variable_linesize = 1;
#endif

  wvec[0] = fi->fi_width;
  wvec[1] = fi->fi_width >> hshift;
  wvec[2] = fi->fi_width >> hshift;
  hvec[0] = fi->fi_height >> interlaced;
  hvec[1] = fi->fi_height >> (vshift + interlaced);
  hvec[2] = fi->fi_height >> (vshift + interlaced);

  glw_video_configure(gv, gve);

  gv_color_matrix_set(gv, fi);

  if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
    return -1;


  if(!interlaced) {
    s->gvs_frame = fi->fi_avframe ? av_frame_clone(fi->fi_avframe) :
      make_avframe_from_frameinfo(fi, 0, 0);
    glw_video_put_surface(gv, s, pts, fi->fi_epoch, fi->fi_duration, 0, 0);
    return 0;
  }

  int duration = fi->fi_duration / 2;

  if(fi->fi_avframe == NULL || !variable_linesize) {
    s->gvs_frame = make_avframe_from_frameinfo(fi, 1, 0);
  } else {
    s->gvs_frame = av_frame_clone(fi->fi_avframe);
    for(int i = 0; i < 3; i++) {
      s->gvs_frame->linesize[i] *= 2;
    }
  }

  glw_video_put_surface(gv, s, pts, fi->fi_epoch, duration, 1, !fi->fi_tff);

  if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL) {
    return -1;
  }

  if(fi->fi_avframe == NULL || !variable_linesize) {
    s->gvs_frame = make_avframe_from_frameinfo(fi, 1, 1);
  } else {
    s->gvs_frame = av_frame_clone(fi->fi_avframe);
    for(int i = 0; i < 3; i++) {
      s->gvs_frame->data[i] += s->gvs_frame->linesize[i];
      s->gvs_frame->linesize[i] *= 2;
    }
  }

  if(pts != PTS_UNSET)
    pts += duration;

  glw_video_put_surface(gv, s, pts, fi->fi_epoch, duration, 1, fi->fi_tff);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_yuvp = {
  .gve_type = 'YUVP',
  .gve_newframe = yuvp_newframe,
  .gve_render   = yuvp_render,
  .gve_reset    = yuvp_reset,
  .gve_init     = yuvp_init,
  .gve_deliver  = yuvp_deliver,
  .gve_blackout = yuvp_blackout,
};

GLW_REGISTER_GVE(glw_video_yuvp);
