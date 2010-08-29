/*
 *  Video output on GL surfaces
 *  Copyright (C) 2007-2010 Andreas Öman
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
#include "glw_video_opengl.h"

#define PBO_RELEASE_BEFORE_MAP

static const char *yuv2rbg_code =
#include "cg/yuv2rgb.h"
;

static const char *yuv2rbg_2mix_code =
#include "cg/yuv2rgb_2mix.h"
;

static const char *yuv2rbg_rect_code =
#include "cg/yuv2rgb_rect.h"
;

static const char *yuv2rbg_2mix_rect_code =
#include "cg/yuv2rgb_2mix_rect.h"
;

#include "video/video_decoder.h"
#include "video/video_playback.h"

/**
 *  GL Video Init
 */
static int
glp_check_error(const char *name)
{
  GLint errpos;
  const GLubyte *errstr;

  glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);

  if(errpos == -1)
    return 0;

  errstr = glGetString(GL_PROGRAM_ERROR_STRING_ARB);

  TRACE(TRACE_ERROR, "OpenGL Video", 
	"%s: error \"%s\" on line %d", name, errstr, errpos);
  return 0;
}


/**
 *
 */
void
glw_video_opengl_init(glw_root_t *gr, int rectmode)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  const char *c;
  GLint tu = 0;

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &tu);
  if(tu < 6) {
    TRACE(TRACE_ERROR, "OpenGL", 
	  "Insufficient number of texture image units (%d) "
	  "for GLW video rendering widget. Video output will be corrupted",
	  tu);
  } else {
    TRACE(TRACE_DEBUG, "OpenGL", "%d texture image units available", tu);
  }

  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_prog);
  
  c = rectmode ? yuv2rbg_rect_code : yuv2rbg_code;
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(c), c);

  glp_check_error("yuv2rgb");


  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_2mix_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_2mix_prog);

  c = rectmode ? yuv2rbg_2mix_rect_code : yuv2rbg_2mix_code;
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(c), c);

  glp_check_error("yuv2rgb_2mix");

  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
}


/**
 * gv_surface_mutex must be held
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  int i;

  for(i = 0; i < 3; i++) {
    if(gvs->gvs_pbo[i] != 0) {
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[i]);
      glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
      gvs->gvs_pbo_ptr[i] = NULL;
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    }
  }
  if(gvs->gvs_pbo[0] != 0)
    glDeleteBuffersARB(3, gvs->gvs_pbo);
  gvs->gvs_pbo[0] = 0;

  if(gvs->gvs_textures[0] != 0)
    glDeleteTextures(3, gvs->gvs_textures);
  gvs->gvs_textures[0] = 0;
}


/**
 *
 */
static void
yuvp_reset(glw_video_t *gv)
{
  int i;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(gv, &gv->gv_surfaces[i]);
}


/**
 *
 */
static void
surface_init(glw_video_t *gv, glw_video_surface_t *gvs,
	     const glw_video_config_t *gvc)
{
  int i;

  glGenBuffersARB(3, gvs->gvs_pbo);
  glGenTextures(3, gvs->gvs_textures);

  for(i = 0; i < 3; i++) {

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[i]);
	
    glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB,
		    gvc->gvc_width[i] * gvc->gvc_height[i],
		    NULL, GL_STREAM_DRAW_ARB);
	
    gvs->gvs_pbo_ptr[i] = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
					 GL_WRITE_ONLY);
	
    gvs->gvs_data[i] = gvs->gvs_pbo_ptr[i];
  }
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
}


/**
 *
 */
