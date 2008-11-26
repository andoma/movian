/*
 *  Video decoder
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

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include "ui/glw/glw.h"

#include "glw_dvdspu.h"

#include "media.h"

#define GV_FRAMES 4

#define GVF_TEX_L   0
#define GVF_TEX_Cr  1
#define GVF_TEX_Cb  2


typedef enum {
  GV_DEILACE_NONE,
  GV_DEILACE_AUTO,
  GV_DEILACE_HALF_RES,
  GV_DEILACE_YADIF_FRAME,
  GV_DEILACE_YADIF_FIELD,
  GV_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE,
  GV_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE,
} deilace_type_t;


TAILQ_HEAD(gl_video_frame_queue, gl_video_frame);

typedef struct {

  float x_next;
  float P_next, P;
  float K, Q, R;
} gv_kalman_t;



typedef struct gl_video_frame {
 
  TAILQ_ENTRY(gl_video_frame) link;

  int gvf_duration;

  int gvf_debob;

  unsigned int gvf_pbo;
  void *gvf_pbo_ptr;
  
  int gvf_pbo_offset[3];

  int gvf_width[3];
  int gvf_height[3];

  uint64_t gvf_pts;
  
  int gvf_uploaded;

  unsigned int gvf_textures[3];

  unsigned int gvf_frame_buffer;

} gl_video_frame_t;



typedef struct glw_video {

  glw_t w;

  int gv_visible;  /* Set if we are currently rendered every frame */
  
  int gv_run_decoder;

  int gv_compensate_thres;

  LIST_ENTRY(glw_video) gv_global_link;


  /* Configuration */

  deilace_type_t gv_deilace_conf;
  int gv_field_parity;
  float gv_zoom;
 

  /* */
  deilace_type_t gv_deilace_type; /* Actual deinterlacer running */

  media_pipe_t *gv_mp;

  gl_video_frame_t gv_frames[GV_FRAMES];

  /* Mutex for protecting access to the frame queues */

  pthread_mutex_t gv_queue_mutex;

  /* Unused frames are in the 'inactive' queue
     gv_buffer_allocator() is responsible for moving frames to/from
     inactive <-> avail queue based on gv_active_frames and
     gv_active_framed_needed */

  struct gl_video_frame_queue gv_inactive_queue;
  int gv_active_frames; /* number of active frames (ie, not on
			    inactive queue */
  int gv_active_frames_needed; /* number of active frames we want */
				   

  /* Display queue contains frames that have been writted into
     and should get displayed. gvf->gvf_uploaded is set once the
     PBO has been texturified */

  struct gl_video_frame_queue gv_display_queue;

  /* Frames on 'avail_queue' are available to decoder for writing into */

  struct gl_video_frame_queue gv_avail_queue;
  pthread_cond_t gv_avail_queue_cond;

  /* Frames on 'displaying_queue' are currently displayed, we cannot
     do anything with these until next frame */

  struct gl_video_frame_queue gv_displaying_queue;

  /* Frames on 'bufalloc' queue needs to have their PBO buffer (re-)alloced
     we cannot do this in the decoder thread (opengl is single threaded)
     so frames are sent to opengl rendered for allocation */

  struct gl_video_frame_queue gv_bufalloc_queue;

  /* Once frames has been (re-)alloced, they are returned on the
     'bufalloced' queue */

  struct gl_video_frame_queue gv_bufalloced_queue;
  pthread_cond_t gv_bufalloced_queue_cond;

  /* Since we may render the same video output multiple times, we keep
     track of the two frames to be displayed separately for the
     render function */

  gl_video_frame_t *gv_fra, *gv_frb;
  float gv_blend;

  int gv_do_flush;

  int gv_interlaced;

  pthread_t gv_ptid;
  
  int64_t gv_nextpts;
  int64_t gv_lastpts;
  int gv_estimated_duration;

  float gv_aspect;

  AVFrame *gv_frame;

  float gv_scale;

  /* Clock (audio - video sync, etc) related members */

  int gv_avdiff;
  int gv_avd_delta;

  int gv_ift;
  int gv_ifti;

  float gv_frameskip_rate;
  float gv_frameskip_ctd;

  float gv_umax, gv_vmax;

  /* DVD / SPU related members */

  gl_dvdspu_t *gv_dvdspu;
  struct dvd_player *gv_dvd;
  
  /* Kalman filter for AVdiff compensation */

  gv_kalman_t gv_avfilter;
  float gv_avdiff_x;

  /* Misc meta */

  const char *gv_codectxt;


  /* color matrix */

  float gv_cmatrix[9];

  /* Subtitles */

  glw_t *gv_subtitle_widget;
  int gv_last_subtitle_index;

  /* Deinterlacing & YADIF */

  AVPicture gv_yadif_pic[3];
  int gv_yadif_width;
  int gv_yadif_height;
  int gv_yadif_pix_fmt;
  int gv_yadif_phase;
  
} glw_video_t;



void glw_video_global_init(glw_root_t *gr);

void glw_video_global_flush(glw_root_t *gr);

void gv_init_timings(glw_video_t *gv);

void gv_kalman_init(gv_kalman_t *gvk);

void glw_video_boot_decoder(glw_video_t *gv, media_pipe_t *mp);

void glw_video_ctor(glw_t *w, int init, va_list ap);

#endif /* VIDEO_DECODER_H */

