/*
 *  Media streaming functions and ffmpeg wrappers
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

#ifndef MEDIA_H
#define MEDIA_H

#include <stdlib.h>
#include <arch/atomic.h>
#include "prop/prop.h"
#include "event.h"
#include "misc/pool.h"

void media_init(void);
struct media_buf;
struct media_queue;
struct video_decoder;

typedef struct event_ts {
  event_t h;
  int64_t ts;

} event_ts_t;


TAILQ_HEAD(media_buf_queue, media_buf);
TAILQ_HEAD(media_pipe_queue, media_pipe);
LIST_HEAD(media_pipe_list, media_pipe);
TAILQ_HEAD(media_track_queue, media_track);

/**
 *
 */
typedef struct media_format {
  int refcount;
  struct AVFormatContext *fctx;
} media_format_t;


/**
 *
 */
typedef struct media_codec {
  int refcount;
  media_format_t *fw;
  struct AVCodec *codec;
  struct AVCodecContext *codec_ctx;
  struct AVCodecParserContext *parser_ctx;

  void *opaque;
  void (*decode)(struct media_codec *mc, struct video_decoder *vd,
		 struct media_queue *mq, struct media_buf *mb, int reqsize);

  void (*close)(struct media_codec *mc);
  void (*reinit)(struct media_codec *mc);

} media_codec_t;


/**
 * A buffer
 */
typedef struct media_buf {
  int64_t mb_dts;
  int64_t mb_pts;
  int64_t mb_time;

  TAILQ_ENTRY(media_buf) mb_link;

  enum {
    MB_VIDEO,
    MB_AUDIO,

    MB_FLUSH,
    MB_FLUSH_SUBTITLES,
    MB_END,

    MB_CTRL_PAUSE,
    MB_CTRL_PLAY,
    MB_CTRL_EXIT,

    MB_DVD_CLUT,
    MB_DVD_RESET_SPU,
    MB_DVD_SPU,
    MB_DVD_PCI,
    MB_DVD_HILITE,

    MB_SUBTITLE,

    MB_REQ_OUTPUT_SIZE,

    MB_BLACKOUT,

    MB_REINITIALIZE,

    MB_EXT_SUBTITLE,

  } mb_data_type;

  void *mb_data;
  media_codec_t *mb_cw;
  void (*mb_dtor)(struct media_buf *mb);

  int mb_size;

  union {
    int32_t mb_data32;
    int mb_rate;
    int mb_codecid;
  };


  uint32_t mb_duration;

  uint8_t mb_aspect_override : 2;
  uint8_t mb_disable_deinterlacer : 1;
  uint8_t mb_skip : 2;
  uint8_t mb_keyframe : 1;
  uint8_t mb_send_pts : 1;

  uint8_t mb_stream;

  uint8_t mb_channels;
  uint8_t mb_epoch;

} media_buf_t;

/*
 * Media queue
 */
typedef struct media_queue {
  struct media_buf_queue mq_q;

  unsigned int mq_packets_current;    /* Packets currently in queue */
  unsigned int mq_packets_threshold;  /* If we are below this threshold
					 the queue is always granted enqueues */

  int mq_stream;             /* Stream id, or -1 if queue is inactive */
  int mq_stream2;            /* Complementary stream */
  hts_cond_t mq_avail;

  int64_t mq_seektarget;

  prop_t *mq_prop_qlen_cur;
  prop_t *mq_prop_qlen_max;

  prop_t *mq_prop_bitrate;   // In kbps

  prop_t *mq_prop_decode_avg;
  prop_t *mq_prop_decode_peak;

  prop_t *mq_prop_upload_avg;
  prop_t *mq_prop_upload_peak;

  prop_t *mq_prop_codec;

  prop_t *mq_prop_too_slow;

} media_queue_t;

/**
 * Media pipe
 */