static void
yuvp_init(glw_video_t *gv)
{
  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  int i;

  for(i = 0; i < gvc->gvc_nsurfaces; i++)
    surface_init(gv, &gv->gv_surfaces[i], gvc);
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
gv_surface_pixmap_upload(glw_video_surface_t *gvs,
			 const glw_video_config_t *gvc, int textype)
{
  if(gvs->gvs_uploaded || gvs->gvs_pbo[0] == 0)
    return;

  gvs->gvs_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[0]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  glBindTexture(textype, gv_tex_get(gvs, GVF_TEX_L));
  gv_set_tex_meta(textype);
  glTexImage2D(textype, 0, 1, gvc->gvc_width[0], gvc->gvc_height[0],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[2]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  glBindTexture(textype, gv_tex_get(gvs, GVF_TEX_Cr));
  gv_set_tex_meta(textype);
  glTexImage2D(textype, 0, 1, gvc->gvc_width[2], gvc->gvc_height[2],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[1]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  glBindTexture(textype, gv_tex_get(gvs, GVF_TEX_Cb));
  gv_set_tex_meta(textype);
  glTexImage2D(textype, 0, 1, gvc->gvc_width[1], gvc->gvc_height[1],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

  gvs->gvs_pbo_ptr[0] = NULL;
  gvs->gvs_pbo_ptr[1] = NULL;
  gvs->gvs_pbo_ptr[2] = NULL;
}


/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  const glw_video_config_t *gvc,
			  struct glw_video_surface_queue *fromqueue)
{
  int i;

  hts_mutex_lock(&gv->gv_surface_mutex);
  TAILQ_REMOVE(fromqueue, gvs, gvs_link);

  if(gvs->gvs_uploaded) {
    gvs->gvs_uploaded = 0;

    for(i = 0; i < 3; i++) {
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvs->gvs_pbo[i]);

      // Setting the buffer to NULL tells the GPU it can assign
      // us another piece of memory as backing store.
#ifdef PBO_RELEASE_BEFORE_MAP
      glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB,
		      gvc->gvc_width[i] * gvc->gvc_height[i],
		      NULL, GL_STREAM_DRAW_ARB);
#endif

      gvs->gvs_pbo_ptr[i] = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
					   GL_WRITE_ONLY);
      gvs->gvs_data[i] = gvs->gvs_pbo_ptr[i];
    }
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
  hts_mutex_unlock(&gv->gv_surface_mutex);
}


/**
 *
 */
static float cmatrix_color[9] = {
  1.1643,  0,        1.5958,
  1.1643, -0.39173, -0.81290,
  1.1643,  2.017,    0
};
#if 0
static float cmatrix_bw[9] = {
  1.1643,  0,        0,
  1.1643,  0,        0,
  1.1643,  0,        0
};
#endif



/**
 *
 */
static void
gv_color_matrix_update(glw_video_t *gv, media_pipe_t *mp)
{
  float *f;
  int i;

  //  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  f = cmatrix_color;

  for(i = 0; i < 9; i++)
    gv->gv_cmatrix[i] = (gv->gv_cmatrix[i] * 15.0 + f[i]) / 16.0;

}



/**
 *
 */
static int64_t
gv_compute_blend(glw_video_t *gv, glw_video_surface_t *sa,
		 glw_video_surface_t *sb, int output_duration)
{
  int64_t pts;
  int x;

  if(sa->gvs_duration >= output_duration) {
  
    gv->gv_sa = sa;
    gv->gv_sb = NULL;

    pts = sa->gvs_pts;

    sa->gvs_duration -= output_duration;
    sa->gvs_pts      += output_duration;

  } else if(sb != NULL) {

    gv->gv_sa = sa;
    gv->gv_sb = sb;
    gv->gv_blend = (float) sa->gvs_duration / (float)output_duration;

    if(sa->gvs_duration + 
       sb->gvs_duration < output_duration) {

      sa->gvs_duration = 0;
      pts = sb->gvs_pts;

    } else {
      pts = sa->gvs_pts;
      x = output_duration - sa->gvs_duration;
      sb->gvs_duration -= x;
      sb->gvs_pts      += x;
    }
    sa->gvs_duration = 0;

  } else {
    gv->gv_sa = sa;
    gv->gv_sb = NULL;
    sa->gvs_pts      += output_duration;

    pts = sa->gvs_pts;
  }

  return pts;
}


/**
 *
 */
static int64_t
yuvp_newframe(glw_video_t *gv, video_decoder_t *vd)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa, *sb, *s;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  gv_color_matrix_update(gv, mp);
  output_duration = glw_video_compute_output_duration(vd, frame_duration);

  
  /* Find new surface to display */
  sa = TAILQ_FIRST(&gv->gv_decoded_queue);
  if(sa == NULL) {
    /* No frame available */
    sa = TAILQ_FIRST(&gv->gv_displaying_queue);
    if(sa != NULL) {
      /* Continue to display last frame */
      gv->gv_sa = sa;
      gv->gv_sa = NULL;
    } else {
      gv->gv_sa = NULL;
      gv->gv_sa = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
    while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL)
      gv_surface_pixmap_release(gv, s, &gv->gv_cfg_cur, 
				&gv->gv_displaying_queue);

    /* */
    sb = TAILQ_NEXT(sa, gvs_link);
    pts = gv_compute_blend(gv, sa, sb, output_duration);
    epoch = sa->gvs_epoch;

    if(!vd->vd_hold || sb != NULL) {
      if(sa != NULL && sa->gvs_duration == 0)
	glw_video_enqueue_for_display(gv, sa, &gv->gv_decoded_queue);
    }
    if(sb != NULL && sb->gvs_duration == 0)
      glw_video_enqueue_for_display(gv, sb, &gv->gv_decoded_queue);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    glw_video_compute_avdiff(gr, vd, mp, pts, epoch);
  }
  return pts;
}


