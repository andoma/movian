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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <GL/gl.h>

#include "showtime.h"
#include "media.h"
#include "glw_video.h"
//#include "subtitles.h"
#include "video/yadif.h"

static const char *yuv2rbg_code =
#include "cg/yuv2rgb.h"
;

static const char *yuv2rbg_2mix_code =
#include "cg/yuv2rgb_2mix.h"
;

static void gv_purge_queues(glw_video_t *gv);


void
gv_kalman_init(gv_kalman_t *gvk)
{
  gvk->P = 1.0;
  gvk->Q = 1.0/ 100000.0;
  gvk->R = 0.01;
  gvk->x_next = 0.0;
}

static float
gv_kalman_feed(gv_kalman_t *gvk, float z) __attribute__((unused));

static float
gv_kalman_feed(gv_kalman_t *gvk, float z)
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

/**
 *
 */
void
glw_video_global_init(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_code), yuv2rbg_code);

  glp_check_error("yuv2rgb");


  glGenProgramsARB(1, &gbr->gbr_yuv2rbg_2mix_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, gbr->gbr_yuv2rbg_2mix_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_2mix_code), yuv2rbg_2mix_code);

  glp_check_error("yuv2rgb_2mix");

  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
}

/**
 *
 */
void
glw_video_global_flush(glw_root_t *gr)
{
  glw_video_t *gv;

  LIST_FOREACH(gv, &gr->gr_be.gbr_video_decoders, gv_global_link) {
    hts_mutex_lock(&gv->gv_queue_mutex);
    gv_purge_queues(gv);
    hts_cond_signal(&gv->gv_avail_queue_cond);
    hts_cond_signal(&gv->gv_bufalloced_queue_cond);
    hts_mutex_unlock(&gv->gv_queue_mutex);
  }
}



/**************************************************************************
 *
 *  Buffer allocator
 *
 */


static void
gv_buffer_allocator(glw_video_t *gv)
{
  gl_video_frame_t *gvf;
  size_t siz;

  hts_mutex_lock(&gv->gv_queue_mutex);
  
  assert(gv->gv_active_frames_needed <= GV_FRAMES);

  while(gv->gv_active_frames < gv->gv_active_frames_needed) {
    gvf = TAILQ_FIRST(&gv->gv_inactive_queue);
    TAILQ_REMOVE(&gv->gv_inactive_queue, gvf, link);
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvf, link);
    hts_cond_signal(&gv->gv_avail_queue_cond);
    gv->gv_active_frames++;
  }

  while((gvf = TAILQ_FIRST(&gv->gv_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_bufalloc_queue, gvf, link);

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

    TAILQ_INSERT_TAIL(&gv->gv_bufalloced_queue, gvf, link);
    hts_cond_signal(&gv->gv_bufalloced_queue_cond);
  }

  hts_mutex_unlock(&gv->gv_queue_mutex);

}

/**************************************************************************
 *
 *  Texture loader
 *
 */


static void
gv_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint 
gv_tex_get(glw_video_t *gv, gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


static void
render_video_upload(glw_video_t *gv, gl_video_frame_t *gvf)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr = NULL;

  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_L));
  gv_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1, 
	       gvf->gvf_width[0], gvf->gvf_height[0],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[0]);

  /* Cr */

  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_Cr));
  gv_set_tex_meta();

  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[1], gvf->gvf_height[1],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[2]);

  /* Cb */
  
  
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, gvf, GVF_TEX_Cb));

  gv_set_tex_meta();
  
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
gv_enqueue_for_decode(glw_video_t *gv, gl_video_frame_t *gvf, 
		       struct gl_video_frame_queue *fromqueue)
{
  
  hts_mutex_lock(&gv->gv_queue_mutex);

  TAILQ_REMOVE(fromqueue, gvf, link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
  
    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvf, link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
  hts_mutex_unlock(&gv->gv_queue_mutex);
}


static void
gv_enqueue_for_display(glw_video_t *gv, gl_video_frame_t *gvf,
			struct gl_video_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvf, link);
  TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, gvf, link);
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
gv_color_matrix_update(glw_video_t *gv, media_pipe_t *mp)
{
  float *f;
  int i;

  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  f = cmatrix_color;

  for(i = 0; i < 9; i++)
    gv->gv_cmatrix[i] = (gv->gv_cmatrix[i] * 15.0 + f[i]) / 16.0;

}