typedef struct media_track_mgr {

  prop_sub_t *mtm_node_sub;
  prop_sub_t *mtm_current_sub;
  struct media_track_queue mtm_tracks;
  struct media_track *mtm_suggested_track;
  struct media_track *mtm_current;
  struct media_pipe *mtm_mp;

  enum {
    MEDIA_TRACK_MANAGER_AUDIO,
    MEDIA_TRACK_MANAGER_SUBTITLES,
  } mtm_type;

  int mtm_user_set; /* If set by user, and if so, we should not suggest
		       anything */

  char *mtm_current_url;

} media_track_mgr_t;


/**
 * Media pipe
 */
typedef struct media_pipe {
  int mp_refcount;

  const char *mp_name;

  LIST_ENTRY(media_pipe) mp_stack_link;
  int mp_flags;
#define MP_PRIMABLE      0x1
#define MP_ON_STACK      0x2
#define MP_VIDEO         0x4

  pool_t *mp_mb_pool;

  unsigned int mp_buffer_current; // Bytes current queued (total for all queues)
  unsigned int mp_buffer_limit;   // Max buffer size
  unsigned int mp_max_realtime_delay; // Max delay in a queue (real time)
  int mp_satisfied;        /* If true, means we are satisfied with buffer
			      fullness */

  hts_mutex_t mp_mutex;

  hts_cond_t mp_backpressure;

  media_queue_t mp_video, mp_audio;
  
  hts_mutex_t mp_clock_mutex;
  int64_t mp_audio_clock;
  int64_t mp_audio_clock_realtime;
  int mp_audio_clock_epoch;
  int mp_avdelta;           // Audio vs video delta (µs)
  int mp_svdelta;           // Subtitle vs video delta (µs)
  int mp_stats;

  struct audio_decoder *mp_audio_decoder;

  struct event_q mp_eq;
  
  /* Props */

  prop_t *mp_prop_root;
  prop_t *mp_prop_type;
  prop_t *mp_prop_io;
  prop_t *mp_prop_notifications;
  prop_t *mp_prop_primary;
  prop_t *mp_prop_metadata;
  prop_t *mp_prop_model;
  prop_t *mp_prop_playstatus;
  prop_t *mp_prop_pausereason;
  prop_t *mp_prop_currenttime;
  prop_t *mp_prop_avdelta;
  prop_t *mp_prop_svdelta;
  prop_t *mp_prop_stats;
  prop_t *mp_prop_url;
  prop_t *mp_prop_avdiff;
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


  prop_courier_t *mp_pc;
  prop_sub_t *mp_sub_currenttime;
  prop_sub_t *mp_sub_stats;

  /* Audio info props */

  prop_t *mp_prop_audio_channels_root;
  prop_t *mp_prop_audio_channel[8];
  prop_t *mp_prop_audio_channel_level[8];

  int mp_cur_channels;
  int64_t mp_cur_chlayout;

  int64_t mp_current_time;

  struct vdpau_dev *mp_vdpau_dev;

  media_track_mgr_t mp_audio_track_mgr;
  media_track_mgr_t mp_subtitle_track_mgr;

  /**
   * Settings
   */

  prop_t *mp_setting_video_root;
  prop_t *mp_setting_audio_root;
  prop_t *mp_setting_subtitle_root;

  struct setting *mp_setting_av_delta;   // Audio vs. Video delta
  struct setting *mp_setting_sv_delta;   // Subtitle vs. Video delta
  struct setting *mp_setting_sub_scale;  // Subtitle scaling
  struct setting *mp_setting_sub_on_video; // Subtitle always on video
  struct setting *mp_setting_vzoom;      // Video zoom in %

} media_pipe_t;

struct AVFormatContext;
struct AVCodecContext;

/**
 *
 */
media_format_t *media_format_create(struct AVFormatContext *fctx);

void media_format_deref(media_format_t *fw);

/**
 * Codecs
 */
void media_codec_deref(media_codec_t *cw);

media_codec_t *media_codec_ref(media_codec_t *cw);


typedef struct media_codec_params {
  unsigned int width;
  unsigned int height;
  unsigned int profile;
  unsigned int level;
  int cheat_for_speed;
  const void *extradata;
  size_t extradata_size;
} media_codec_params_t;


