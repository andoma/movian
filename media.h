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
#include <libavformat/avformat.h>
#include <libhts/htsatomic.h>

#include "event.h"
#include "prop.h"


typedef struct event_ts {
  event_t h;
  int stream;
  int64_t dts;
  int64_t pts;
} event_ts_t;


#define MP_WIDGET_AUTO_DISPLAY_TIME 250

struct appi;

#define MQ_LOWWATER 20
#define MQ_HIWATER  200

TAILQ_HEAD(media_buf_queue, media_buf);
TAILQ_HEAD(media_pipe_queue, media_pipe);
LIST_HEAD(media_pipe_list, media_pipe);

/*
 * ffmpeg does not have mutexes itself, we keep a wrapper struct
 */

typedef struct formatwrap {
  AVFormatContext *format;

  hts_mutex_t fw_mutex;
  hts_cond_t fw_cond;
  LIST_HEAD(, codecwrap) codecs;
} formatwrap_t;



typedef struct codecwrap {
  int refcount;
  AVCodec *codec;
  AVCodecContext *codec_ctx;
  AVCodecParserContext *parser_ctx;
  formatwrap_t *format;
  LIST_ENTRY(codecwrap) format_link;
} codecwrap_t;



/*
 * A buffer
 */

typedef struct media_buf {
  TAILQ_ENTRY(media_buf) mb_link;

  enum {
    MB_VIDEO,
    MB_CLUT,
    MB_RESET_SPU,
    MB_DVD_SPU,
    MB_DVD_PCI,
    MB_DVD_HILITE,
    MB_AUDIO,

    MB_FLUSH,

    MB_CTRL_PAUSE,
    MB_CTRL_PLAY,
    MB_CTRL_EXIT,

  } mb_data_type;

  void *mb_data;
  int mb_size;

  int mb_data32;

  uint32_t mb_duration;
  uint32_t mb_aspect_override;
  int64_t mb_dts;
  int64_t mb_pts;
  int mb_mts;  /* in ms */

  codecwrap_t *mb_cw;

  int mb_stream; /* For feedback */

} media_buf_t;

/*
 * Media queue
 */

typedef struct media_queue {
  struct media_buf_queue mq_q;
  unsigned int mq_len;
  int mq_stream;             /* Stream id, or -1 if queue is inactive */
  hts_cond_t mq_avail;

  prop_t *mq_prop_qlen_cur;
  prop_t *mq_prop_qlen_max;
} media_queue_t;



/**
 * Media pipe
 */
typedef struct media_pipe {
  int mp_refcount;

  //  mp_playstatus_t mp_playstatus;

  const char *mp_name;

  hts_mutex_t mp_mutex;

  hts_cond_t mp_backpressure;

  media_queue_t mp_video, mp_audio;
  
  int64_t mp_audio_clock;
  int mp_audio_clock_valid;

  struct subtitles *mp_subtitles;

  struct audio_decoder *mp_audio_decoder;

  struct event_q mp_eq;
  
  /* Props */

  prop_t *mp_prop_root;
  prop_t *mp_prop_meta;
  prop_t *mp_prop_playstatus;
  prop_t *mp_prop_currenttime_x;

  prop_courier_t *mp_pc;
  prop_sub_t *mp_sub_currenttime;

} media_pipe_t;


/**
 * Format
 */
formatwrap_t *wrap_format_create(AVFormatContext *fctx);

void wrap_format_destroy(formatwrap_t *fw);

/**
 * Codecs
 */
void wrap_codec_deref(codecwrap_t *cw);

codecwrap_t *wrap_codec_ref(codecwrap_t *cw);

codecwrap_t *wrap_codec_create(enum CodecID id, enum CodecType type, 
			       int parser, formatwrap_t *fw,
			       AVCodecContext *ctx,
			       int cheat_for_speed);

void media_buf_free(media_buf_t *mb);


static inline media_buf_t *
media_buf_alloc(void)
{
  media_buf_t *mb = calloc(1, sizeof(media_buf_t));
  mb->mb_mts = -1;
  return mb;
}

media_pipe_t *mp_create(const char *name);

#define mp_ref(mp) atomic_add(&(mp)->mp_refcount, 1)
void mp_unref(media_pipe_t *mp);

void mq_flush(media_queue_t *mq);

int mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);
event_t *mb_enqueue_with_events(media_pipe_t *mp, media_queue_t *mq, 
				media_buf_t *mb);
void mp_enqueue_event(media_pipe_t *mp, event_t *e);
event_t *mp_dequeue_event(media_pipe_t *mp);


void mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_head(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d);
void mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, 
			  uint32_t u);

void mp_flush(media_pipe_t *mp);

int mp_update_hold_by_event(int hold, event_type_t et);

void mp_wait(media_pipe_t *mp, int audio, int video);

void mp_codec_lock(media_pipe_t *mp);

void mp_codec_unlock(media_pipe_t *mp);

void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u);

//#define MP_DONT_GRAB_AUDIO 0x1
//void mp_set_playstatus(media_pipe_t *mp, int status, int flags);

void mp_playpause(struct media_pipe *mp, int key);

#define MP_GRAB_AUDIO 0x1
void mp_prepare(struct media_pipe *mp, int flags);

void mp_hibernate(struct media_pipe *mp);

void media_pipe_acquire_audio(struct media_pipe *mp);

void nice_codec_name(char *buf, int len, AVCodecContext *ctx);

int mp_is_audio_silenced(media_pipe_t *mp);

void media_update_codec_info_prop(prop_t *p, AVCodecContext *ctx);

//void media_update_playstatus_prop(prop_t *p, mp_playstatus_t mps);

void media_get_codec_info(AVCodecContext *ctx, char *buf, size_t size);

extern media_pipe_t *primary_audio;
struct filetag_list;

void media_fill_properties(prop_t *root, const char *url, int type,
			   struct filetag_list *tags);

void media_set_currentmedia(media_pipe_t *mp);
void media_set_metatree(media_pipe_t *mp, prop_t *src);
void media_clear_metatree(media_pipe_t *mp);

void mp_set_current_time(media_pipe_t *mp, int mts);

#endif /* MEDIA_H */
