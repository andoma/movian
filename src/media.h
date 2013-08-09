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

#include "config.h"

#if ENABLE_LIBAV
#include <libavcodec/avcodec.h>

#define MEDIA_TYPE_VIDEO      AVMEDIA_TYPE_VIDEO
#define MEDIA_TYPE_AUDIO      AVMEDIA_TYPE_AUDIO
#define MEDIA_TYPE_DATA       AVMEDIA_TYPE_DATA
#define MEDIA_TYPE_SUBTITLE   AVMEDIA_TYPE_SUBTITLE
#define MEDIA_TYPE_ATTACHMENT AVMEDIA_TYPE_ATTACHMENT

#else

enum codec_id {
  CODEC_ID_AC3 = 1,
  CODEC_ID_EAC3, 
  CODEC_ID_AAC,
  CODEC_ID_MP2,
  CODEC_ID_MPEG2VIDEO,
  CODEC_ID_H264,
  CODEC_ID_DVB_SUBTITLE,
  CODEC_ID_MOV_TEXT,
  CODEC_ID_DVD_SUBTITLE,
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

#define PTS_UNSET INT64_C(0x8000000000000000)

void media_init(void);

struct media_buf;
struct media_queue;
struct media_pipe;
struct video_decoder;

#define MP_SKIP_LIMIT 2000000 /* µs that must before a skip back is
				 actually considered a restart */

typedef struct event_ts {
  event_t h;
  int64_t ts;
  int epoch;
} event_ts_t;


TAILQ_HEAD(media_buf_queue, media_buf);
TAILQ_HEAD(media_pipe_queue, media_pipe);
LIST_HEAD(media_pipe_list, media_pipe);
TAILQ_HEAD(media_track_queue, media_track);
TAILQ_HEAD(video_overlay_queue, video_overlay);
TAILQ_HEAD(dvdspu_queue, dvdspu);

/**
 *
 */
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
typedef void (video_frame_deliver_t)(const frame_info_t *info, void *opaque);


/**
 *
 */
typedef struct media_codec {
  int refcount;
  struct media_format *fw;
  int codec_id;

  struct AVCodecContext *fmt_ctx;     // Context owned by AVFormatContext
  struct AVCodecContext *ctx;         // Context owned by decoder thread
  
  struct AVCodecParserContext *parser_ctx;

  void *opaque;

  struct media_pipe *mp;

  void (*decode)(struct media_codec *mc, struct video_decoder *vd,
		 struct media_queue *mq, struct media_buf *mb, int reqsize);

  void (*flush)(struct media_codec *mc, struct video_decoder *vd);

  void (*close)(struct media_codec *mc);
  void (*reinit)(struct media_codec *mc);
  void (*reconfigure)(struct media_codec *mc);

} media_codec_t;


/**
 * 
 */
typedef struct media_buf_meta {
  int64_t mbm_delta;
  int64_t mbm_pts;
  int64_t mbm_dts;
  int mbm_epoch;
  uint32_t mbm_duration;
  uint32_t mbm_aspect_override      : 2;
  uint32_t mbm_skip                 : 2;
  uint32_t mbm_keyframe             : 1;
  uint32_t mbm_flush                : 1; 
  uint32_t mbm_nopts                : 1;
  uint32_t mbm_nodts                : 1;
  uint32_t mbm_drive_clock          : 1;
  uint32_t mbm_disable_deinterlacer : 1;
} media_buf_meta_t;


/**
 * A buffer
 */
typedef struct media_buf {
  TAILQ_ENTRY(media_buf) mb_link;

  media_buf_meta_t mb_meta;

#define mb_delta                mb_meta.mbm_delta
#define mb_pts                  mb_meta.mbm_pts
#define mb_dts                  mb_meta.mbm_dts
#define mb_duration             mb_meta.mbm_duration
#define mb_epoch                mb_meta.mbm_epoch
#define mb_aspect_override      mb_meta.mbm_aspect_override
#define mb_skip                 mb_meta.mbm_skip
#define mb_disable_deinterlacer mb_meta.mbm_disable_deinterlacer
#define mb_keyframe             mb_meta.mbm_keyframe
#define mb_drive_clock          mb_meta.mbm_drive_clock

  enum {
    MB_VIDEO,
    MB_AUDIO,


    MB_DVD_CLUT,
    MB_DVD_RESET_SPU,
    MB_DVD_SPU,
    MB_DVD_PCI,

    MB_SUBTITLE,

    MB_CTRL,

    MB_CTRL_FLUSH,
    MB_CTRL_PAUSE,
    MB_CTRL_PLAY,
    MB_CTRL_EXIT,
    MB_CTRL_FLUSH_SUBTITLES,

    MB_CTRL_DVD_HILITE,
    MB_CTRL_EXT_SUBTITLE,

    MB_CTRL_REINITIALIZE, // Full reinit (such as VDPAU context loss)
    MB_CTRL_RECONFIGURE,  // Reconfigure (such as OMX output port changed)

    MB_CTRL_REQ_OUTPUT_SIZE,
    MB_CTRL_DVD_SPU2,
    
    MB_CTRL_UNBLOCK,

    MB_CTRL_SET_VOLUME,

  } mb_data_type;

  void *mb_data;
  media_codec_t *mb_cw;
  void (*mb_dtor)(struct media_buf *mb);

  int mb_size;

  union {
    int32_t mb_data32;
    int mb_rate;
    int mb_codecid;
    int mb_font_context;
  };


  uint8_t mb_stream;

  uint8_t mb_channels;

} media_buf_t;

/*
 * Media queue
 */
typedef struct media_queue {
  struct media_buf_queue mq_q_data;
  struct media_buf_queue mq_q_ctrl;
  struct media_buf_queue mq_q_aux;

  unsigned int mq_packets_current;    /* Packets currently in queue */

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

  struct media_pipe *mq_mp;

} media_queue_t;




/**
 * Media pipe
 */
typedef struct media_track_mgr {

  prop_sub_t *mtm_node_sub;
  prop_sub_t *mtm_current_sub;
  prop_sub_t *mtm_url_sub;
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
  char *mtm_canonical_url;
  rstr_t *mtm_user_pref;  // Configured by user

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

  int mp_eof;   // End of file: We don't expect to need to read more data
  int mp_hold;  // Paused

  pool_t *mp_mb_pool;


  unsigned int mp_buffer_current; // Bytes current queued (total for all queues)
  unsigned int mp_buffer_limit;   // Max buffer size
  unsigned int mp_max_realtime_delay; // Max delay in a queue (real time)
  int mp_satisfied;        /* If true, means we are satisfied with buffer
			      fullness */

  hts_mutex_t mp_mutex;

  hts_cond_t mp_backpressure;

  media_queue_t mp_video, mp_audio;

  void *mp_video_frame_opaque;
  video_frame_deliver_t *mp_video_frame_deliver;
  
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
  prop_t *mp_prop_type;
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

  prop_courier_t *mp_pc;
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

  prop_t *mp_setting_video_root;
  prop_t *mp_setting_audio_root;
  prop_t *mp_setting_subtitle_root;
  prop_t *mp_setting_root;

  struct setting *mp_setting_av_delta;   // Audio vs. Video delta
  struct setting *mp_setting_audio_vol;  // Audio volume
  struct setting *mp_setting_sv_delta;   // Subtitle vs. Video delta
  struct setting *mp_setting_sub_scale;  // Subtitle scaling
  struct setting *mp_setting_sub_displace_y;
  struct setting *mp_setting_sub_displace_x;
  struct setting *mp_setting_sub_on_video; // Subtitle always on video
  struct setting *mp_setting_vzoom;      // Video zoom in %
  struct setting *mp_setting_hstretch;   // Horizontal stretch
  struct setting *mp_setting_fstretch;   // Fullscreen stretch
  struct setting *mp_setting_vdpau_deinterlace;      // Deinterlace interlaced content
  struct setting *mp_setting_standby_after_eof;


  /**
   * Extra (created by media_pipe_init_extra)
   */
  void *mp_extra;

  void (*mp_seek_initiate)(struct media_pipe *mp);
  void (*mp_seek_audio_done)(struct media_pipe *mp);
  void (*mp_seek_video_done)(struct media_pipe *mp);
  void (*mp_hold_changed)(struct media_pipe *mp);

} media_pipe_t;

extern void (*media_pipe_init_extra)(media_pipe_t *mp);
extern void (*media_pipe_fini_extra)(media_pipe_t *mp);


struct AVFormatContext;
struct AVCodecContext;
struct media_format;

/**
 *
 */
typedef struct media_codec_params {
  unsigned int width;
  unsigned int height;
  unsigned int profile;
  unsigned int level;
  int cheat_for_speed;
  const void *extradata;
  size_t extradata_size;
} media_codec_params_t;


/**
 *
 */
typedef struct codec_def {
  LIST_ENTRY(codec_def) link;
  void (*init)(void);
  int (*open)(media_codec_t *mc, const media_codec_params_t *mcp,
	      media_pipe_t *mp);
  int prio;
} codec_def_t;

void media_register_codec(codec_def_t *cd);

// Higher value of prio_ == better preference

#define REGISTER_CODEC(init_, open_, prio_)			   \
  static codec_def_t HTS_JOIN(codecdef, __LINE__) = {		   \
    .init = init_,						   \
    .open = open_,						   \
    .prio = prio_						   \
  };								   \
  static void  __attribute__((constructor))			   \
  HTS_JOIN(registercodecdef, __LINE__)(void)			   \
  { media_register_codec(&HTS_JOIN(codecdef, __LINE__)); }


/**
 *
 */
typedef struct media_format {
  int refcount;
  struct AVFormatContext *fctx;
} media_format_t;

#if ENABLE_LIBAV

media_format_t *media_format_create(struct AVFormatContext *fctx);

void media_format_deref(media_format_t *fw);

#endif

/**
 * Codecs
 */
void media_codec_deref(media_codec_t *cw);

media_codec_t *media_codec_ref(media_codec_t *cw);

media_codec_t *media_codec_create(int codec_id, int parser,
				  struct media_format *fw, 
				  struct AVCodecContext *ctx,
				  const media_codec_params_t *mcp,
                                  media_pipe_t *mp);

void media_buf_free_locked(media_pipe_t *mp, media_buf_t *mb);

void media_buf_free_unlocked(media_pipe_t *mp, media_buf_t *mb);

struct AVPacket;

void mb_enq(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);

media_buf_t *media_buf_alloc_locked(media_pipe_t *mp, size_t payloadsize);
media_buf_t *media_buf_alloc_unlocked(media_pipe_t *mp, size_t payloadsize);
media_buf_t *media_buf_from_avpkt_unlocked(media_pipe_t *mp, struct AVPacket *pkt);

media_pipe_t *mp_create(const char *name, int flags, const char *type);

void mp_reinit_streams(media_pipe_t *mp);

#define mp_ref_inc(mp) atomic_add(&(mp)->mp_refcount, 1)
void mp_ref_dec(media_pipe_t *mp);

int mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
			int auxtype);
