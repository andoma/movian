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

#include "media.h"

#define GVF_TEX_L   0
#define GVF_TEX_Cr  1
#define GVF_TEX_Cb  2



/**
 *
 */
typedef struct {
  float x_next;
  float P_next, P;
  float K, Q, R;
} kalman_t;


/**
 *
 */
static inline void
kalman_init(kalman_t *k)
{
  k->P = 1.0;
  k->Q = 1.0/ 100000.0;
  k->R = 0.01;
  k->x_next = 0.0;
}


/**
 *
 */
static inline float
kalman(kalman_t *k, float z)
{
  float x;

  k->P_next = k->P + k->Q;
  k->K = k->P_next / (k->P_next + k->R);
  x = k->x_next + k->K * (z - k->x_next);
  k->P = (1 - k->K) * k->P_next;
  return k->x_next = x;
}


/**
 *
 */
typedef enum {
  VD_DEILACE_NONE,
  VD_DEILACE_AUTO,
  VD_DEILACE_HALF_RES,
  VD_DEILACE_YADIF_FRAME,
  VD_DEILACE_YADIF_FIELD,
  VD_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE,
  VD_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE,
} deilace_type_t;


TAILQ_HEAD(video_decoder_frame_queue, video_decoder_frame);


/**
 *
 */
typedef struct video_decoder_frame {
 
  TAILQ_ENTRY(video_decoder_frame) vdf_link;

  int vdf_duration;
  uint64_t vdf_pts;

  int vdf_debob;

  void *vdf_data[3];

  int vdf_width[3];
  int vdf_height[3];

} video_decoder_frame_t;


/**
 *
 */
typedef struct video_decoder {

  hts_thread_t vd_decoder_thread;

  int vd_hold;

  int vd_compensate_thres;

  LIST_ENTRY(glw_video) vd_global_link;


  /* Configuration */

  deilace_type_t vd_deilace_conf;
  int vd_field_parity;
 

  /* */
  deilace_type_t vd_deilace_type; /* Actual deinterlacer running */

  media_pipe_t *vd_mp;


  /* Mutex for protecting access to the frame queues */

  hts_mutex_t vd_queue_mutex;

  int vd_decoder_running;

  /* gv_buffer_allocator() is responsible for allocating
     frames based on vd_active_frames and vd_active_framed_needed */

  int vd_active_frames; /* number of active frames (ie, not on
			    inactive queue */
  int vd_active_frames_needed; /* number of active frames we want */
				   

  /* Display queue contains frames that have been writted into
     and should get displayed. gvf->gvf_uploaded is set once the
     PBO has been texturified */

  struct video_decoder_frame_queue vd_display_queue;

  /* Frames on 'avail_queue' are available to decoder for writing into */

  struct video_decoder_frame_queue vd_avail_queue;
  hts_cond_t vd_avail_queue_cond;

  /* Frames on 'displaying_queue' are currently displayed, we cannot
     do anything with these until next frame */

  struct video_decoder_frame_queue vd_displaying_queue;

  /* Frames on 'bufalloc' queue needs to have their PBO buffer (re-)alloced
     we cannot do this in the decoder thread (opengl is single threaded)
     so frames are sent to opengl rendered for allocation */

  struct video_decoder_frame_queue vd_bufalloc_queue;

  /* Once frames has been (re-)alloced, they are returned on the
     'bufalloced' queue */

  struct video_decoder_frame_queue vd_bufalloced_queue;
  hts_cond_t vd_bufalloced_queue_cond;

  /* Since we may render the same video output multiple times, we keep
     track of the two frames to be displayed separately for the
     render function */

  int vd_do_flush;

  int vd_interlaced;

  int64_t vd_nextpts;
  int64_t vd_lastpts;
  int vd_estimated_duration;

  float vd_aspect;

  AVFrame *vd_frame;

  /* Clock (audio - video sync, etc) related members */

  int vd_avdiff;
  int vd_avd_delta;

  /* DVD / SPU related members */

  //  gl_dvdspu_t *vd_dvdspu;
  //  struct dvd_player *vd_dvd;
  
  /* Kalman filter for AVdiff compensation */

  kalman_t vd_avfilter;
  float vd_avdiff_x;

  /* Deinterlacing & YADIF */

  AVPicture vd_yadif_pic[3];
  int vd_yadif_width;
  int vd_yadif_height;
  int vd_yadif_pix_fmt;
  int vd_yadif_phase;
  
} video_decoder_t;


video_decoder_t *video_decoder_create(media_pipe_t *mp);

void video_decoder_stop(video_decoder_t *gv);

void video_decoder_destroy(video_decoder_t *gv);

#endif /* VIDEO_DECODER_H */

