/*
 *  Video output on GL surfaces
 *  Copyright (C) 2007 Andreas Öman
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
#include <pthread.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <GL/gl.h>

#include "showtime.h"
#include "media.h"
#include "video_decoder.h"
#include "subtitles.h"
#include "yadif.h"

static GLuint yuv2rbg_prog;
static const char *yuv2rbg_code =
#include "cg/yuv2rgb.h"
;

static GLuint yuv2rbg_2mix_prog;
static const char *yuv2rbg_2mix_code =
#include "cg/yuv2rgb_2mix.h"
;


void
vd_kalman_init(gv_kalman_t *gvk)
{
  gvk->P = 1.0;
  gvk->Q = 1.0/ 100000.0;
  gvk->R = 0.01;
  gvk->x_next = 0.0;
}

static float
vd_kalman_feed(gv_kalman_t *gvk, float z) __attribute__((unused));

static float
vd_kalman_feed(gv_kalman_t *gvk, float z)
{
  float x;

  gvk->P_next = gvk->P + gvk->Q;
  gvk->K = gvk->P_next / (gvk->P_next + gvk->R);
  x = gvk->x_next + gvk->K * (z - gvk->x_next);
  gvk->P = (1 - gvk->K) * gvk->P_next;

  gvk->x_next = x;

  return x;
}

/**************************************************************************
 *
 *  GL Video Pipe Init
 *
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

  fprintf(stderr, "%s: error \"%s\" on line %d\n", name, errstr, errpos);
  return 0;
}

void
vd_init(void)
{

  yadif_init();

  glGenProgramsARB(1, &yuv2rbg_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_code), yuv2rbg_code);

  glp_check_error("yuv2rgb");


  glGenProgramsARB(1, &yuv2rbg_2mix_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_2mix_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_2mix_code), yuv2rbg_2mix_code);

  glp_check_error("yuv2rgb_2mix");

  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
}




/**************************************************************************
 *
 *  Create video output widget
 *
 */

static int gl_video_widget_callback(glw_t *w, void *opaque, 
				    glw_signal_t signal, void *extra);


glw_t *
vd_create_widget(glw_t *p, media_pipe_t *mp, float zdisplacement)
{
  video_decoder_t *vd;

  if(mp->mp_video_decoder == NULL)
     video_decoder_create(mp);

  vd = mp->mp_video_decoder;

  assert(vd->vd_widget == NULL);

  vd->vd_dvdspu = gl_dvdspu_init();

  vd->vd_widget = glw_create(GLW_EXT,
			     GLW_ATTRIB_DISPLACEMENT, 0.0, 0.0, zdisplacement,
			     GLW_ATTRIB_FLAGS, GLW_EVERY_FRAME,
			     GLW_ATTRIB_PARENT, p, 
			     GLW_ATTRIB_SIGNAL_HANDLER, 
			     gl_video_widget_callback, vd, 0,
			     NULL);

  return vd->vd_widget;
}



/**************************************************************************
 *
 *  Buffer allocator
 *
 */


static void
vd_buffer_allocator(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;
  size_t siz;

  pthread_mutex_lock(&vd->vd_queue_mutex);
  
  assert(vd->vd_active_frames_needed <= VD_FRAMES);

  while(vd->vd_active_frames < vd->vd_active_frames_needed) {
    gvf = TAILQ_FIRST(&vd->vd_inactive_queue);
    TAILQ_REMOVE(&vd->vd_inactive_queue, gvf, link);
    TAILQ_INSERT_TAIL(&vd->vd_avail_queue, gvf, link);
    pthread_cond_signal(&vd->vd_avail_queue_cond);
    vd->vd_active_frames++;
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, gvf, link);

    if(gvf->gvf_pbo != 0)
      glDeleteBuffersARB(1, &gvf->gvf_pbo);
    glGenBuffersARB(1, &gvf->gvf_pbo);


    /* XXX: Do we really need to delete textures if they are already here ? */
       
    if(gvf->gvf_textures[0] != 0)
      glDeleteTextures(3, gvf->gvf_textures);

    glGenTextures(3, gvf->gvf_textures);

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

    siz = 
      gvf->gvf_width[0] * gvf->gvf_height[0] + 
      gvf->gvf_width[1] * gvf->gvf_height[1] + 
      gvf->gvf_width[2] * gvf->gvf_height[2];

    glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, siz, NULL, GL_STREAM_DRAW_ARB);

    gvf->gvf_pbo_offset[0] = 0;
    gvf->gvf_pbo_offset[1] = gvf->gvf_width[0] * gvf->gvf_height[0];

    gvf->gvf_pbo_offset[2] = 
      gvf->gvf_pbo_offset[1] + gvf->gvf_width[1] * gvf->gvf_height[1];


    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    TAILQ_INSERT_TAIL(&vd->vd_bufalloced_queue, gvf, link);
    pthread_cond_signal(&vd->vd_bufalloced_queue_cond);
  }

  pthread_mutex_unlock(&vd->vd_queue_mutex);

}

