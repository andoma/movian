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

#include "gl_dvdspu.h"

#include "media.h"

#define VD_FRAMES 4

#define GVF_TEX_L   0
#define GVF_TEX_Cr  1
#define GVF_TEX_Cb  2


typedef enum {
  VD_DEILACE_NONE,
  VD_DEILACE_AUTO,
  VD_DEILACE_HALF_RES,
  VD_DEILACE_YADIF_FRAME,
  VD_DEILACE_YADIF_FIELD,
  VD_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE,
  VD_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE,
} vd_deilace_type_t;


typedef struct vd_conf {

  vd_deilace_type_t gc_deilace_type;
  int gc_avcomp;
  int gc_zoom;
  int gc_field_parity;
  
} vd_conf_t;


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



typedef struct video_decoder {

  int vd_running;
  
  int vd_compensate_thres;

  LIST_ENTRY(video_decoder) vd_global_link;

  glw_t *vd_widget;

  vd_deilace_type_t vd_deilace_type; /* Actual deinterlacer running */

  media_pipe_t *vd_mp;

  gl_video_frame_t vd_frames[VD_FRAMES];

  /* Mutex for protecting access to the frame queues */

  pthread_mutex_t vd_queue_mutex;

  /* Unused frames are in the 'inactive' queue
     vd_buffer_allocator() is responsible for moving frames to/from
     inactive <-> avail queue based on vd_active_frames and
     vd_active_framed_needed */

  struct gl_video_frame_queue vd_inactive_queue;
  int vd_active_frames; /* number of active frames (ie, not on
			    inactive queue */
  int vd_active_frames_needed; /* number of active frames we want */
				   

  /* Display queue contains frames that have been writted into
     and should get displayed. gvf->gvf_uploaded is set once the
     PBO has been texturified */

  struct gl_video_frame_queue vd_display_queue;

  /* Frames on 'avail_queue' are available to decoder for writing into */

  struct gl_video_frame_queue vd_avail_queue;
  pthread_cond_t vd_avail_queue_cond;

  /* Frames on 'displaying_queue' are currently displayed, we cannot
     do anything with these until next frame */

  struct gl_video_frame_queue vd_displaying_queue;

  /* Frames on 'bufalloc' queue needs to have their PBO buffer (re-)alloced
     we cannot do this in the decoder thread (opengl is single threaded)
     so frames are sent to opengl rendered for allocation */

  struct gl_video_frame_queue vd_bufalloc_queue;

  /* Once frames has been (re-)alloced, they are returned on the
     'bufalloced' queue */

  struct gl_video_frame_queue vd_bufalloced_queue;
  pthread_cond_t vd_bufalloced_queue_cond;

  /* Since we may render the same video output multiple times, we keep
     track of the two frames to be displayed separately for the
     render function */

  gl_video_frame_t *vd_fra, *vd_frb;
  float vd_blend;

  int vd_do_flush;

  int vd_interlaced;

  pthread_t vd_ptid;
  
  int64_t vd_nextpts;
  int64_t vd_lastpts;
  int vd_estimated_duration;

  float vd_aspect;

  AVFrame *vd_frame;

  float vd_scale;

  /* Clock (audio - video sync, etc) related members */

  int vd_avdiff;

  int vd_ift;
  int vd_ifti;

  float vd_frameskip_rate;
  float vd_frameskip_ctd;

  float vd_umax, vd_vmax;

  /* DVD / SPU related members */

  gl_dvdspu_t *vd_dvdspu;
  struct dvd_player *vd_dvd;
  
  /* Kalman filter for AVdiff compensation */

  gv_kalman_t vd_avfilter;
  float vd_avdiff_x;

  /* Misc meta */

  const char *vd_codectxt;

  float vd_zoom;

  /* color matrix */

  float vd_cmatrix[9];

  /* Subtitles */

  glw_t *vd_subtitle_widget;
  int vd_last_subtitle_index;

  /* YADIF */

  AVPicture vd_yadif_pic[3];
  int vd_yadif_width;
  int vd_yadif_height;
  int vd_yadif_pix_fmt;
  int vd_yadif_phase;

} video_decoder_t;

void vd_init(void);

void vd_set_dvd(media_pipe_t *mp, struct dvd_player *dvd);

void vd_conf_init(vd_conf_t *gc);

glw_t *vd_menu_setup(glw_t *p, vd_conf_t *gc);

void vd_init_timings(video_decoder_t *vd);

void vd_kalman_init(gv_kalman_t *gvk);

void video_decoder_create(media_pipe_t *mp);

void video_decoder_start(video_decoder_t *vd);

void video_decoder_join(media_pipe_t *mp, video_decoder_t *vd);

glw_t *vd_create_widget(glw_t *p, media_pipe_t *mp, float zdisplacement);

void video_decoder_purge(video_decoder_t *vd);

void vd_flush_all(void);

#endif /* VIDEO_DECODER_H */
