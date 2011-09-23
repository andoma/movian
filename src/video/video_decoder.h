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
#include "misc/kalman.h"

#if ENABLE_DVD
TAILQ_HEAD(dvdspu_queue, dvdspu);
#include <dvdnav/dvdnav.h>
#endif

TAILQ_HEAD(video_overlay_queue, video_overlay);


struct AVCodecContext;
struct AVFrame;
struct video_decoder;
struct pixmap;

typedef struct frame_info {
  int width;
  int height;
  int pix_fmt;
  int64_t pts;
  int epoch;
  int duration;

  AVRational dar;

  char interlaced;     // Frame delivered is interlaced 
  char tff;            // For interlaced frame, top-field-first
  char prescaled;      // Output frame is prescaled to requested size
  
  enum AVColorSpace color_space;
  enum AVColorRange color_range;


} frame_info_t;


/**
 *
 */
typedef void (vd_frame_deliver_t)(uint8_t * const data[], const int pitch[],
				  const frame_info_t *info, void *opaque);

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

  int vd_decoder_running;
  int vd_do_flush;
  int vd_skip;

  int64_t vd_nextpts;
  int64_t vd_prevpts;
  int vd_prevpts_cnt;
  int vd_estimated_duration;

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

  /* Deinterlacing */

  int vd_interlaced; // Used to keep deinterlacing on

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
   * Video overlay and subtitles
   */
  struct video_overlay_queue vd_overlay_queue;
  hts_mutex_t vd_overlay_mutex;

  struct ext_subtitles *vd_ext_subtitles;

  /**
   * Bitrate computation
   */
#define VD_FRAME_SIZE_LEN 16
#define VD_FRAME_SIZE_MASK (VD_FRAME_SIZE_LEN - 1)

  int vd_frame_size[VD_FRAME_SIZE_LEN];
  int vd_frame_size_ptr;

  /**
   * Acceleration helpers
   */
  void *vd_accelerator_opaque;
  void (*vd_accelerator_blackout)(void *opaque);
  void (*vd_accelerator_stop)(void *opaque);

} video_decoder_t;

video_decoder_t *video_decoder_create(media_pipe_t *mp, 
				      vd_frame_deliver_t *frame_delivery,
				      void *opaque);

void video_decoder_stop(video_decoder_t *gv);

void video_decoder_destroy(video_decoder_t *gv);

void video_deliver_frame(video_decoder_t *vd,
			 media_pipe_t *mp, media_queue_t *mq,
			 AVCodecContext *ctx, AVFrame *frame,
			 const media_buf_t *mb, int decode_time);

void video_decoder_set_accelerator(video_decoder_t *vd,
				   void (*stopfn)(void *opaque),
				   void (*blackoutfn)(void *opaque),
				   void *opaque);

void video_decoder_scan_ext_sub(video_decoder_t *vd, int64_t pts);


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

#endif /* VIDEO_DECODER_H */