/**************************************************************************
 *
 *  Texture loader
 *
 */


static void
vd_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint 
vd_tex_get(video_decoder_t *vd, gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


static void
render_video_upload(video_decoder_t *vd, gl_video_frame_t *gvf)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr = NULL;

  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_L));
  vd_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1, 
	       gvf->gvf_width[0], gvf->gvf_height[0],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[0]);

  /* Cr */

  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_Cr));
  vd_set_tex_meta();

  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[1], gvf->gvf_height[1],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[2]);

  /* Cb */
  
  
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_Cb));

  vd_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[2], gvf->gvf_height[2],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[1]);
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

}




/**************************************************************************
 *
 *  Video widget layout
 *
 */



static void
vd_enqueue_for_decode(video_decoder_t *vd, gl_video_frame_t *gvf, 
		       struct gl_video_frame_queue *fromqueue)
{
  
  pthread_mutex_lock(&vd->vd_queue_mutex);

  TAILQ_REMOVE(fromqueue, gvf, link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
  
    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&vd->vd_avail_queue, gvf, link);
  pthread_cond_signal(&vd->vd_avail_queue_cond);
  pthread_mutex_unlock(&vd->vd_queue_mutex);
}


static void
vd_enqueue_for_display(video_decoder_t *vd, gl_video_frame_t *gvf,
			struct gl_video_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvf, link);
  TAILQ_INSERT_TAIL(&vd->vd_displaying_queue, gvf, link);
}


static float cmatrix_color[9] = {
  1.1643,  0,        1.5958,
  1.1643, -0.39173, -0.81290,
  1.1643,  2.017,    0
};

static float cmatrix_bw[9] = {
  1.1643,  0,        0,
  1.1643,  0,        0,
  1.1643,  0,        0
};

static void
vd_color_matrix_update(video_decoder_t *vd, media_pipe_t *mp)
{
  float *f;
  int i;

  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  f = cmatrix_color;

  for(i = 0; i < 9; i++)
    vd->vd_cmatrix[i] = (vd->vd_cmatrix[i] * 15.0 + f[i]) / 16.0;

}