static int
gv_compute_output_duration(glw_video_t *gv, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(gv->gv_avdiff_x > 0) {
    delta = pow(gv->gv_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(gv->gv_avdiff_x < 0) {
    delta = -pow(-gv->gv_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}

static void
gv_compute_avdiff(glw_video_t *gv, media_pipe_t *mp, int64_t pts)
{
  if(!mp->mp_audio_clock_valid) {
    gv->gv_avdiff_x = 0;
    gv_kalman_init(&gv->gv_avfilter);
    return;
  }

  if(gv->gv_compensate_thres > 0) {
    gv->gv_compensate_thres--;
    gv->gv_avdiff_x = 0;
    gv_kalman_init(&gv->gv_avfilter);
    return;
  }
  

  gv->gv_avdiff = mp->mp_audio_clock - pts - gv->gv_avd_delta;

  if(abs(gv->gv_avdiff) < 10000000) {

    gv->gv_avdiff_x = gv_kalman_feed(&gv->gv_avfilter, 
					(float)gv->gv_avdiff / 1000000);
    if(gv->gv_avdiff_x > 10.0f)
      gv->gv_avdiff_x = 10.0f;
    
    if(gv->gv_avdiff_x < -10.0f)
      gv->gv_avdiff_x = -10.0f;
  }
#if 0
  printf("%s: AVDIFF = %f %d %lld %lld\n", 
	 mp->mp_name, gv->gv_avdiff_x, gv->gv_avdiff,
	 mp->mp_audio_clock, pts);
#endif
}


static int64_t
gv_compute_blend(glw_video_t *gv, gl_video_frame_t *fra,
		  gl_video_frame_t *frb, int output_duration, int paused)
{
  int64_t pts;
  int x;

  if(fra->gvf_duration >= output_duration) {
  
    gv->gv_fra = fra;
    gv->gv_frb = NULL;

    pts = fra->gvf_pts;

    fra->gvf_duration -= output_duration;
    fra->gvf_pts += output_duration;

  } else if(frb != NULL) {

    gv->gv_fra = fra;
    gv->gv_frb = frb;
    gv->gv_blend = (float) fra->gvf_duration / (float)output_duration;

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
    gv->gv_fra = fra;
    gv->gv_frb = NULL;
    pts = AV_NOPTS_VALUE;
  }

  return pts;
}





static void 
layout_video_pipe(glw_video_t *gv, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  struct gl_video_frame_queue *dq;
  int frame_duration = gv->w.glw_root->gr_frameduration;

  if(gv->gv_subtitle_widget)
    glw_layout0(gv->gv_subtitle_widget, rc);

  gv_color_matrix_update(gv, mp);
  output_duration = gv_compute_output_duration(gv, frame_duration);


  dq = &gv->gv_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&gv->gv_displaying_queue);
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
      
    while((frb = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL)
      gv_enqueue_for_decode(gv, frb, &gv->gv_displaying_queue);
      
    frb = TAILQ_NEXT(fra, link);

    pts = gv_compute_blend(gv, fra, frb, output_duration,
			   mp_get_playstatus(mp) == MP_PAUSE);

    if(mp_get_playstatus(mp) != MP_PAUSE || frb != NULL) {
      if(fra != NULL && fra->gvf_duration == 0)
	gv_enqueue_for_display(gv, fra, dq);
    }
    if(frb != NULL && frb->gvf_duration == 0)
      gv_enqueue_for_display(gv, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    gv_compute_avdiff(gv, mp, pts);
  }
  //gl_dvdspu_layout(gv->gv_dvd, gv->gv_dvdspu);
}




/**************************************************************************
 *
 *  Video widget render
 *
 */

static void
render_video_quad(media_pipe_t *mp, glw_video_t *gv, 
		  gl_video_frame_t *gvf)
{
  
  float tzoom = gv->gv_interlaced ? 0.01 : 0.00;
  
  glBegin(GL_QUADS);

  glTexCoord2f(tzoom, tzoom);
  glVertex3f( -1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(gv->gv_umax - tzoom, tzoom);
  glVertex3f( 1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(gv->gv_umax - tzoom, gv->gv_vmax - tzoom);
  glVertex3f( 1.0f, -1.0f, 0.0f);

  glTexCoord2f(tzoom, gv->gv_vmax - tzoom); 
  glVertex3f( -1.0f, -1.0f, 0.0f);

  glEnd();
}




static void
render_video_1f(media_pipe_t *mp, glw_video_t *gv, 
		gl_video_frame_t *gvf, float alpha)
{
  int i;
  GLuint tex;

  render_video_upload(gv, gvf);

  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_prog);


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
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_Cb);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_Cr);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = gv_tex_get(gv, gvf, GVF_TEX_L);
  glBindTexture(GL_TEXTURE_2D, tex);
  
  render_video_quad(mp, gv, gvf);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


static void
gv_blend_frames(glw_video_t *gv, glw_rctx_t *rc, gl_video_frame_t *fra,
		 gl_video_frame_t *frb, media_pipe_t *mp)
{
  float blend = gv->gv_blend;
  int i;
  
  render_video_upload(gv, fra);
  render_video_upload(gv, frb);
    
  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB,
		   gv->w.glw_root->gr_be.gbr_yuv2rbg_2mix_prog);

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
				 gv->gv_cmatrix[i * 3 + 0],
				 gv->gv_cmatrix[i * 3 + 1],
				 gv->gv_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, fra, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, fra, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, fra, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, frb, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, frb, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(GL_TEXTURE_2D, gv_tex_get(gv, frb, GVF_TEX_L));

  render_video_quad(mp, gv, fra);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}




static void 
render_video_pipe(glw_t *w, glw_video_t *gv, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int width = 0, height = 0;

  /*
   * rescale
   */
 
  glPushMatrix();
  glTranslatef(w->glw_displacement.x,
	       w->glw_displacement.y,
	       w->glw_displacement.z);
 
  if(gv->gv_zoom != 100)
    glScalef(gv->gv_zoom / 100.0f, gv->gv_zoom / 100.0f, 1.0f);

  glw_rescale(rc->rc_scale_x / rc->rc_scale_y, gv->gv_aspect);

  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  
  fra = gv->gv_fra;
  frb = gv->gv_frb;

  if(fra != NULL) {

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];

    if(frb != NULL) {
      gv_blend_frames(gv, rc, fra, frb, mp);
    } else {
      render_video_1f(mp, gv, fra, rc->rc_alpha);
    }
  }

  glEnable(GL_BLEND); 

#if 0
  if(width > 0)
    gl_dvdspu_render(gv->gv_dvdspu, width, height, rc->rc_alpha);
#endif

  glPopMatrix();

  if(gv->gv_subtitle_widget)
    glw_render0(gv->gv_subtitle_widget, rc);

}

/*
 * 
 */


static void
gvf_purge(glw_video_t *gv, gl_video_frame_t *gvf)
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

  TAILQ_INSERT_TAIL(&gv->gv_inactive_queue, gvf, link);
  assert(gv->gv_active_frames > 0);
  gv->gv_active_frames--;
}



static void
gv_purge_queues(glw_video_t *gv)
{
  gl_video_frame_t *gvf;

  while((gvf = TAILQ_FIRST(&gv->gv_avail_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_avail_queue, gvf, link);
    gvf_purge(gv, gvf);
  }

  while((gvf = TAILQ_FIRST(&gv->gv_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_bufalloc_queue, gvf, link);
    gvf_purge(gv, gvf);
  }

  while((gvf = TAILQ_FIRST(&gv->gv_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_bufalloced_queue, gvf, link);
    gvf_purge(gv, gvf);
  }

  while((gvf = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_displaying_queue, gvf, link);
    gvf_purge(gv, gvf);
  }

  while((gvf = TAILQ_FIRST(&gv->gv_display_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_display_queue, gvf, link);
    gvf_purge(gv, gvf);
  }

}



static void
gl_constant_frame_flush(glw_video_t *gv)
{
  gl_video_frame_t *fra;

  fra = TAILQ_FIRST(&gv->gv_displaying_queue);
  if(fra != NULL) {
    assert(fra->gvf_pbo_ptr == NULL);
    gv_enqueue_for_decode(gv, fra, &gv->gv_displaying_queue);
  } else {
    fra = TAILQ_FIRST(&gv->gv_display_queue);
    if(fra != NULL) {
      gv_enqueue_for_decode(gv, fra, &gv->gv_display_queue);
    }
  }
}


static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
			 void *extra)
{
  glw_video_t *gv = opaque;

  switch(signal) {
  case GLW_SIGNAL_DTOR:

    /* If decoder thread is running ask it to shut itself down */
    if(gv->gv_mp != NULL) {
      w->glw_refcnt++;  /* Don't free widget just yet, 
			   decoder thread will do it */
      gv->gv_run_decoder = 0;

      mp_send_cmd_u32_head(gv->gv_mp, &gv->gv_mp->mp_video, MB_EXIT, 0);
    }

    /* We are going away, flush out all frames (PBOs and textures),
       and wakeup decoder (it will notice that widget no longer exist
       and just start dropping decoded output) */
    gv->gv_run_decoder = 0;

    LIST_REMOVE(gv, gv_global_link);

    hts_mutex_lock(&gv->gv_queue_mutex);

    gv_purge_queues(gv);

    hts_cond_signal(&gv->gv_avail_queue_cond);
    hts_cond_signal(&gv->gv_bufalloced_queue_cond);
  
    hts_mutex_unlock(&gv->gv_queue_mutex);

    /* XXX: Does this need any other locking ? */

    fprintf(stderr, "gl_dvdspu_deinit(gv->gv_dvdspu);!!!!\n");
    gv->gv_dvdspu = NULL;

    return 0;

  case GLW_SIGNAL_LAYOUT:
    gv->gv_visible = 1;
    glw_set_active0(w);
    layout_video_pipe(gv, extra);
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video_pipe(w, gv, extra);
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    gv_buffer_allocator(gv);
    if(!gv->gv_visible)
      gl_constant_frame_flush(gv);
    return 0;

  case GLW_SIGNAL_INACTIVE:
    gv->gv_visible = 0;
    return 0;


  default:
    return 0;
  }
}
/**
 *
 */
static void
glw_video_init(glw_video_t *gv, glw_root_t *gr)
{
  int i;

  //  gv->gv_dvdspu = gl_dvdspu_init();
 
  LIST_INSERT_HEAD(&gr->gr_be.gbr_video_decoders, gv, gv_global_link);

  gv->gv_zoom = 100;
  gv->gv_umax = 1;
  gv->gv_vmax = 1;
  gv_init_timings(gv);

  /* For the exact meaning of these, see gl_video.h */
    
  TAILQ_INIT(&gv->gv_inactive_queue);
  TAILQ_INIT(&gv->gv_avail_queue);
  TAILQ_INIT(&gv->gv_displaying_queue);
  TAILQ_INIT(&gv->gv_display_queue);
  TAILQ_INIT(&gv->gv_bufalloc_queue);
  TAILQ_INIT(&gv->gv_bufalloced_queue);
    
  for(i = 0; i < GV_FRAMES; i++)
    TAILQ_INSERT_TAIL(&gv->gv_inactive_queue, &gv->gv_frames[i], link);
    
  hts_cond_init(&gv->gv_avail_queue_cond);
  hts_cond_init(&gv->gv_bufalloced_queue_cond);
  hts_mutex_init(&gv->gv_queue_mutex);
}



/**
 *
 */
void
glw_video_ctor(glw_t *w, int init, va_list ap)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;

  if(init) {
    glw_signal_handler_int(w, gl_video_widget_callback);
    glw_video_init(gv, gr);
  }
}
