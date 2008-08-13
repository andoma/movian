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
#include <libglw/glw.h>
#include <libavformat/avformat.h>

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
  hts_mutex_t mutex;
  AVFormatContext *format;
  LIST_HEAD(, codecwrap) codecs;
  int refcount;                   /* this does not include the codecs */
} formatwrap_t;



typedef struct codecwrap {
  hts_mutex_t mutex;
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

  /* informational stuff */

  char  mq_info_codec[30];
  int   mq_info_rate;               /* in kbps */
  char  mq_info_output_type[50];

  time_t mq_info_last_time;
  int   mq_info_rate_acc;

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

  hts_mutex_t mp_ref_mutex;
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

  struct appi *mp_ai;

  AVFormatContext *mp_format;

  float mp_speed_gain;

  struct subtitles *mp_subtitles;

  struct audio_decoder *mp_audio_decoder;

  struct video_decoder *mp_video_decoder;
  struct vd_conf *mp_video_conf;

  struct glw_event_queue *mp_feedback;

  int64_t mp_videoseekdts;

  glw_t *mp_status_xfader;

} media_pipe_t;


/*
 * ffmpeg lock wrappers
 */


static inline void wrap_lock_codec(codecwrap_t *cw);

static inline void
wrap_lock_codec(codecwrap_t *cw)
{
  hts_mutex_lock(&cw->mutex);
}


static inline void wrap_unlock_codec(codecwrap_t *cw);

static inline void
wrap_unlock_codec(codecwrap_t *cw)
{
  hts_mutex_unlock(&cw->mutex);
}

void wrap_codec_deref(codecwrap_t *cw, int lock);
codecwrap_t *wrap_codec_ref(codecwrap_t *cw);
codecwrap_t *wrap_codec_create(enum CodecID id, enum CodecType type, 
			       int parser, formatwrap_t *fw,
			       AVCodecContext *ctx);
formatwrap_t *wrap_format_create(AVFormatContext *fctx, int refcount);
void wrap_lock_all_codecs(formatwrap_t *fw);
void wrap_unlock_all_codecs(formatwrap_t *fw);
void wrap_format_purge(formatwrap_t *fw);
void wrap_format_wait(formatwrap_t *fw);


static inline media_buf_t *
media_buf_alloc(void)
{
  return calloc(1, sizeof(media_buf_t));
}

static inline void
media_buf_free(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    free(mb->mb_data);

  if(mb->mb_cw != NULL)
    wrap_codec_deref(mb->mb_cw, 1);
  
  free(mb);
}


media_pipe_t *mp_create(const char *name, struct appi *ai);
media_pipe_t *mp_ref(media_pipe_t *mp);
void mp_unref(media_pipe_t *mp);

void mq_flush(media_queue_t *mq);

struct vd_conf;
void mp_set_video_conf(media_pipe_t *mp, struct vd_conf *vdc);

media_buf_t *mb_dequeue_wait(media_pipe_t *hmp, media_queue_t *hmq);
void mb_enqueue(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb);
void mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd);
void mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d);
void mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, 
			  uint32_t u);

void mp_flush(media_pipe_t *mp);


void mp_wait(media_pipe_t *mp, int audio, int video);

void mp_codec_lock(media_pipe_t *mp);

void mp_codec_unlock(media_pipe_t *mp);

void mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u);

void mp_set_playstatus(media_pipe_t *mp, int status);

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

void media_update_codec_info_widget(glw_t *w, const char *id, 
				    AVCodecContext *ctx);

void media_get_codec_info(AVCodecContext *ctx, char *buf, size_t size);

extern media_pipe_t *primary_audio;
struct filetag_list;

void media_fill_properties(glw_prop_t *root, const char *url, int type,
			   struct filetag_list *tags);

#endif /* MEDIA_H */