/**
 *  Video widget render
 */
static void
render_video_quad(int interlace, int rectmode, int width, int height)
{
  float x1, x2, y1, y2;

  const int bordersize = 3;

  if(rectmode) {

    if(interlace) {

      x1 = bordersize;
      y1 = bordersize;
      x2 = width  - bordersize;
      y2 = height - bordersize;

    } else {

      x1 = 0;
      y1 = 0;
      x2 = width;
      y2 = height;
    }

  } else {
    
    if(interlace) {

      x1 = 0 + (bordersize / (float)width);
      y1 = 0 + (bordersize / (float)height);
      x2 = 1 - (bordersize / (float)width);
      y2 = 1 - (bordersize / (float)height);

    } else {

      x1 = 0;
      y1 = 0;
      x2 = 1;
      y2 = 1;
    }
  }
 
  glBegin(GL_QUADS);

  glTexCoord2f(x1, y2);
  glVertex3f( -1.0f, -1.0f, 0.0f);
  
  glTexCoord2f(x2, y2);
  glVertex3f( 1.0f, -1.0f, 0.0f);
  
  glTexCoord2f(x2, y1);
  glVertex3f( 1.0f, 1.0f, 0.0f);

  glTexCoord2f(x1, y1);
  glVertex3f( -1.0f, 1.0f, 0.0f);

  glEnd();
}


/**
 *
 */