static int
vd_compute_output_duration(video_decoder_t *vd, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(vd->vd_avdiff_x > 0) {
    delta = pow(vd->vd_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(vd->vd_avdiff_x < 0) {
    delta = -pow(-vd->vd_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}

static void
vd_compute_avdiff(video_decoder_t *vd, media_pipe_t *mp, int64_t pts)
{
  if(!mp->mp_audio_clock_valid) {
    vd->vd_avdiff_x = 0;
    vd_kalman_init(&vd->vd_avfilter);
    return;
  }

  if(vd->vd_compensate_thres > 0) {
    vd->vd_compensate_thres--;
    vd->vd_avdiff_x = 0;
    vd_kalman_init(&vd->vd_avfilter);
    return;
  }
  

  vd->vd_avdiff = 
    mp->mp_audio_clock - pts - mp->mp_video_conf->gc_avcomp * 1000;

  if(abs(vd->vd_avdiff) < 10000000) {

    vd->vd_avdiff_x = vd_kalman_feed(&vd->vd_avfilter, 
					(float)vd->vd_avdiff / 1000000);
    if(vd->vd_avdiff_x > 10.0f)
      vd->vd_avdiff_x = 10.0f;
    
    if(vd->vd_avdiff_x < -10.0f)
      vd->vd_avdiff_x = -10.0f;
  }
#if 0
  printf("%s: AVDIFF = %f %d %lld %lld\n", 
	 mp->mp_name, vd->vd_avdiff_x, vd->vd_avdiff,
	 mp->mp_audio_clock, pts);
#endif
}


static int64_t
vd_compute_blend(video_decoder_t *vd, gl_video_frame_t *fra,
		  gl_video_frame_t *frb, int output_duration, int paused)
{
  int64_t pts;
  int x;

  if(fra->gvf_duration >= output_duration) {
  
    vd->vd_fra = fra;
    vd->vd_frb = NULL;

    pts = fra->gvf_pts;

    fra->gvf_duration -= output_duration;
    fra->gvf_pts += output_duration;

  } else if(frb != NULL) {

    vd->vd_fra = fra;
    vd->vd_frb = frb;
    vd->vd_blend = (float) fra->gvf_duration / (float)output_duration;

    if(fra->gvf_duration + frb->gvf_duration < output_duration) {

      printf("blend error\n");

      fra->gvf_duration = 0;
      pts = frb->gvf_pts;

    } else {
      pts = fra->gvf_pts;
      x = output_duration - fra->gvf_duration;
      frb->gvf_duration -= x;
      frb->gvf_pts += x;
    }
    fra->gvf_duration = 0;

  } else {
    vd->vd_fra = fra;
    vd->vd_frb = NULL;
    pts = AV_NOPTS_VALUE;
  }

  return pts;
}





static void 
layout_video_pipe(video_decoder_t *vd, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = vd->vd_mp;
  int output_duration;
  int64_t pts = 0;
  vd_conf_t *gc = mp->mp_video_conf;
  struct gl_video_frame_queue *dq;

  if(vd->vd_subtitle_widget)
    glw_layout(vd->vd_subtitle_widget, rc);

  vd->vd_zoom = (vd->vd_zoom * 7.0f + gc->gc_zoom) / 8.0f;


  vd_color_matrix_update(vd, mp);
  output_duration = vd_compute_output_duration(vd, frame_duration);


  dq = &vd->vd_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&vd->vd_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      vd->vd_fra = fra;
      vd->vd_frb = NULL;
    } else {
      vd->vd_fra = NULL;
      vd->vd_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL)
      vd_enqueue_for_decode(vd, frb, &vd->vd_displaying_queue);
      
    frb = TAILQ_NEXT(fra, link);

    pts = vd_compute_blend(vd, fra, frb, output_duration,
			   mp_get_playstatus(mp) == MP_PAUSE);

    if(mp_get_playstatus(mp) != MP_PAUSE || frb != NULL) {
      if(fra != NULL && fra->gvf_duration == 0)
	vd_enqueue_for_display(vd, fra, dq);
    }
    if(frb != NULL && frb->gvf_duration == 0)
      vd_enqueue_for_display(vd, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    vd_compute_avdiff(vd, mp, pts);
  }
  gl_dvdspu_layout(vd->vd_dvd, vd->vd_dvdspu);
}




/**************************************************************************
 *
 *  Video widget render
 *
 */

static void
render_video_quad(media_pipe_t *mp, video_decoder_t *vd, 
		  gl_video_frame_t *gvf)
{
  
  float tzoom = vd->vd_interlaced ? 0.01 : 0.00;
  
  glBegin(GL_QUADS);

  glTexCoord2f(tzoom, tzoom);
  glVertex3f( -1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(vd->vd_umax - tzoom, tzoom);
  glVertex3f( 1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(vd->vd_umax - tzoom, vd->vd_vmax - tzoom);
  glVertex3f( 1.0f, -1.0f, 0.0f);

  glTexCoord2f(tzoom, vd->vd_vmax - tzoom); 
  glVertex3f( -1.0f, -1.0f, 0.0f);

  glEnd();
}




static void
render_video_1f(media_pipe_t *mp, video_decoder_t *vd, 
		gl_video_frame_t *gvf, float alpha)
{
  int i;
  GLuint tex;

  render_video_upload(vd, gvf);

  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_prog);


  /* ctrl constants */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha,
			       /* ctrl.y == ishift */
			       (-0.5f * gvf->gvf_debob) / 
			       (float)gvf->gvf_height[0],
			       0.0,
			       0.0);

  /* color matrix */

  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 vd->vd_cmatrix[i * 3 + 0],
				 vd->vd_cmatrix[i * 3 + 1],
				 vd->vd_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_Cb);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_Cr);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_L);
  glBindTexture(GL_TEXTURE_2D, tex);
  
  render_video_quad(mp, vd, gvf);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


static void
vd_blend_frames(video_decoder_t *vd, glw_rctx_t *rc, gl_video_frame_t *fra,
		 gl_video_frame_t *frb, media_pipe_t *mp)
{
  float blend = vd->vd_blend;
  int i;
  
  render_video_upload(vd, fra);
  render_video_upload(vd, frb);
    
  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_2mix_prog);

  /* ctrl */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       rc->rc_alpha, 
			       /* ctrl.y == blend */
			       blend,
			       /* ctrl.z == image a, y displace */
			       (-0.5f * fra->gvf_debob) / 
			       (float)fra->gvf_height[0],
			       /* ctrl.w == image b, y displace */
			       (-0.5f * frb->gvf_debob) / 
			       (float)frb->gvf_height[0]);
				
  /* color matrix */
  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 vd->vd_cmatrix[i * 3 + 0],
				 vd->vd_cmatrix[i * 3 + 1],
				 vd->vd_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_L));

  render_video_quad(mp, vd, fra);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}




