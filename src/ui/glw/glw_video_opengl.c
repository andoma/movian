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
#include "video/yadif.h"

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
 *
 */
typedef struct gl_video_frame {

  video_decoder_frame_t gvf_vdf;

  GLuint gvf_pbo[3];
  void *gvf_pbo_ptr[3];

  int gvf_uploaded;

  GLuint gvf_textures[3];

  unsigned int gvf_frame_buffer;

} gl_video_frame_t;


/**
 *
 */
typedef struct glw_video {

  glw_t w;

  LIST_ENTRY(glw_video) gv_global_link;

  video_decoder_t *gv_vd;
  video_playback_t *gv_vp;

  float gv_cmatrix[9];

  video_decoder_frame_t *gv_fra, *gv_frb;
  float gv_blend;

  media_pipe_t *gv_mp;

  // Used to map mouse pointer coords to video frame pixels
  int gv_width;
  int gv_height;

  glw_video_overlay_t gv_spu; // DVD SPU 
  glw_video_overlay_t gv_sub; // Subtitles
  
} glw_video_t;



static void glw_video_frame_deliver(video_decoder_t *vd, AVCodecContext *ctx,
				    AVFrame *frame, int64_t pts, int epoch, 
				    int duration, int disable_deinterlacer);

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
 * 
 */
static void
framepurge(video_decoder_t *vd, video_decoder_frame_t *vdf)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;
  int i;

  for(i = 0; i < 3; i++) {
    if(gvf->gvf_pbo[i] != 0) {
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[i]);
      glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
      gvf->gvf_pbo_ptr[i] = NULL;
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    }
    glDeleteBuffersARB(3, gvf->gvf_pbo);
  }

  for(i = 0; i < 3; i++)
    gvf->gvf_pbo[i] = 0;

  if(gvf->gvf_textures[0] != 0)
    glDeleteTextures(3, gvf->gvf_textures);

  gvf->gvf_textures[0] = 0;

  free(vdf);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
}


/**
 *
 */
void
glw_video_opengl_flush(glw_root_t *gr)
{
  glw_video_t *gv;
  video_decoder_t *vd;

  LIST_FOREACH(gv, &gr->gr_be.gbr_video_decoders, gv_global_link) {
    vd = gv->gv_vd;

    hts_mutex_lock(&vd->vd_queue_mutex);
    glw_video_purge_queues(vd, framepurge);
    hts_cond_signal(&vd->vd_avail_queue_cond);
    hts_cond_signal(&vd->vd_bufalloced_queue_cond);
    hts_mutex_unlock(&vd->vd_queue_mutex);
  }
}


/**
 *  Buffer allocator
 */
