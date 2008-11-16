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
#include <sys/queue.h>
#include <libavformat/avformat.h>
#include "ui/glw/glw.h"

typedef struct event_ts {
  glw_event_t h;
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
    MB_RESET,
    MB_VIDEO,
    MB_CLUT,
    MB_RESET_SPU,
    MB_DISCONT,
    MB_DVD_SPU,
    MB_DVD_PCI,
    MB_DVD_HILITE,
    MB_DVD_NAVKEY,
    MB_AUDIO,
  } mb_data_type;

  void *mb_data;
  int mb_size;

  int mb_data32;

  uint32_t mb_duration;
  uint32_t mb_time;           /* in seconds, of current track */
  uint32_t mb_aspect_override;
  int64_t mb_dts;
  int64_t mb_pts;

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




/*
 * Media pipe
 */

typedef enum {
  MP_STOP,
  MP_PAUSE,           /* Freeze playback */
  MP_PLAY,
  MP_VIDEOSEEK_PLAY,  /* Audio == silent
		       * Video == decode until dts >= videoseekdts,
		       *          then display that frame and goto MP_PLAY
		       */
  MP_VIDEOSEEK_PAUSE, /* Same as above, but goto PAUSE */
} mp_playstatus_t;

typedef struct media_pipe {
  int mp_refcount;

  mp_playstatus_t mp_playstatus;

  const char *mp_name;

  hts_mutex_t mp_mutex;

  hts_cond_t mp_backpressure;

  media_queue_t mp_video, mp_audio;
  
  int64_t mp_audio_clock;
  int mp_audio_clock_valid;

  LIST_ENTRY(media_pipe) mp_asched_link;

  int mp_total_time;

  AVFormatContext *mp_format;

  float mp_speed_gain;

  struct subtitles *mp_subtitles;

  struct audio_decoder *mp_audio_decoder;

  struct video_decoder *mp_video_decoder;
  struct vd_conf *mp_video_conf;

  struct glw_event_queue *mp_feedback;

  int64_t mp_videoseekdts;

  /* Props */

  prop_t *mp_prop_root;
  prop_t *mp_prop_meta;
  prop_t *mp_prop_playstatus;
  prop_t *mp_prop_currenttime;

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


static inline media_buf_t *
media_buf_alloc(void)
{
  media_buf_t *mb = calloc(1, sizeof(media_buf_t));
  mb->mb_time = -1;
  return mb;
}

static inline void
media_buf_free(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    free(mb->mb_data);

  if(mb->mb_cw != NULL)
    wrap_codec_deref(mb->mb_cw);
  
  free(mb);
}


media_pipe_t *mp_create(const char *name);
media_pipe_t *mp_ref(media_pipe_t *mp);
void mp_unref(media_pipe_t *mp);

void mq_flush(media_queue_t *mq);

struct vd_conf;
void mp_set_video_conf(media_pipe_t *mp, struct vd_conf *vdc);

media_buf_t *mb_dequeue_wait(media_pipe_t *hmp, media_queue_t *hmq);
void mb_enqueue(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);
int mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);
void mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d);
void mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, 
			  uint32_t u);

void mp_flush(media_pipe_t *mp);


void mp_wait(media_pipe_t *mp, int audio, int video);

void mp_codec_lock(media_pipe_t *mp);

void mp_codec_unlock(media_pipe_t *mp);

void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u);

#define MP_DONT_GRAB_AUDIO 0x1
void mp_set_playstatus(media_pipe_t *mp, int status, int flags);

#define mp_get_playstatus(mp) ((mp)->mp_playstatus)

static inline int 
mp_is_paused(struct media_pipe *mp)
{
  return mp->mp_playstatus == MP_PAUSE;
}

void mp_playpause(struct media_pipe *mp, int key);

void media_pipe_acquire_audio(struct media_pipe *mp);

void nice_codec_name(char *buf, int len, AVCodecContext *ctx);

int mp_is_audio_silenced(media_pipe_t *mp);

void media_update_codec_info_prop(prop_t *p, AVCodecContext *ctx);

void media_update_playstatus_prop(prop_t *p, mp_playstatus_t mps);

void media_get_codec_info(AVCodecContext *ctx, char *buf, size_t size);

extern media_pipe_t *primary_audio;
struct filetag_list;

void media_fill_properties(prop_t *root, const char *url, int type,
			   struct filetag_list *tags);

void media_set_currentmedia(media_pipe_t *mp);
void media_set_metatree(media_pipe_t *mp, prop_t *src);
void media_clear_metatree(media_pipe_t *mp);

#endif /* MEDIA_H */
