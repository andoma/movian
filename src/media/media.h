/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#pragma once
#include <stdlib.h>

#include "config.h"
#include "settings.h"
#include "misc/cancellable.h"
#include "misc/lockmgr.h"

// -------------------------------------------------------------------

#define MP_SKIP_LIMIT 3000000 /* µs that must before a skip back is
				 actually considered a restart */


// -------------------------------------------------------------------

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
#include "media_event.h"

#define PTS_UNSET INT64_C(0x8000000000000000)


extern atomic_t media_buffer_hungry; /* Set if we try to fill media buffers
                                        Code can check this and avoid doing IO
                                        intensive tasks
                                     */

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


  union {
    uint8_t *ptr[4];
    uint32_t u32[4];
  } u;

#define fi_data   u.ptr
#define fi_u32    u.u32

  int fi_pitch[4];

  uint32_t fi_type;

  int fi_width;
  int fi_height;
  int64_t fi_pts;
  int64_t fi_user_time;
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

  void (*fi_ref_release)(void *aux);
  void *fi_ref_aux;

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

  LIST_ENTRY(media_pipe) mp_global_link;

  const char *mp_name;

  LIST_ENTRY(media_pipe) mp_stack_link;
  int mp_flags;
#define MP_PRIMABLE         0x1
#define MP_PRE_BUFFERING    0x2   // Enables pre-buffering
#define MP_VIDEO            0x4
#define MP_FLUSH_ON_HOLD    0x8
#define MP_ALWAYS_SATISFIED 0x10
#define MP_CAN_SEEK         0x20
#define MP_CAN_PAUSE        0x40
#define MP_CAN_EJECT        0x80

  AVRational mp_framerate;

  int mp_eof;   // End of file: We don't expect to need to read more data
  int mp_hold_flags; // Paused

#define MP_HOLD_PAUSE         0x1  // The pause event from UI
#define MP_HOLD_PRE_BUFFERING 0x2
#define MP_HOLD_OS            0x4  // Operating system doing other stuff
#define MP_HOLD_STREAM        0x8  // DVD VM pause, etc
#define MP_HOLD_DISPLAY       0x10 // Display on/off
#define MP_HOLD_SYNC          0x20 /* Stream sync, useful when merging streams
                                    * from different sources (HLS, DASH, etc)
                                    */

  int mp_hold_gate;

  /*
   * Prebuffer logic
   *
   * When the queues are empty (initially, after a flush or due to
   * underrun) and the MP_PRE_BUFFERING flag is set the media code
   * will pause the stream (by asserting MP_HOLD_PRE_BUFFERING) until
   * a certain threshold is reached.
   *
   * mp_pre_buffer_delay controls this delay
   */
  int mp_pre_buffer_delay; // in µs


  pool_t *mp_mb_pool;


  unsigned int mp_buffer_current; // Bytes current queued (total for all queues)
  unsigned int mp_buffer_delay;   // Current delay of buffer in µs
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
  int64_t mp_realtime_delta;
  int mp_audio_clock_epoch;
  int mp_avdelta;           // Audio vs video delta (µs)
  int mp_svdelta;           // Subtitle vs video delta (µs)
  int mp_auto_standby;
  int mp_stats_update_limiter;

  struct audio_decoder *mp_audio_decoder;

  /**
   * Event queue for events to be passed to the demuxer
   */
  struct event_q mp_eq;

  /**
   * Callback where the demuxer can intercept events before those are
   * enqueued on the event queue (mp_eq).
   *
   * If this function return non-zero the event is not enqueued on mp_eq
   * otherwise (zero return) the event is enqueued. Enqueueing is also
   * the default action if this callback is unset.
   *
   * Note that it's a good idea for this callback to execute quite
   * quickly (ie, not block on I/O) as it might cause other threads to
   * block otherwise.
   *
   * Note2: mp_mutex will be held when this is called.
   */
  int (*mp_handle_event)(struct media_pipe *mp, void *opaque, event_t *e);
  void *mp_handle_event_opaque;

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
  prop_t *mp_prop_audio_track_current_manual;
  prop_t *mp_prop_audio_tracks;

  prop_t *mp_prop_subtitle_track_current;
  prop_t *mp_prop_subtitle_track_current_manual;
  prop_t *mp_prop_subtitle_tracks;

  prop_t *mp_prop_buffer_current;
  prop_t *mp_prop_buffer_limit;
  prop_t *mp_prop_buffer_delay;

  prop_sub_t *mp_sub_currenttime;
  prop_sub_t *mp_sub_eventsink;

  int64_t mp_seek_base;
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

  int mp_subtitle_loader_running;
  char *mp_subtitle_loader_url;
  int mp_subtitle_loader_status;


  int64_t mp_reset_time;
  int mp_reset_epoch;

} media_pipe_t;

