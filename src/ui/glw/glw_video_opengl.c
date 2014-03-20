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

#define GVF_TEX_L   0
#define GVF_TEX_Cr  1
#define GVF_TEX_Cb  2


#define LINESIZE(w, c) \
  (((w) * (c) + PIXMAP_ROW_ALIGN - 1) & ~(PIXMAP_ROW_ALIGN - 1))

#define PBO_RELEASE_BEFORE_MAP

#define NUM_SURFACES 4

#include "video/video_decoder.h"
#include "video/video_playback.h"


typedef struct reap_task {
  glw_video_reap_task_t hdr;
  
  GLuint pbo[3];
  GLuint tex[3];

  int planes;

} reap_task_t;

/**
 *
 */
static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  for(int i = 0; i < t->planes; i++) {
    if(t->pbo[i] != 0) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, t->pbo[i]);
      glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }
  if(t->pbo[0] != 0)
    glDeleteBuffers(t->planes, t->pbo);

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

  for(int i = 0; i < gv->gv_planes; i++) {
    t->pbo[i] = gvs->gvs_pbo[i];
    t->tex[i] = gvs->gvs_textures[i];
    gvs->gvs_pbo[i] = 0;
    gvs->gvs_textures[i] = 0;
    gvs->gvs_data[i] = NULL;
  }
}


/**
 *
 */
