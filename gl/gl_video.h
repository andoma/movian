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

#ifndef VIDEO_GL_H
#define VIDEO_GL_H

#include <GL/glut.h> 

#include <dvdnav/dvdnav.h>
#include "gl_dvdspu.h"

#include "media.h"

#define GVPF_AUTO_FLUSH 0x1

#define GVP_FRAMES 4
#define GVP_GLPP_FRAMES 2

#define GVF_TEX_L   0
#define GVF_TEX_Cr  1
#define GVF_TEX_Cb  2


typedef enum {
  GVP_PP_NONE,
  GVP_PP_AUTO,
  GVP_PP_DEINTERLACER,
  GVP_PP_TEST,
} gvp_pp_type_t;


typedef struct gvp_conf {

  gvp_pp_type_t gc_postproc_type;
  int gc_avcomp;
  int gc_zoom;
  int gc_field_parity;
  
} gvp_conf_t;


TAILQ_HEAD(gl_video_frame_queue, gl_video_frame);

typedef struct {

  float x_next;
  float P_next, P;
  float K, Q, R;
} gv_kalman_t;



typedef struct gl_video_frame {
 
  TAILQ_ENTRY(gl_video_frame) link;

  int gvf_duration;

  int gvf_interlaced;
  int gvf_bottom_frame;

  GLuint gvf_pbo;
  void *gvf_pbo_ptr;
  
  int gvf_pbo_offset[3];

  int gvf_width[3];
  int gvf_height[3];

  uint64_t gvf_pts;
  
  int gvf_uploaded;

  GLuint gvf_textures[3];

  GLuint gvf_frame_buffer;

} gl_video_frame_t;


typedef enum {
  GVP_STATE_IDLE = 0,           /* No action */
  GVP_STATE_THREAD_RUNNING,     /* Thread is running */
  GVP_STATE_THREAD_DESTROYING,  /* Thread is beeing destroyed */
} gvp_state_t;


typedef struct gl_video_pipe {

  gvp_state_t gvp_state;

  int gvp_rendered;
  int gvp_idle;           /* number of consecutive frames for which
			     we have not been rendering */

  int gvp_flags;

  int gvp_purged;

  int gvp_compensate_thres;

  gvp_conf_t *gvp_conf;

  LIST_ENTRY(gl_video_pipe) gvp_global_link;

  glw_t *gvp_widget;

  gvp_pp_type_t gvp_postproc_type; /* Actual postprocessor running */

  media_pipe_t *gvp_mp;

  gl_video_frame_t gvp_frames[GVP_FRAMES];

  /* Mutex for protecting access to the frame queues */

  pthread_mutex_t gvp_queue_mutex;

  /* Unused frames are in the 'inactive' queue
     gvp_buffer_allocator() is responsible for moving frames to/from
     inactive <-> avail queue based on gvp_active_frames and
     gvp_active_framed_needed */

  struct gl_video_frame_queue gvp_inactive_queue;
  int gvp_active_frames; /* number of active frames (ie, not on
			    inactive queue */
  int gvp_active_frames_needed; /* number of active frames we want */
				   

  /* Display queue contains frames that have been writted into
     and should get displayed. gvf->gvf_uploaded is set once the
     PBO has been texturified */

  struct gl_video_frame_queue gvp_display_queue;

  /* Frames on 'avail_queue' are available to decoder for writing into */

  struct gl_video_frame_queue gvp_avail_queue;
  pthread_cond_t gvp_avail_queue_cond;

  /* Frames on 'displaying_queue' are currently displayed, we cannot
     do anything with these until next frame */

  struct gl_video_frame_queue gvp_displaying_queue;

  /* Frames on 'bufalloc' queue needs to have their PBO buffer (re-)alloced
     we cannot do this in the decoder thread (opengl is single threaded)
     so frames are sent to opengl rendered for allocation */

  struct gl_video_frame_queue gvp_bufalloc_queue;

  /* Once frames has been (re-)alloced, they are returned on the
     'bufalloced' queue */

  struct gl_video_frame_queue gvp_bufalloced_queue;
  pthread_cond_t gvp_bufalloced_queue_cond;

  /* Since we may render the same video output multiple times, we keep
     track of the two frames to be displayed separately for the
     render function */

  gl_video_frame_t *gvp_fra, *gvp_frb;
  float gvp_blend;


  pthread_mutex_t gvp_spill_mutex;
  int gvp_spill;

  int gvp_do_flush;

  int gvp_interlaced;

  pthread_t gvp_decode_thrid; /* Set if thread is running */
  
  int64_t gvp_nextpts;
  int64_t gvp_lastpts;
  int gvp_estimated_duration;

  float gvp_aspect;

  AVFrame *gvp_frame;

  float gvp_scale;

  /* Clock (audio - video sync, etc) related members */

  int gvp_avdiff;

  int gvp_ift;
  int gvp_ifti;

  float gvp_frameskip_rate;
  float gvp_frameskip_ctd;

  float gvp_umax, gvp_vmax;

  /* DVD / SPU related members */

  gl_dvdspu_t *gvp_dvdspu;
  struct dvd_player *gvp_dvd;
  
  /* Kalman filter for AVdiff compensation */

  gv_kalman_t gvp_avfilter;
  float gvp_avdiff_x;

  /* Misc meta */

  const char *gvp_codectxt;

  float gvp_zoom;

  /* color matrix */

  float gvp_cmatrix[9];

  /* Subtitles */

  glw_t *gvp_subtitle_widget;
  int gvp_last_subtitle_index;

} gl_video_pipe_t;

void gvp_init(void);

glw_t *gvp_create(glw_t *p, media_pipe_t *mp, gvp_conf_t *gc, int flags);

void gvp_set_dvd(glw_t *w, struct dvd_player *dvd);

void gvp_conf_init(gvp_conf_t *gc);

glw_t *gvp_menu_setup(glw_t *p, gvp_conf_t *gc);

int gvp_get_idle_frames(glw_t *w);

#endif /* VIDEO_GL_H */