static void
gv_buffer_allocator(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;
  video_decoder_frame_t *vdf;
  int i;

  hts_mutex_lock(&vd->vd_queue_mutex);
  
  while(vd->vd_active_frames < vd->vd_active_frames_needed) {
    vdf = calloc(1, sizeof(gl_video_frame_t));
    TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_avail_queue_cond);
    vd->vd_active_frames++;
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);

    gvf = (gl_video_frame_t *)vdf;

    if(gvf->gvf_pbo[0] != 0)
      glDeleteBuffersARB(3, gvf->gvf_pbo);
    glGenBuffersARB(3, gvf->gvf_pbo);


    /* XXX: Do we really need to delete textures if they are already here ? */
       
    if(gvf->gvf_textures[0] != 0)
      glDeleteTextures(3, gvf->gvf_textures);

    glGenTextures(3, gvf->gvf_textures);

    for(i = 0; i < 3; i++) {

      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[i]);

      glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB,
		      vdf->vdf_width[i] * vdf->vdf_height[i],
		      NULL, GL_STREAM_DRAW_ARB);

      gvf->gvf_pbo_ptr[i] = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
					   GL_WRITE_ONLY);

      vdf->vdf_data[i] = gvf->gvf_pbo_ptr[i];
    }
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    TAILQ_INSERT_TAIL(&vd->vd_bufalloced_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_bufalloced_queue_cond);
  }

  hts_mutex_unlock(&vd->vd_queue_mutex);

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
gv_tex_get(gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


/**
 *
 */
static void
video_frame_upload(glw_video_t *gv, gl_video_frame_t *gvf, int textype)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[0]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr[0] = NULL;

  glBindTexture(textype, gv_tex_get(gvf, GVF_TEX_L));
  gv_set_tex_meta(textype);
  
  glTexImage2D(textype, 0, 1, 
	       gvf->gvf_vdf.vdf_width[0], gvf->gvf_vdf.vdf_height[0],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);


  /* Cr */

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[2]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr[1] = NULL;

  glBindTexture(textype, gv_tex_get(gvf, GVF_TEX_Cr));
  gv_set_tex_meta(textype);


  glTexImage2D(textype, 0, 1,
	       gvf->gvf_vdf.vdf_width[1], gvf->gvf_vdf.vdf_height[1],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

  /* Cb */
  
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[1]);
  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr[2] = NULL;

  glBindTexture(textype, gv_tex_get(gvf, GVF_TEX_Cb));

  gv_set_tex_meta(textype);
  
  glTexImage2D(textype, 0, 1,
	       gvf->gvf_vdf.vdf_width[2], gvf->gvf_vdf.vdf_height[2],
	       0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
}


/**
 *  Video widget layout
 */
static void
gv_enqueue_for_decode(video_decoder_t *vd, video_decoder_frame_t *vdf,
		      struct video_decoder_frame_queue *fromqueue)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;
  int i;

  hts_mutex_lock(&vd->vd_queue_mutex);

  TAILQ_REMOVE(fromqueue, vdf, vdf_link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    for(i = 0; i < 3; i++) {
      glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo[i]);

      // Setting the buffer to NULL tells the GPU it can assign
      // us another piece of memory as backing store.
      
      glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB,
		      vdf->vdf_width[i] * vdf->vdf_height[i],
		      NULL, GL_STREAM_DRAW_ARB);

      gvf->gvf_pbo_ptr[i] = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
					   GL_WRITE_ONLY);
      vdf->vdf_data[i] = gvf->gvf_pbo_ptr[i];
    }
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
  hts_cond_signal(&vd->vd_avail_queue_cond);
  hts_mutex_unlock(&vd->vd_queue_mutex);
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
gv_compute_blend(glw_video_t *gv, video_decoder_frame_t *fra,
		 video_decoder_frame_t *frb, int output_duration)
{
  int64_t pts;
  int x;

  if(fra->vdf_duration >= output_duration) {
  
    gv->gv_fra = fra;
    gv->gv_frb = NULL;

    pts = fra->vdf_pts;

    fra->vdf_duration -= output_duration;
    fra->vdf_pts      += output_duration;

  } else if(frb != NULL) {

    gv->gv_fra = fra;
    gv->gv_frb = frb;
    gv->gv_blend = (float) fra->vdf_duration / (float)output_duration;

    if(fra->vdf_duration + 
       frb->vdf_duration < output_duration) {

      fra->vdf_duration = 0;
      pts = frb->vdf_pts;

    } else {
      pts = fra->vdf_pts;
      x = output_duration - fra->vdf_duration;
      frb->vdf_duration -= x;
      frb->vdf_pts      += x;
    }
    fra->vdf_duration = 0;

  } else {
    gv->gv_fra = fra;
    gv->gv_frb = NULL;
    fra->vdf_pts      += output_duration;

    pts = fra->vdf_pts;
  }

  return pts;
}


/**
 *
 */