static void
video_opengl_reset(glw_video_t *gv)
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
  int i;

  if(gvs->gvs_pbo[0])
    glDeleteBuffers(gv->gv_planes, gvs->gvs_pbo);

  glGenBuffers(gv->gv_planes, gvs->gvs_pbo);

  if(!gvs->gvs_textures[0])
    glGenTextures(gv->gv_planes, gvs->gvs_textures);

  gvs->gvs_uploaded = 0;
  for(i = 0; i < gv->gv_planes; i++) {

    int linesize = LINESIZE(gvs->gvs_width[i], gv->gv_tex_bytes_per_pixel);

    gvs->gvs_size[i] = linesize * gvs->gvs_height[i];
    assert(gvs->gvs_size[i] > 0);

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gvs->gvs_pbo[i]);
    glBufferData(GL_PIXEL_UNPACK_BUFFER,gvs->gvs_size[i], NULL, GL_STREAM_DRAW);
    gvs->gvs_data[i] = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    assert(gvs->gvs_data[i] != NULL);
  }
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
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
yuvp_init(glw_video_t *gv)
{
  gv->gv_planes = 3;

  gv->gv_tex_internal_format = 1;
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
gv_set_tex_meta(int textype)
{
  glTexParameteri(textype, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(textype, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(textype, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(textype, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}


/**
 *
 */
static GLuint 
gv_tex_get(glw_video_surface_t *gvs, int plane)
{
  return gvs->gvs_textures[plane];
}


/**
 *
 */
static void
gv_surface_pixmap_upload(glw_video_surface_t *gvs, int textype,
                         const glw_video_t *gv)
{
  if(gvs->gvs_uploaded || gvs->gvs_pbo[0] == 0)
    return;

  gvs->gvs_uploaded = 1;

  for(int i = 0; i < gv->gv_planes; i++) {
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gvs->gvs_pbo[i]);
    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    glBindTexture(textype, gv_tex_get(gvs, i));
    gv_set_tex_meta(textype);
    glTexImage2D(textype, 0, gv->gv_tex_internal_format,
                 gvs->gvs_width[i], gvs->gvs_height[i],
                 0, gv->gv_tex_format, gv->gv_tex_type, NULL);
    gvs->gvs_data[i] = NULL;
  }

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}


/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  struct glw_video_surface_queue *fromqueue)
{
  int i;

  assert(gvs != gv->gv_sa);
  assert(gvs != gv->gv_sb);

  TAILQ_REMOVE(fromqueue, gvs, gvs_link);

  if(gvs->gvs_uploaded) {
    gvs->gvs_uploaded = 0;

    for(i = 0; i < gv->gv_planes; i++) {
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, gvs->gvs_pbo[i]);

      // Setting the buffer to NULL tells the GPU it can assign
      // us another piece of memory as backing store.
#ifdef PBO_RELEASE_BEFORE_MAP
      glBufferData(GL_PIXEL_UNPACK_BUFFER, gvs->gvs_size[i],
		   NULL, GL_STREAM_DRAW);
#endif

      gvs->gvs_data[i] = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
      assert(gvs->gvs_data[i] != NULL);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  }

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
video_opengl_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  hts_mutex_assert(&gv->gv_surface_mutex);

  glw_video_surface_t *gvs;

  while((gvs = TAILQ_FIRST(&gv->gv_parked_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_parked_queue, gvs, gvs_link);
    surface_init(gv, gvs);
  }

  glw_need_refresh(gv->w.glw_root, 0);

  gv_color_matrix_update(gv);
  return glw_video_newframe_blend(gv, vd, flags, &gv_surface_pixmap_release);
}


/**
 *  Video widget render
 */
static void
render_video_quad(int interlace, int rectmode, int width, int height,
		  int bob1, int bob2,
		  glw_backend_root_t *gbr, glw_program_t *gp,
		  const glw_video_t *gv, glw_rctx_t *rc)
{
  float x1, x2, y1, y2;
  float b1 = 0, b2 = 0;

  static const float vertices[12] = {-1, -1, 1, -1, 1, 1, -1, 1};
  const uint8_t elements[6] = {0,1,2,0,2,3};
  float tc[12];

  x1 = 0;
  y1 = 0;

  if(rectmode) {

    x2 = width;
    y2 = height;

  } else {

    x2 = 1;
    y2 = 1;

    if(interlace) {

      b1 = (0.5 * bob1) / (float)height;
      b2 = (0.5 * bob2) / (float)height;
    }
  }

  tc[0] = x1;
  tc[1] = y2 - b1;
  tc[2] = y2 - b2;

  tc[3] = x2;
  tc[4] = y2 - b1;
  tc[5] = y2 - b2;

  tc[6] = x2;
  tc[7] = y1 - b1;
  tc[8] = y1 - b2;

  tc[9] = x1;
  tc[10] = y1 - b1;
  tc[11] = y1 - b2;

  glw_load_program(gbr, gp);
  glw_program_set_uniform_color(gbr, 1, 1, 1, rc->rc_alpha);
  glw_program_set_modelview(gbr, rc);
  if(gp->gp_uniform_blend != -1)
    glUniform1f(gp->gp_uniform_blend, gv->gv_blend);

  if(gp->gp_uniform_colormtx != -1)
    glUniformMatrix4fv(gp->gp_uniform_colormtx, 1, GL_FALSE,
                       gv->gv_cmatrix_cur);

  glVertexAttribPointer(gp->gp_attribute_texcoord, 3, GL_FLOAT, 0, 0, tc);
  glVertexAttribPointer(gp->gp_attribute_position, 2, GL_FLOAT, 0, 0, vertices);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, elements);
}


/**
 *
 */
static void
render_video_1f(const glw_video_t *gv, glw_video_surface_t *s,
		int textype, int rectmode, glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &gv->w.glw_root->gr_be;
  glw_program_t *gp;

  gv_surface_pixmap_upload(s, textype, gv);

  switch(gv->gv_planes) {
  case 3:
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(textype, gv_tex_get(s, GVF_TEX_Cb));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(textype, gv_tex_get(s, GVF_TEX_Cr));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(textype, gv_tex_get(s, GVF_TEX_L));
    gp = gbr->gbr_yuv2rgb_1f;
    break;

  case 1:
    glBindTexture(textype, gv_tex_get(s, 0));
    gp = gbr->gbr_rgb2rgb_1f;
    break;

  default:
    return;
  }

  render_video_quad(s->gvs_interlaced, rectmode,
		    s->gvs_width[0], s->gvs_height[0],
		    s->gvs_yshift, 0, gbr, gp, gv, rc);
}


/**
 *
 */
static void
render_video_2f(const glw_video_t *gv, 
		glw_video_surface_t *sa, glw_video_surface_t *sb,
		int textype, int rectmode, glw_rctx_t *rc)
{
  glw_backend_root_t *gbr = &gv->w.glw_root->gr_be;
  glw_program_t *gp;

  gv_surface_pixmap_upload(sa, textype, gv);
  gv_surface_pixmap_upload(sb, textype, gv);

  switch(gv->gv_planes) {
  case 3:
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(textype, gv_tex_get(sb, GVF_TEX_Cb));

    glActiveTexture(GL_TEXTURE4);
    glBindTexture(textype, gv_tex_get(sb, GVF_TEX_Cr));

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(textype, gv_tex_get(sb, GVF_TEX_L));

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(textype, gv_tex_get(sa, GVF_TEX_Cb));

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(textype, gv_tex_get(sa, GVF_TEX_Cr));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(textype, gv_tex_get(sa, GVF_TEX_L));
    gp = gbr->gbr_yuv2rgb_2f;
    break;

  case 1:
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(textype, gv_tex_get(sb, 0));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(textype, gv_tex_get(sa, 0));
    gp = gbr->gbr_rgb2rgb_2f;
    break;

  default:
    return;
  }

  render_video_quad(sa->gvs_interlaced || sb->gvs_interlaced, rectmode, 
		    sa->gvs_width[0], sa->gvs_height[0],
		    sa->gvs_yshift, sb->gvs_yshift,
		    gbr, gp, gv, rc);
}


/**
 *
 */
static void
video_opengl_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_root_t *gr = gv->w.glw_root;
  int textype = gr->gr_be.gbr_primary_texture_mode;
  int rectmode = !gr->gr_normalized_texture_coords;
  glw_video_surface_t *sa = gv->gv_sa, *sb = gv->gv_sb;

  if(sa == NULL)
    return;

  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];

  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  
  if(sb != NULL) {
    render_video_2f(gv, sa, sb, textype, rectmode, rc);
  } else {
    render_video_1f(gv, sa, textype, rectmode, rc);
  }

  glEnable(GL_BLEND); 
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
static int
yuvp_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  int hvec[3], wvec[3];
  int i, h, w;
  const uint8_t *src;
  uint8_t *dst;
  int tff;
  int hshift = fi->fi_hshift, vshift = fi->fi_vshift;
  glw_video_surface_t *s;
  const int parity = 0;
  int64_t pts = fi->fi_pts;

  wvec[0] = fi->fi_width;
  wvec[1] = fi->fi_width >> hshift;
  wvec[2] = fi->fi_width >> hshift;
  hvec[0] = fi->fi_height >> fi->fi_interlaced;
  hvec[1] = fi->fi_height >> (vshift + fi->fi_interlaced);
  hvec[2] = fi->fi_height >> (vshift + fi->fi_interlaced);

  glw_video_configure(gv, gve);

  gv_color_matrix_set(gv, fi);

  if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
    return -1;

  if(!fi->fi_interlaced) {

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      src = fi->fi_data[i];
      dst = s->gvs_data[i];
      assert(dst != NULL);

      const int linesize = LINESIZE(w, 1);

      while(h--) {
	memcpy(dst, src, w);
	dst += linesize;
	src += fi->fi_pitch[i];
      }
    }

    glw_video_put_surface(gv, s, pts, fi->fi_epoch, fi->fi_duration, 0, 0);

  } else {

    int duration = fi->fi_duration >> 1;

    tff = fi->fi_tff ^ parity;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = fi->fi_data[i]; 
      dst = s->gvs_data[i];
      const int linesize = LINESIZE(w, 1);
      while(h--) {
	memcpy(dst, src, w);
	dst += linesize;
	src += fi->fi_pitch[i] * 2;
      }
    }
    glw_video_put_surface(gv, s, pts, fi->fi_epoch, duration, 1, !tff);

    if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
      return -1;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = fi->fi_data[i] + fi->fi_pitch[i];
      dst = s->gvs_data[i];
      const int linesize = LINESIZE(w, 1);
      while(h--) {
	memcpy(dst, src, w);
	dst += linesize;
	src += fi->fi_pitch[i] * 2;
      }
    }

    if(pts != PTS_UNSET)
      pts += duration;

    glw_video_put_surface(gv, s, pts, fi->fi_epoch, duration, 1, tff);
  }
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_opengl = {
  .gve_type = 'YUVP',
  .gve_newframe = video_opengl_newframe,
  .gve_render   = video_opengl_render,
  .gve_reset    = video_opengl_reset,
  .gve_init     = yuvp_init,
  .gve_deliver  = yuvp_deliver,
  .gve_blackout = yuvp_blackout,
};