static void
render_video_1f(const glw_video_t *gv, glw_video_surface_t *s,
		float alpha, int textype, int rectmode)
{
  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  int i;
  GLuint tex;

  gv_surface_pixmap_upload(s, &gv->gv_cfg_cur, textype);

  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_prog);


  /* ctrl constants */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha,
			       /* ctrl.y == ishift */
			       (-0.5f * s->gvs_yshift ) / 
			       (float)gvc->gvc_height[0],
			       0.0,
			       0.0);

  /* color matrix */

  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 1 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = gv_tex_get(s, GVF_TEX_Cb);
  glBindTexture(textype, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = gv_tex_get(s, GVF_TEX_Cr);
  glBindTexture(textype, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = gv_tex_get(s, GVF_TEX_L);
  glBindTexture(textype, tex);
  
  render_video_quad(!!(gvc->gvc_flags & GVC_CUTBORDER), rectmode, 
		    gvc->gvc_width[0], gvc->gvc_height[0]);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


/**
 *
 */
static void
gv_blend_frames(const glw_video_t *gv, 
		glw_video_surface_t *sa, glw_video_surface_t *sb,
		float alpha, int textype, int rectmode)
{
  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  const float blend = gv->gv_blend;
  int i;
  
  gv_surface_pixmap_upload(sa, &gv->gv_cfg_cur, textype);
  gv_surface_pixmap_upload(sb, &gv->gv_cfg_cur, textype);
  
  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_2mix_prog);

  /* ctrl */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha, 
			       /* ctrl.y == blend */
			       blend,
			       /* ctrl.z == image a, y displace */
			       (-0.5f * sa->gvs_yshift) / 
			       (float)gvc->gvc_height[0],
			       /* ctrl.w == image b, y displace */
			       (-0.5f * sb->gvs_yshift) / 
			       (float)gvc->gvc_height[0]);
				
  /* color matrix */
  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 1 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(textype, gv_tex_get(sb, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(textype, gv_tex_get(sb, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(textype, gv_tex_get(sb, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(textype, gv_tex_get(sa, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(textype, gv_tex_get(sa, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(textype, gv_tex_get(sa, GVF_TEX_L));

  render_video_quad(!!(gvc->gvc_flags & GVC_CUTBORDER), rectmode, 
		    gvc->gvc_width[0], gvc->gvc_height[0]);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


/**
 *
 */
static void
yuvp_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_root_t *gr = gv->w.glw_root;
  int textype = gr->gr_be.gbr_primary_texture_mode;
  int rectmode = !gr->gr_normalized_texture_coords;
  glw_video_surface_t *sa = gv->gv_sa, *sb = gv->gv_sb;

  if(sa == NULL)
    return;

  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  
  if(sb != NULL) {
    gv_blend_frames(gv, sa, sb, rc->rc_alpha, textype, rectmode);
  } else {
    render_video_1f(gv, sa, rc->rc_alpha, textype, rectmode);
  }

  glEnable(GL_BLEND); 
}



/**
 *
 */
static glw_video_engine_t glw_video_opengl = {
  .gve_name = "OpenGL YUVP fragment shader",
  .gve_newframe = yuvp_newframe,
  .gve_render = yuvp_render,
  .gve_reset = yuvp_reset,
  .gve_init = yuvp_init,
};


/**
 *
 */
void
glw_video_input_yuvp(glw_video_t *gv,
		     uint8_t * const data[], const int pitch[],
		     const frame_info_t *fi)
{
  int hvec[3], wvec[3];
  int i, h, w;
  uint8_t *src;
  uint8_t *dst;
  int tff;
  int hshift, vshift;
  glw_video_surface_t *s;
  const int parity = 0;

  avcodec_get_chroma_sub_sample(fi->pix_fmt, &hshift, &vshift);

  wvec[0] = fi->width;
  wvec[1] = fi->width >> hshift;
  wvec[2] = fi->width >> hshift;
  hvec[0] = fi->height >> fi->interlaced;
  hvec[1] = fi->height >> (vshift + fi->interlaced);
  hvec[2] = fi->height >> (vshift + fi->interlaced);

  if(glw_video_configure(gv, &glw_video_opengl, wvec, hvec, 3,
			 fi->interlaced ? (GVC_YHALF | GVC_CUTBORDER) : 0))
    return;
  
  if((s = glw_video_get_surface(gv)) == NULL)
    return;

  if(!fi->interlaced) {

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      src = data[i];
      dst = s->gvs_data[i];
 
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i];
      }
    }

    glw_video_put_surface(gv, s, fi->pts, fi->epoch, fi->duration, 0);

  } else {

    int duration = fi->duration >> 1;

    tff = fi->tff ^ parity;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = data[i]; 
      dst = s->gvs_data[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i] * 2;
      }
    }
    
    glw_video_put_surface(gv, s, fi->pts, fi->epoch, duration, !tff);

    if((s = glw_video_get_surface(gv)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = data[i] + pitch[i];
      dst = s->gvs_data[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i] * 2;
      }
    }
    
    glw_video_put_surface(gv, s, fi->pts + duration, fi->epoch, duration, tff);
  }
}
