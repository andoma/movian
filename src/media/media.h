/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#ifndef MEDIA_H
#define MEDIA_H

#include <stdlib.h>

#include "config.h"
#include "settings.h"

#if ENABLE_LIBAV
#include <libavcodec/avcodec.h>

#define MEDIA_TYPE_VIDEO      AVMEDIA_TYPE_VIDEO
#define MEDIA_TYPE_AUDIO      AVMEDIA_TYPE_AUDIO
#define MEDIA_TYPE_DATA       AVMEDIA_TYPE_DATA
#define MEDIA_TYPE_SUBTITLE   AVMEDIA_TYPE_SUBTITLE
#define MEDIA_TYPE_ATTACHMENT AVMEDIA_TYPE_ATTACHMENT

#else

enum codec_id {
  AV_CODEC_ID_AC3 = 1,
  AV_CODEC_ID_EAC3, 
  AV_CODEC_ID_AAC,
  AV_CODEC_ID_MP2,
  AV_CODEC_ID_MPEG2VIDEO,
  AV_CODEC_ID_H264,
  AV_CODEC_ID_DVB_SUBTITLE,
  AV_CODEC_ID_MOV_TEXT,
  AV_CODEC_ID_DVD_SUBTITLE,
};

#define MEDIA_TYPE_VIDEO      0
#define MEDIA_TYPE_AUDIO      1
#define MEDIA_TYPE_DATA       2
#define MEDIA_TYPE_SUBTITLE   3
#define MEDIA_TYPE_ATTACHMENT 4

#endif


#include "arch/atomic.h"
#include "prop/prop.h"
#include "event.h"
#include "misc/pool.h"
#include "media_buf.h"
#include "media_queue.h"
#include "media_codec.h"
#include "media_track.h"

#define PTS_UNSET INT64_C(0x8000000000000000)


extern atomic_t media_buffer_hungry; /* Set if we try to fill media buffers
                                        Code can check this and avoid doing IO
                                        intensive tasks
                                     */


struct media_buf;
struct media_queue;
struct media_pipe;
struct cancellable;

typedef struct event_ts {
  event_t h;
  int64_t ts;
  int epoch;
} event_ts_t;


TAILQ_HEAD(media_pipe_queue, media_pipe);
LIST_HEAD(media_pipe_list, media_pipe);
TAILQ_HEAD(video_overlay_queue, video_overlay);
TAILQ_HEAD(dvdspu_queue, dvdspu);

/**
 *
 */