GLW_REGISTER_GVE(glw_video_opengl);


/**
 *
 */
static int
bgr_init(glw_video_t *gv)
{
  gv->gv_planes = 1;

  gv->gv_tex_internal_format = GL_RGBA8;
  gv->gv_tex_format = GL_BGR;
  gv->gv_tex_type = GL_UNSIGNED_BYTE;
  gv->gv_tex_bytes_per_pixel = 3;

  make_surfaces_available(gv);
  return 0;
}


/**
 *
 */
static int
bgr_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  glw_video_surface_t *s;
  int64_t pts = fi->fi_pts;
  int wvec[3] = {0};
  int hvec[3] = {0};

  wvec[0] = fi->fi_width;
  hvec[0] = fi->fi_height;

  glw_video_configure(gv, gve);

  if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
    return -1;

  int linesize = LINESIZE(fi->fi_width, 3);

  const uint8_t *src = fi->fi_data[0];
  uint8_t *dst = s->gvs_data[0];
  for(int y = 0; y < fi->fi_height; y++) {
    memcpy(dst, src, linesize);
    src += fi->fi_pitch[0];
    dst += linesize;
  }

  glw_video_put_surface(gv, s, pts, fi->fi_epoch, fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_BGR = {
  .gve_type     = 'BGR',
  .gve_newframe = video_opengl_newframe,
  .gve_render   = video_opengl_render,
  .gve_reset    = video_opengl_reset,
  .gve_init     = bgr_init,
  .gve_deliver  = bgr_deliver,
};

GLW_REGISTER_GVE(glw_video_BGR);