extern void (*media_pipe_init_extra)(media_pipe_t *mp);
extern void (*media_pipe_fini_extra)(media_pipe_t *mp);


/**
 * Global init of the media system
 */
void media_init(void);


/**
 * Create a new media pipe
 */
media_pipe_t *mp_create(const char *name, int flags);

/**
 * Destroy a media pipe. This is supposed to matched to mp_create().
 *
 * This will remove the media pipe from the global property tree so it's
 * no longer visible from the UI, etc
 *
 * It will also release the primary media_pipe poointer
 */
void mp_destroy(media_pipe_t *mp);

/**
 * Reset various things, needs to be done when reusing the same media_pipe
 * for a different source
 */
void mp_reset(media_pipe_t *mp);

/**
 * Lockmgr to be passed to prop framework for prop callbacks into
 * a specific media_pipe
 */
int mp_lockmgr(void *ptr, lockmgr_op_t op);


/**
 * Retain a reference to a media_pipe. Must be matched with a mp_relese()
 */
static __inline media_pipe_t *  attribute_unused_result
mp_retain(media_pipe_t *mp)
{
  atomic_inc(&(mp)->mp_refcount);
  return mp;
}

/**
 * Release a reference to a media_pipe. See mp_retain()
 */
void mp_release(media_pipe_t *mp);

/**
 * Become the primary media pipe.
 *
 * Also calls mp_init_audio()
 */
void mp_become_primary(struct media_pipe *mp);

/**
 * Init audio (without becoming primary)
 */
void mp_init_audio(struct media_pipe *mp);


/**
 * Shutdown the audio decoder, should be called from the player thread
 */
void mp_shutdown(struct media_pipe *mp);


/**
 * Bump epoch, useful when we detect a discontinuity in the player thread.
 */
void mp_bump_epoch(media_pipe_t *mp);


/**
 * Called from video or audio decoing threads to update current time.
 *
 * Should only be called if packet has the mb_drive_clock flag set.
 */
void mp_set_current_time(media_pipe_t *mp, int64_t ts, int epoch,
			 int64_t delta);

/**
 * Will pause the playback
 *
 * @flag is one of MP_HOLD_xxx flags
 *
 * An optional message can be passed (reason for the pause)
 */
void mp_hold(media_pipe_t *mp, int flag, const char *msg);


/**
 * Will unpause the playback
 *
 * @flag is one of MP_HOLD_xxx flags
 */
void mp_unhold(media_pipe_t *mp, int flag);

/**
 * Set current URL.
 *
 * Will also rebind settings if settings (MEDIA_SETTINGS) is compiled in.
 */
void mp_set_url(media_pipe_t *mp, const char *url, const char *parent_url,
                const char *parent_title);

#define MP_BUFFER_NONE    0
#define MP_BUFFER_SHALLOW 2
#define MP_BUFFER_DEEP    3


/**
 * Configure flags, buffer depth, duration, etc
 *
 * Typically called just before playback begins.
 */
void mp_configure(media_pipe_t *mp, int flags, int buffer_mode,
		  int64_t duration, const char *type);


/**
 * Set/clear mp_flags
 */
void mp_set_clr_flags(media_pipe_t *mp, int set, int clr);

/**
 * Update total duration of the currently played object
 */
void mp_set_duration(media_pipe_t *mp, int64_t duration);

/**
 * Can be used to pause/unpause all media pipelines using the given flag
 */
void media_global_hold(int on, int flag);

/**
 * Internal helper
 */
void mp_set_playstatus_by_hold_locked(media_pipe_t *mp, const char *msg);


/**
 * Do underrun processing when in pre-buffering mode
 */
void mp_underrun(media_pipe_t *mp);


/**
 * Check if an underrun has happened, must be called with mp_mutex locked
 */
static inline void
mp_check_underrun(media_pipe_t *mp)
{
  if(mp->mp_flags & MP_PRE_BUFFERING &&
     unlikely(TAILQ_FIRST(&mp->mp_video.mq_q_data) == NULL) &&
     unlikely(TAILQ_FIRST(&mp->mp_audio.mq_q_data) == NULL))
    mp_underrun(mp);
}

void media_discontinuity_debug(media_discontinuity_aux_t *aux,
                               int64_t dts,
                               int64_t pts,
                               int epoch,
                               int skip,
                               const char *prefix);

