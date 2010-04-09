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
#include "misc/avgtime.h"
#if ENABLE_DVD
TAILQ_HEAD(dvdspu_queue, dvdspu);
#include <dvdnav/dvdnav.h>
#endif

TAILQ_HEAD(subtitle_queue, subtitle);


struct AVCodecContext;
struct AVFrame;
struct video_decoder;

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

TAILQ_HEAD(video_decoder_frame_queue, video_decoder_frame);


/**
 *
 */
typedef struct video_decoder_frame {
 
  TAILQ_ENTRY(video_decoder_frame) vdf_link;

  int vdf_duration;
  uint64_t vdf_pts;
  int vdf_epoch;

  uint8_t vdf_debob;
  uint8_t vdf_cutborder;

  void *vdf_data[3];

  int vdf_width[3];
  int vdf_height[3];

} video_decoder_frame_t;


/**
 *
 */
typedef void (vd_frame_deliver_t)(struct video_decoder *vd,
				  uint8_t * const data[],
				  const int pitch[],
				  int width,
				  int height,
				  int pix_fmt,
				  int64_t pts,
				  int epoch,
				  int duration,
				  int flags);

#define VD_INTERLACED 0x1  // Frame delivered is interlaced
#define VD_TFF        0x2  // For interlaced frame, top-field-first

/**
 *
 */
typedef struct video_decoder {

  void *vd_opaque;

  vd_frame_deliver_t *vd_frame_deliver;

  hts_thread_t vd_decoder_thread;

  int vd_hold;

  int vd_compensate_thres;

  LIST_ENTRY(glw_video) vd_global_link;


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

  int vd_skip;

  int64_t vd_nextpts;
  int64_t vd_lastpts;
  int vd_frames_since_last;
  int vd_estimated_duration;

  float vd_aspect;

  AVFrame *vd_frame;

  /* Clock (audio - video sync, etc) related members */

  int vd_avdiff;
  int vd_avd_delta;

   
  /* stats */

  avgtime_t vd_decode_time;
  avgtime_t vd_upload_time;


  /* Kalman filter for AVdiff compensation */

  kalman_t vd_avfilter;
  float vd_avdiff_x;

  /* Deinterlacing & YADIF */

  AVPicture vd_yadif_pic[3];
  int vd_yadif_width;
  int vd_yadif_height;
  int vd_yadif_pix_fmt;
  int vd_yadif_phase;
  


  int vd_may_update_avdiff;

  /**
   * DVD / SPU related members
   */
#ifdef CONFIG_DVD
  struct dvdspu_queue vd_spu_queue;

  uint32_t *vd_spu_clut;
  
  hts_mutex_t vd_spu_mutex;

  pci_t vd_pci;

  int vd_spu_curbut;
  int vd_spu_repaint;

#endif
  int vd_spu_in_menu;

  /**
   * Subtitling
   */
  struct subtitle_queue vd_sub_queue;
  hts_mutex_t vd_sub_mutex;

} video_decoder_t;


video_decoder_t *video_decoder_create(media_pipe_t *mp, 
				      vd_frame_deliver_t *frame_delivery,
				      void *opaque);

void video_decoder_stop(video_decoder_t *gv);

void video_decoder_destroy(video_decoder_t *gv);

video_decoder_frame_t *vd_dequeue_for_decode(video_decoder_t *vd, 
					     int w[3], int h[3]);


/**
 * DVD SPU (SubPicture Units)
 *
 * This include both subtitling and menus on DVDs
 */
#if ENABLE_DVD

typedef struct dvdspu {

  TAILQ_ENTRY(dvdspu) d_link;

  uint8_t *d_data;
  size_t d_size;

  int d_cmdpos;
  int64_t d_pts;

  uint8_t d_palette[4];
  uint8_t d_alpha[4];
  
  int d_x1, d_y1;
  int d_x2, d_y2;

  uint8_t *d_bitmap;

  int d_destroyme;

} dvdspu_t;

void dvdspu_decoder_init(video_decoder_t *vd);

void dvdspu_decoder_deinit(video_decoder_t *vd);

void dvdspu_destroy(video_decoder_t *vd, dvdspu_t *d);

void dvdspu_decoder_dispatch(video_decoder_t *vd, media_buf_t *mb,
			     media_pipe_t *mp);

int dvdspu_decode(dvdspu_t *d, int64_t pts);

#endif


typedef struct subtitle_rect {
  int x,y,w,h;
  char *bitmap;
} subtitle_rect_t;

/**
 * Subtitling
 */
typedef struct subtitle {

  TAILQ_ENTRY(subtitle) s_link;

  int s_active;

  int64_t s_start;
  int64_t s_stop;

  char *s_text;

  int s_num_rects;
  subtitle_rect_t s_rects[0];

} subtitle_t;

void video_subtitle_destroy(video_decoder_t *vd, subtitle_t *s);

void video_subtitles_init(video_decoder_t *vd);

void video_subtitles_deinit(video_decoder_t *vd);

void video_subtitles_decode(video_decoder_t *vd, media_buf_t *mb);


#endif /* VIDEO_DECODER_H */