struct event *mb_enqueue_with_events_ex(media_pipe_t *mp, media_queue_t *mq, 
					media_buf_t *mb, int *blocked);

#define mb_enqueue_with_events(mp, mq, mb) \
  mb_enqueue_with_events_ex(mp, mq, mb, NULL)

void mb_enqueue_always(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);

void mp_enqueue_event(media_pipe_t *mp, struct event *e);
struct event *mp_dequeue_event(media_pipe_t *mp);
struct event *mp_dequeue_event_deadline(media_pipe_t *mp, int timeout);

struct event *mp_wait_for_empty_queues(media_pipe_t *mp);


void mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd);
//void mp_send_cmd_head(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d);
void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, 
		     uint32_t u);

void mp_flush(media_pipe_t *mp, int blackout);

void mp_bump_epoch(media_pipe_t *mp);

void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u);

void mp_become_primary(struct media_pipe *mp);

void mp_init_audio(struct media_pipe *mp);

void mp_shutdown(struct media_pipe *mp);

void media_update_codec_info_prop(prop_t *p, struct AVCodecContext *ctx);

void media_get_codec_info(struct AVCodecContext *ctx, char *buf, size_t size);

void media_set_metatree(media_pipe_t *mp, prop_t *src);

void media_clear_metatree(media_pipe_t *mp);

void mp_set_current_time(media_pipe_t *mp, int64_t ts, int epoch,
			 int64_t delta);

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

void mp_configure(media_pipe_t *mp, int caps, int buffer_mode,
		  int64_t duration);

void mp_set_duration(media_pipe_t *mp, int64_t duration);

int64_t mq_realtime_delay(media_queue_t *mq);

void mp_load_ext_sub(media_pipe_t *mp, const char *url, AVRational *framerate);

void mq_update_stats(media_pipe_t *mp, media_queue_t *mq);

void mp_add_track(prop_t *parent,
		  const char *title,
		  const char *url,
		  const char *format,
		  const char *longformat,
		  const char *isolang,
		  const char *source,
		  prop_t *sourcep,
		  int score,
                  int autosel);

void mp_add_trackr(prop_t *parent,
		   rstr_t *title,
		   const char *url,
		   rstr_t *format,
		   rstr_t *longformat,
		   rstr_t *isolang,
		   rstr_t *source,
		   prop_t *sourcep,
		   int score,
                   int autosel);

void mp_add_track_off(prop_t *tracks, const char *title);

#endif /* MEDIA_H */
