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
#include <dvdnav/dvdnav.h>
#endif

TAILQ_HEAD(dvdspu_queue, dvdspu);
TAILQ_HEAD(video_overlay_queue, video_overlay);

#define VIDEO_DECODER_REORDER_SIZE 16
#define VIDEO_DECODER_REORDER_MASK (VIDEO_DECODER_REORDER_SIZE-1)

struct AVCodecontext;
struct AVFrame;
struct video_decoder;
struct pixmap;


typedef struct frame_info {
  uint8_t *fi_data[4];
  int fi_pitch[4];

  uint32_t fi_type;

  int fi_width;
  int fi_height;
  int64_t fi_pts;
  int64_t fi_delta;
  int fi_epoch;
  int fi_duration;

  int fi_dar_num;
  int fi_dar_den;

  int fi_hshift;
  int fi_vshift;

  int fi_pix_fmt;

  char fi_interlaced;     // Frame delivered is interlaced 
  char fi_tff;            // For interlaced frame, top-field-first
  char fi_prescaled;      // Output frame is prescaled to requested size
  char fi_drive_clock;

  enum {
    COLOR_SPACE_UNSET = 0,
    COLOR_SPACE_BT_709,
    COLOR_SPACE_BT_601,
    COLOR_SPACE_SMPTE_240M,
  } fi_color_space;

} frame_info_t;


/**
 *
 */
typedef void (vd_frame_deliver_t)(const frame_info_t *info, void *opaque);

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

  int vd_do_flush;
  int vd_skip;

  int64_t vd_nextpts;
  int64_t vd_prevpts;
  int vd_prevpts_cnt;
  int vd_estimated_duration;

  struct AVFrame *vd_frame;

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
  struct dvdspu_queue vd_spu_queue;

  
  hts_mutex_t vd_spu_mutex;

#ifdef CONFIG_DVD
  uint32_t vd_dvd_clut[16];
  pci_t vd_pci;
#endif

  int vd_spu_curbut;
  int vd_spu_repaint;
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
   * Reordering 
   */

  int vd_reorder_ptr;
  struct media_buf vd_reorder[VIDEO_DECODER_REORDER_SIZE];
} video_decoder_t;

video_decoder_t *video_decoder_create(media_pipe_t *mp, 
				      vd_frame_deliver_t *frame_delivery,
				      void *opaque);

void video_decoder_stop(video_decoder_t *gv);

void video_decoder_destroy(video_decoder_t *gv);

void video_deliver_frame_avctx(video_decoder_t *vd,
			       media_pipe_t *mp, media_queue_t *mq,
			       struct AVCodecContext *ctx,
                               struct AVFrame *frame,
			       const media_buf_t *mb, int decode_time);

void video_deliver_frame(video_decoder_t *vd, const frame_info_t *info);

void video_decoder_set_accelerator(video_decoder_t *vd,
				   void (*stopfn)(void *opaque),
				   void (*blackoutfn)(void *opaque),
				   void *opaque);

#endif /* VIDEO_DECODER_H */