media_codec_t *media_codec_create(int codec_id, int parser,
				  media_format_t *fw, 
				  struct AVCodecContext *ctx,
				  media_codec_params_t *mcp, media_pipe_t *mp);

void media_buf_free_locked(media_pipe_t *mp, media_buf_t *mb);

void media_buf_free_unlocked(media_pipe_t *mp, media_buf_t *mb);

#ifdef POOL_DEBUG
media_buf_t *media_buf_alloc_locked_ex(media_pipe_t *mp, const char *file, int line);
#define media_buf_alloc_locked(mp) media_buf_alloc_locked_ex(mp, __FILE__, __LINE__)
media_buf_t *media_buf_alloc_unlocked_ex(media_pipe_t *mp, const char *file, int line);
#define media_buf_alloc_unlocked(mp) media_buf_alloc_unlocked_ex(mp, __FILE__, __LINE__)
#else
media_buf_t *media_buf_alloc_locked(media_pipe_t *mp, size_t payloadsize);
media_buf_t *media_buf_alloc_unlocked(media_pipe_t *mp, size_t payloadsize);
#endif

media_pipe_t *mp_create(const char *name, int flags, const char *type);

#define mp_ref_inc(mp) atomic_add(&(mp)->mp_refcount, 1)
void mp_ref_dec(media_pipe_t *mp);

int mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
			int auxtype);
struct event *mb_enqueue_with_events(media_pipe_t *mp, media_queue_t *mq, 
				media_buf_t *mb);
void mb_enqueue_always(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);

void mp_enqueue_event(media_pipe_t *mp, struct event *e);
struct event *mp_dequeue_event(media_pipe_t *mp);
struct event *mp_dequeue_event_deadline(media_pipe_t *mp, int timeout);

struct event *mp_wait_for_empty_queues(media_pipe_t *mp);


void mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_head(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d);
void mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, 
			  uint32_t u);

void mp_flush(media_pipe_t *mp, int blackout);

int mp_seek_in_queues(media_pipe_t *mp, int64_t pos);

void mp_end(media_pipe_t *mp);

void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u);

void mp_become_primary(struct media_pipe *mp);

void mp_init_audio(struct media_pipe *mp);

void mp_shutdown(struct media_pipe *mp);

void media_update_codec_info_prop(prop_t *p, struct AVCodecContext *ctx);

void media_get_codec_info(struct AVCodecContext *ctx, char *buf, size_t size);

void media_set_metatree(media_pipe_t *mp, prop_t *src);

void media_clear_metatree(media_pipe_t *mp);

void mp_set_current_time(media_pipe_t *mp, int64_t mts);


extern media_pipe_t *media_primary;

#define mp_is_primary(mp) ((mp) == media_primary)

void mp_set_playstatus_by_hold(media_pipe_t *mp, int hold, const char *msg);

void mp_set_playstatus_stop(media_pipe_t *mp);

void mp_set_url(media_pipe_t *mp, const char *url);

#define MP_PLAY_CAPS_SEEK 0x1
#define MP_PLAY_CAPS_PAUSE 0x2
#define MP_PLAY_CAPS_EJECT 0x4

#define MP_BUFFER_NONE    0
#define MP_BUFFER_SHALLOW 2
#define MP_BUFFER_DEEP    3

void mp_configure(media_pipe_t *mp, int caps, int buffer_mode);

void mp_load_ext_sub(media_pipe_t *mp, const char *url);

void metadata_from_ffmpeg(char *dst, size_t dstlen, 
			  struct AVCodec *codec, struct AVCodecContext *avctx);

void mp_set_mq_meta(media_queue_t *mq, struct AVCodec *codec,
		    struct AVCodecContext *avctx);

void mq_update_stats(media_pipe_t *mp, media_queue_t *mq);

void mp_add_track(prop_t *parent,
		  const char *title,
		  const char *url,
		  const char *format,
		  const char *longformat,
		  const char *isolang,
		  const char *source,
		  int score);

void mp_add_track_off(prop_t *tracks, const char *title);

#endif /* MEDIA_H */