typedef struct frame_info {
  struct AVFrame *fi_avframe;

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
typedef int (video_frame_deliver_t)(const frame_info_t *info, void *opaque);
typedef int (set_video_codec_t)(uint32_t type, struct media_codec *mc,
				void *opaque, const frame_info_t *info);



/**
 * Media pipe
 */
typedef struct media_pipe {
  atomic_t mp_refcount;

  const char *mp_name;

  LIST_ENTRY(media_pipe) mp_stack_link;
  int mp_flags;
#define MP_PRIMABLE         0x1
#define MP_VIDEO            0x4
#define MP_FLUSH_ON_HOLD    0x8
#define MP_ALWAYS_SATISFIED 0x10
#define MP_CAN_SEEK         0x20
#define MP_CAN_PAUSE        0x40
#define MP_CAN_EJECT        0x80

  AVRational mp_framerate;

  int mp_eof;   // End of file: We don't expect to need to read more data
  int mp_hold;  // Paused

  pool_t *mp_mb_pool;


  unsigned int mp_buffer_current; // Bytes current queued (total for all queues)
  int mp_buffer_delay;            // Current delay of buffer in µs
  unsigned int mp_buffer_limit;   // Max buffer size
  unsigned int mp_max_realtime_delay; // Max delay in a queue (real time)
  int mp_satisfied;        /* If true, means we are satisfied with buffer
			      fullness */

  hts_mutex_t mp_mutex;

  hts_cond_t mp_backpressure;

  media_queue_t mp_video, mp_audio;

  void *mp_video_frame_opaque;
  video_frame_deliver_t *mp_video_frame_deliver;
  set_video_codec_t *mp_set_video_codec;

  hts_mutex_t mp_overlay_mutex; // Also protects mp_spu_queue
  struct video_overlay_queue mp_overlay_queue;
  struct dvdspu_queue mp_spu_queue;

  hts_mutex_t mp_clock_mutex;
  int64_t mp_audio_clock;
  int64_t mp_audio_clock_avtime;
  int mp_audio_clock_epoch;
  int mp_avdelta;           // Audio vs video delta (µs)
  int mp_svdelta;           // Subtitle vs video delta (µs)
  int mp_auto_standby;
  int mp_stats;

  struct audio_decoder *mp_audio_decoder;

  struct event_q mp_eq;
  
  /* Props */

  prop_t *mp_prop_root;
  prop_t *mp_prop_io;
  prop_t *mp_prop_ctrl;
  prop_t *mp_prop_notifications;
  prop_t *mp_prop_primary;
  prop_t *mp_prop_metadata;
  prop_t *mp_prop_metadata_source;
  prop_t *mp_prop_model;
  prop_t *mp_prop_playstatus;
  prop_t *mp_prop_pausereason;
  prop_t *mp_prop_currenttime;
  prop_t *mp_prop_avdelta;
  prop_t *mp_prop_svdelta;
  prop_t *mp_prop_stats;
  prop_t *mp_prop_url;
  prop_t *mp_prop_avdiff;
  prop_t *mp_prop_avdiff_error;
  prop_t *mp_prop_shuffle;
  prop_t *mp_prop_repeat;

  prop_t *mp_prop_canSkipBackward;
  prop_t *mp_prop_canSkipForward;
  prop_t *mp_prop_canSeek;
  prop_t *mp_prop_canPause;
  prop_t *mp_prop_canEject;
  prop_t *mp_prop_canShuffle;
  prop_t *mp_prop_canRepeat;

  prop_t *mp_prop_video;
  prop_t *mp_prop_audio;

  prop_t *mp_prop_audio_track_current;
  prop_t *mp_prop_audio_tracks;

  prop_t *mp_prop_subtitle_track_current;
  prop_t *mp_prop_subtitle_tracks;

  prop_t *mp_prop_buffer_current;
  prop_t *mp_prop_buffer_limit;
  prop_t *mp_prop_buffer_delay;

  prop_sub_t *mp_sub_currenttime;
  prop_sub_t *mp_sub_stats;

  int64_t mp_seek_base;
  int64_t mp_start_time;
  int64_t mp_duration;  // Duration of currently played (0 if unknown)
  int mp_epoch;

  struct vdpau_dev *mp_vdpau_dev;

  media_track_mgr_t mp_audio_track_mgr;
  media_track_mgr_t mp_subtitle_track_mgr;

  /**
   * Settings
   */

  prop_t *mp_setting_root;

  prop_t *mp_setting_video_root;
  prop_t *mp_setting_audio_root;
  prop_t *mp_setting_subtitle_root;

  struct setting_list mp_settings_video;
  struct setting_list mp_settings_audio;
  struct setting_list mp_settings_subtitle;

  struct setting_list mp_settings_video_dir;
  struct setting_list mp_settings_audio_dir;
  struct setting_list mp_settings_subtitle_dir;

  struct setting_list mp_settings_other;

  struct setting *mp_vol_setting;

  /**
   * Extra (created by media_pipe_init_extra)
   */
  void *mp_extra;

  void (*mp_seek_initiate)(struct media_pipe *mp);
  void (*mp_seek_audio_done)(struct media_pipe *mp);
  void (*mp_seek_video_done)(struct media_pipe *mp);
  void (*mp_hold_changed)(struct media_pipe *mp);
  void (*mp_clock_setup)(struct media_pipe *mp, int has_audio);


  /**
   * Volume control
   */

  int mp_vol_user;
  float mp_vol_ui;

  /**
   * Cancellable must be accessed under mp_mutex protection
   */
  struct cancellable *mp_cancellable;

  /**
   * Subtitle loader
   */

  hts_thread_t mp_subtitle_loader_thread;
  char *mp_subtitle_loader_url;
  int mp_subtitle_loader_status;


} media_pipe_t;

extern void (*media_pipe_init_extra)(media_pipe_t *mp);
extern void (*media_pipe_fini_extra)(media_pipe_t *mp);



void media_init(void);

media_pipe_t *mp_create(const char *name, int flags);

void mp_destroy(media_pipe_t *mp);

void mp_reinit_streams(media_pipe_t *mp);

int mp_lockmgr(void *ptr, int op);


static __inline media_pipe_t *  attribute_unused_result
mp_retain(media_pipe_t *mp)
{
  atomic_inc(&(mp)->mp_refcount);
  return mp;
}

void mp_release(media_pipe_t *mp);

void mp_become_primary(struct media_pipe *mp);

void mp_init_audio(struct media_pipe *mp);

void mp_shutdown(struct media_pipe *mp);



void mp_enqueue_event(media_pipe_t *mp, struct event *e);
void mp_enqueue_event_locked(media_pipe_t *mp, event_t *e);
struct event *mp_dequeue_event(media_pipe_t *mp);
struct event *mp_dequeue_event_deadline(media_pipe_t *mp, int timeout);

struct event *mp_wait_for_empty_queues(media_pipe_t *mp);


void mp_bump_epoch(media_pipe_t *mp);

void mp_set_current_time(media_pipe_t *mp, int64_t ts, int epoch,
			 int64_t delta);

void mp_set_playstatus_by_hold(media_pipe_t *mp, int hold, const char *msg);

void mp_set_url(media_pipe_t *mp, const char *url, const char *parent_url,
                const char *parent_title);

#define MP_BUFFER_NONE    0
#define MP_BUFFER_SHALLOW 2
#define MP_BUFFER_DEEP    3

void mp_configure(media_pipe_t *mp, int flags, int buffer_mode,
		  int64_t duration, const char *type);

void mp_set_clr_flags(media_pipe_t *mp, int set, int clr);

void mp_set_duration(media_pipe_t *mp, int64_t duration);

void mp_set_cancellable(media_pipe_t *mp, struct cancellable *c);

#endif /* MEDIA_H */