static void 
gv_new_frame(video_decoder_t *vd, glw_video_t *gv, glw_root_t *gr)
{
  video_decoder_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  struct video_decoder_frame_queue *dq;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  gv_color_matrix_update(gv, mp);
  output_duration = glw_video_compute_output_duration(vd, frame_duration);


  dq = &vd->vd_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&vd->vd_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      gv->gv_fra = fra;
      gv->gv_frb = NULL;
    } else {
      gv->gv_fra = NULL;
      gv->gv_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL)
      gv_enqueue_for_decode(vd, frb, &vd->vd_displaying_queue);
    
    frb = TAILQ_NEXT(fra, vdf_link);

    pts = gv_compute_blend(gv, fra, frb, output_duration);
    epoch = fra->vdf_epoch;

    if(!vd->vd_hold || frb != NULL) {
      if(fra != NULL && fra->vdf_duration == 0)
	glw_video_enqueue_for_display(vd, fra, dq);
    }
    if(frb != NULL && frb->vdf_duration == 0)
      glw_video_enqueue_for_display(vd, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    glw_video_compute_avdiff(vd, mp, pts, epoch);

#if ENABLE_DVD
    glw_video_spu_layout(vd, &gv->gv_spu, gr, pts);
#endif

    glw_video_sub_layout(vd, &gv->gv_sub, gr, pts, (glw_t *)gv);
  }
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
render_video_1f(glw_video_t *gv, video_decoder_t *vd,
		video_decoder_frame_t *vdf, float alpha, int textype,
		int rectmode)
{
  gl_video_frame_t *gvf = (gl_video_frame_t *)vdf;
  int i;
  GLuint tex;

  video_frame_upload(gv, gvf, textype);

  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_prog);


  /* ctrl constants */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha,
			       /* ctrl.y == ishift */
			       (-0.5f * vdf->vdf_debob) / 
			       (float)vdf->vdf_height[0],
			       0.0,
			       0.0);

  /* color matrix */

  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 1 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = gv_tex_get(gvf, GVF_TEX_Cb);
  glBindTexture(textype, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = gv_tex_get(gvf, GVF_TEX_Cr);
  glBindTexture(textype, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = gv_tex_get(gvf, GVF_TEX_L);
  glBindTexture(textype, tex);
  
  render_video_quad(vdf->vdf_cutborder, rectmode, 
		    vdf->vdf_width[0], vdf->vdf_height[0]);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


/**
 *
 */
static void
gv_blend_frames(glw_video_t *gv, video_decoder_t *vd, 
		video_decoder_frame_t *fra, video_decoder_frame_t *frb,
		float alpha, int textype, int rectmode)
{
  float blend = gv->gv_blend;
  gl_video_frame_t *gvf_a = (gl_video_frame_t *)fra;
  gl_video_frame_t *gvf_b = (gl_video_frame_t *)frb;
  int i;
  
  video_frame_upload(gv, gvf_a, textype);
  video_frame_upload(gv, gvf_b, textype);
    
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
			       (-0.5f * fra->vdf_debob) / 
			       (float)fra->vdf_height[0],
			       /* ctrl.w == image b, y displace */
			       (-0.5f * frb->vdf_debob) / 
			       (float)frb->vdf_height[0]);
				
  /* color matrix */
  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 1 + i,
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(textype, gv_tex_get(gvf_b, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(textype, gv_tex_get(gvf_b, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(textype, gv_tex_get(gvf_b, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(textype, gv_tex_get(gvf_a, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(textype, gv_tex_get(gvf_a, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(textype, gv_tex_get(gvf_a, GVF_TEX_L));

  render_video_quad(fra->vdf_cutborder, rectmode, 
		    fra->vdf_width[0], fra->vdf_height[0]);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


/**
 *
 */
static void 
glw_video_render(glw_t *w, glw_rctx_t *rc)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;
  glw_rctx_t rc0 = *rc;
  glw_root_t *gr = w->glw_root;
  video_decoder_frame_t *fra = gv->gv_fra, *frb = gv->gv_frb;
  int width = 0, height = 0;
  int textype = gr->gr_be.gbr_primary_texture_mode;
  int rectmode = !gr->gr_normalized_texture_coords;

  glPushMatrix();

  glw_scale_to_aspect(&rc0, vd->vd_aspect);

  if(fra != NULL && glw_is_focusable(w))
    glw_store_matrix(w, &rc0);

  if(rc0.rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  

  if(fra != NULL) {

    width = fra->vdf_width[0];
    height = fra->vdf_height[0];

    if(frb != NULL) {
      gv_blend_frames(gv, vd, fra, frb, rc0.rc_alpha, textype, rectmode);
    } else {
      render_video_1f(gv, vd, fra, rc0.rc_alpha, textype, rectmode);
    }
  }

  gv->gv_width  = width;
  gv->gv_height = height;

  glEnable(GL_BLEND); 

  glScalef(2.0f / width, -2.0f / height, 0.0f);
  glTranslatef(-width / 2, -height / 2, 0.0f);

  if(width > 0 && (glw_is_focused(w) || !vd->vd_pci.hli.hl_gi.hli_ss))
    gvo_render(&gv->gv_spu, w->glw_root, &rc0);

  gvo_render(&gv->gv_sub, w->glw_root, &rc0);

  glPopMatrix();

  if(gv->gv_sub.gvo_child != NULL)
    glw_render0(gv->gv_sub.gvo_child, rc);
}


/**
 *
 */
static int 
glw_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
			  void *extra)
{
  glw_root_t *gr = w->glw_root;
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    if(gv->gv_sub.gvo_child != NULL)
      glw_layout0(gv->gv_sub.gvo_child, extra);
    return 0;

  case GLW_SIGNAL_DTOR:
    /* We are going away, flush out all frames (PBOs and textures)
       and destroy zombie video decoder */

    gvo_deinit(&gv->gv_spu);
    gvo_deinit(&gv->gv_sub);

    LIST_REMOVE(gv, gv_global_link);
    glw_video_purge_queues(vd, framepurge);
    video_decoder_destroy(vd);
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    gv_buffer_allocator(vd);
    gv_new_frame(vd, gv, gr);
    glw_video_update_focusable(vd, w);
    return 0;

  case GLW_SIGNAL_EVENT:
    return glw_video_widget_event(extra, gv->gv_mp, vd->vd_spu_in_menu);

  case GLW_SIGNAL_DESTROY:
    video_playback_destroy(gv->gv_vp);
    video_decoder_stop(vd);
    mp_ref_dec(gv->gv_mp);
    gv->gv_mp = NULL;
    return 0;

#if ENABLE_DVD
  case GLW_SIGNAL_POINTER_EVENT:
    return glw_video_pointer_event(vd, gv->gv_width, gv->gv_height,
				   extra, gv->gv_mp);
#endif

  default:
    return 0;
  }
}


/**
 *
 */
static void
glw_video_set(glw_t *w, int init, va_list ap)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  const char *filename = NULL;
  prop_t *p, *p2;
  event_t *e;

  if(init) {

    gv->gv_mp = mp_create("Video decoder", "video", MP_VIDEO);

    LIST_INSERT_HEAD(&gr->gr_be.gbr_video_decoders, gv, gv_global_link);

    gv->gv_vd = video_decoder_create(gv->gv_mp);
    gv->gv_vd->vd_frame_deliver = glw_video_frame_deliver;
    gv->gv_vp = video_playback_create(gv->gv_mp);

    // We like fullwindow mode if possible (should be confiurable perhaps)
    glw_set_constraints(w, 0, 0, 0, 0, GLW_CONSTRAINT_F, 0);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_PROPROOTS:
      p = va_arg(ap, void *);

      p2 = prop_create(p, "media");
      
      prop_link(gv->gv_mp->mp_prop_root, p2);
      
      p = va_arg(ap, void *); // Parent, just throw it away
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL && filename[0] != 0) {
    e = event_create_url(EVENT_PLAY_URL, filename);
    mp_enqueue_event(gv->gv_mp, e);
    event_unref(e);
  }
}


/**
 *
 */
static glw_class_t glw_video = {
  .gc_name = "video",
  .gc_flags = GLW_EVERY_FRAME,
  .gc_instance_size = sizeof(glw_video_t),
  .gc_set = glw_video_set,
  .gc_render = glw_video_render,
  .gc_signal_handler = glw_video_widget_callback,
};

GLW_REGISTER_CLASS(glw_video);




typedef enum {
  DEINTERLACE_NONE,
  DEINTERLACE_OPENGL,
  DEINTERLACE_YADIF_FRAME,
  DEINTERLACE_YADIF_FIELD, // Best YADIF version
  DEINTERLACE_YADIF_FRAME_NO_SPATIAL_ILACE,
  DEINTERLACE_YADIF_FIELD_NO_SPATIAL_ILACE,
} deinterlacertype_t;

/**
 * Frame delivery from video decoder
 */
static void
glw_video_frame_deliver(video_decoder_t *vd, AVCodecContext *ctx,
			AVFrame *frame, int64_t pts, int epoch, int duration,
			int disable_deinterlacer)
{
  int hvec[3], wvec[3];
  int tff, w2, mode, i, j, h, w;
  uint8_t *prev, *cur, *next, *src, *dst;
  int hshift, vshift;
  deinterlacertype_t dt;
  video_decoder_frame_t *vdf;

  const int parity = 0;

  if(frame->interlaced_frame && !disable_deinterlacer) {
    dt = DEINTERLACE_OPENGL;
  } else {
    dt = DEINTERLACE_NONE;
  }

  avcodec_get_chroma_sub_sample(ctx->pix_fmt, &hshift, &vshift);

  wvec[0] = ctx->width;
  wvec[1] = ctx->width >> hshift;
  wvec[2] = ctx->width >> hshift;
  hvec[0] = ctx->height;
  hvec[1] = ctx->height >> vshift;
  hvec[2] = ctx->height >> vshift;

  switch(dt) {

  case DEINTERLACE_NONE:
    vd->vd_active_frames_needed = 3;
    if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      h = vdf->vdf_height[i];
      w = vdf->vdf_width[i];
      
      src = frame->data[i];
      dst = vdf->vdf_data[i];
 
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }

    vdf->vdf_pts = pts;
    vdf->vdf_epoch = epoch;
    vdf->vdf_duration = duration;
    vdf->vdf_cutborder = 0;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    return;

  case DEINTERLACE_OPENGL:
    duration /= 2;

    tff = !!frame->top_field_first ^ parity;

    vd->vd_active_frames_needed = 3;

    /*
     *  Deinterlace by 2 x framerate and 0.5 * y-res,
     *  OpenGL does bledning for us
     */

    hvec[0] = hvec[0] / 2;
    hvec[1] = hvec[1] / 2;
    hvec[2] = hvec[2] / 2;

    if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      
      src = frame->data[i]; 
      dst = vdf->vdf_data[i];
      h = vdf->vdf_height[i];
      w = vdf->vdf_width[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i] * 2;
      }
    }
    
    vdf->vdf_debob = !tff;
    
    vdf->vdf_pts = pts;
    vdf->vdf_epoch = epoch;
    vdf->vdf_duration = duration;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);


    if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      
      src = frame->data[i] + frame->linesize[i];
      dst = vdf->vdf_data[i];
      h = vdf->vdf_height[i];
      w = vdf->vdf_width[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i] * 2;
      }
    }
    
    vdf->vdf_debob = tff;
    vdf->vdf_cutborder = 1;

    vdf->vdf_pts = pts + duration;
    vdf->vdf_epoch = epoch;
    vdf->vdf_duration = duration;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    return;
    
  case DEINTERLACE_YADIF_FRAME:
    mode = 0; goto yadif;
  case DEINTERLACE_YADIF_FIELD:
    mode = 1; goto yadif;
  case DEINTERLACE_YADIF_FRAME_NO_SPATIAL_ILACE:
    mode = 2; goto yadif;
  case DEINTERLACE_YADIF_FIELD_NO_SPATIAL_ILACE:
    mode = 3;
  yadif:
    if(vd->vd_yadif_width   != ctx->width  ||
       vd->vd_yadif_height  != ctx->height ||
       vd->vd_yadif_pix_fmt != ctx->pix_fmt) {
      
      vd->vd_yadif_width   = ctx->width;
      vd->vd_yadif_height  = ctx->height;
      vd->vd_yadif_pix_fmt = ctx->pix_fmt;

      for(i = 0; i < 3; i++) {
	avpicture_free(&vd->vd_yadif_pic[i]);
	avpicture_alloc(&vd->vd_yadif_pic[i], ctx->pix_fmt, 
			ctx->width, ctx->height);
      }
    }

    vd->vd_active_frames_needed = 3;
    for(i = 0; i < 3; i++) {
      w = vd->vd_yadif_width  >> (i ? hshift : 0);
      h = vd->vd_yadif_height >> (i ? vshift : 0);
      src = frame->data[i];
      dst = vd->vd_yadif_pic[vd->vd_yadif_phase].data[i];
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }

    tff = !!frame->top_field_first ^ parity;

    pts -= duration;

    if(mode & 1) 
      duration /= 2;

    for(j = 0; j <= (mode & 1); j++) {

      if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
	int y;
	int parity = j ^ tff ^ 1;

	h = vd->vd_yadif_phase;
	next = vd->vd_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	cur = vd->vd_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	prev = vd->vd_yadif_pic[h].data[i];

	dst = vdf->vdf_data[i];
	h = vdf->vdf_height[i];
	w = vdf->vdf_width[i];
	w2 = vd->vd_yadif_width >> (i ? hshift : 0);

	for(y = 0; y < 2; y++) {
	  memcpy(dst, cur, w);
	  dst  += w; prev += w2; cur += w2; next += w2;
	}

	for(; y < h - 2; y++) {

	  if((y ^ parity) & 1) {
	    yadif_filter_line(mode, dst, prev, cur, next, w, w2, parity ^ tff);
	  } else {
	    memcpy(dst, cur, w);
	  }
	  dst  += w; prev += w2; cur += w2; next += w2;
	}

	for(; y < h; y++) {
	  memcpy(dst, cur, w);
	  dst  += w; prev += w2; cur += w2; next += w2;
	}
      }

      /* XXXX: Ugly */
#if defined(__i386__) || defined(__x86_64__)
      asm volatile("emms \n\t" : : : "memory");
#endif

      vdf->vdf_pts = pts + j * duration;
      vdf->vdf_epoch = epoch;
      vdf->vdf_duration = duration;
      vdf->vdf_cutborder = 0;
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    }

    vd->vd_yadif_phase++;
    if(vd->vd_yadif_phase > 2)
      vd->vd_yadif_phase = 0;
    return;
  }
}