static void 
render_video_pipe(glw_t *w, video_decoder_t *vd, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = vd->vd_mp;
  int width = 0, height = 0;

  /*
   * rescale
   */
 
  glPushMatrix();
  glTranslatef(w->glw_displacement.x,
	       w->glw_displacement.y,
	       w->glw_displacement.z);
 
  if(vd->vd_zoom != 100)
    glScalef(vd->vd_zoom / 100.0f, vd->vd_zoom / 100.0f, 1.0f);

  glw_scale_and_rotate(rc->rc_aspect, vd->vd_aspect, 0.0f);

  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  
  fra = vd->vd_fra;
  frb = vd->vd_frb;

  if(fra != NULL) {

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];

    if(frb != NULL) {
      vd_blend_frames(vd, rc, fra, frb, mp);
    } else {
      render_video_1f(mp, vd, fra, rc->rc_alpha);
    }
  }

  glEnable(GL_BLEND); 

  if(width > 0)
    gl_dvdspu_render(vd->vd_dvdspu, width, height, rc->rc_alpha);

  glPopMatrix();

  if(vd->vd_subtitle_widget)
    glw_render(vd->vd_subtitle_widget, rc);

}

/*
 * 
 */


static void
gvf_purge(video_decoder_t *vd, gl_video_frame_t *gvf)
{
  if(gvf->gvf_pbo != 0) {
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
    glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
    gvf->gvf_pbo_ptr = NULL;
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    glDeleteBuffersARB(1, &gvf->gvf_pbo);
  }

  gvf->gvf_pbo = 0;

  if(gvf->gvf_textures[0] != 0)
    glDeleteTextures(3, gvf->gvf_textures);

  gvf->gvf_textures[0] = 0;

  TAILQ_INSERT_TAIL(&vd->vd_inactive_queue, gvf, link);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
}



static void
vd_purge_queues(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;

  while((gvf = TAILQ_FIRST(&vd->vd_avail_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_avail_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloced_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_displaying_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_display_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_display_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

}



static void
gl_constant_frame_flush(video_decoder_t *vd)
{
  gl_video_frame_t *fra;

  fra = TAILQ_FIRST(&vd->vd_displaying_queue);
  if(fra != NULL) {
    assert(fra->gvf_pbo_ptr == NULL);
    vd_enqueue_for_decode(vd, fra, &vd->vd_displaying_queue);
  } else {
    fra = TAILQ_FIRST(&vd->vd_display_queue);
    if(fra != NULL) {
      vd_enqueue_for_decode(vd, fra, &vd->vd_display_queue);
    }
  }
}


static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
			 void *extra)
{
  video_decoder_t *vd = opaque;

  switch(signal) {
  case GLW_SIGNAL_DTOR:

    /* We are going away, flush out all frames (PBOs and textures),
       and wakeup decoder (it will notice that widget no longer exist
       and just start dropping decoded output) */

    pthread_mutex_lock(&vd->vd_queue_mutex);

    vd_purge_queues(vd);

    vd->vd_widget = NULL;

    pthread_cond_signal(&vd->vd_avail_queue_cond);
    pthread_cond_signal(&vd->vd_bufalloced_queue_cond);
  
    pthread_mutex_unlock(&vd->vd_queue_mutex);

    /* XXX: Does this need correct locking ? */

    gl_dvdspu_deinit(vd->vd_dvdspu);
    vd->vd_dvdspu = NULL;

    video_decoder_purge(vd);
    return 0;

  case GLW_SIGNAL_LAYOUT:
    vd->vd_running = 1;
    glw_set_active(w);
    layout_video_pipe(vd, extra);
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video_pipe(w, vd, extra);
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    vd_buffer_allocator(vd);
    if(!vd->vd_running)
      gl_constant_frame_flush(vd);
    return 0;

  case GLW_SIGNAL_INACTIVE:
    vd->vd_running = 0;
    return 0;


  default:
    return 0;
  }
}
