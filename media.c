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

#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include "media.h"
#include "hid/input.h"
#include "showtime.h"
#include "audio/audio_decoder.h"

media_pipe_t *primary_audio;

static void
mq_mutex_init(pthread_mutex_t *mutex)
{
  pthread_mutex_init(mutex, NULL);
}

/*
 *
 */

static void
mq_init(media_queue_t *mq)
{
  TAILQ_INIT(&mq->mq_q);
  mq->mq_len = 0;
  mq->mq_stream = -1;
  pthread_cond_init(&mq->mq_avail, NULL);

}

/*
 *
 */

void
mp_init(media_pipe_t *mp, const char *name, struct appi *ai, int flags)
{
  mp->mp_flags = flags;
  mp->mp_name = name;
  mp->mp_ai = ai;
  mp->mp_info_widget_auto_display = 0;
  mp->mp_speed_gain = 1.0f;

  mq_mutex_init(&mp->mp_mutex);
  pthread_cond_init(&mp->mp_backpressure, NULL);
  
  mq_init(&mp->mp_audio);
  mq_init(&mp->mp_video);
}


/*
 *
 */

void
mp_deinit(media_pipe_t *mp)
{
  if(primary_audio == mp)
    primary_audio = NULL;

  mp_set_playstatus(mp, MP_STOP);
  if(mp->mp_info_widget != NULL)
    glw_destroy(mp->mp_info_widget);
  if(mp->mp_info_extra_widget != NULL)
    glw_destroy(mp->mp_info_extra_widget);
  mp_flush(mp, 1);
}


/*
 *
 */

media_buf_t *
mb_dequeue(media_pipe_t *mp, media_queue_t *mq)
{
  media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  mb = TAILQ_FIRST(&mq->mq_q);
  if(mb != NULL) {
    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_len--;
    pthread_cond_signal(&mp->mp_backpressure);
  }

  pthread_mutex_unlock(&mp->mp_mutex);
  return mb;
}

/*
 *
 */

media_buf_t *
mb_dequeue_wait(media_pipe_t *mp, media_queue_t *mq)
{
  media_buf_t *mb;
  pthread_mutex_lock(&mp->mp_mutex);

  while((mb = TAILQ_FIRST(&mq->mq_q)) == NULL ||
	mp->mp_playstatus == MP_PAUSE) {
    pthread_cond_wait(&mq->mq_avail, &mp->mp_mutex);
  }

  TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
  mq->mq_len--;
  pthread_cond_signal(&mp->mp_backpressure);
  pthread_mutex_unlock(&mp->mp_mutex);
  return mb;
}

/*
 *
 */

static void
mb_enq_tail(media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_TAIL(&mq->mq_q, mb, mb_link);
  mq->mq_len++;
  pthread_cond_signal(&mq->mq_avail);
}

/*
 *
 */

static void
mb_enq_head(media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_HEAD(&mq->mq_q, mb, mb_link);
  mq->mq_len++;
  pthread_cond_signal(&mq->mq_avail);
}


/*
 *
 */

void
mb_enqueue(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;

  pthread_mutex_lock(&mp->mp_mutex);
  
  if(a->mq_stream >= 0 && v->mq_stream >= 0) {
    while((a->mq_len > MQ_LOWWATER && v->mq_len > MQ_LOWWATER) ||
	  a->mq_len > MQ_HIWATER || v->mq_len > MQ_HIWATER) {
      pthread_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
    }
  } else {
    while(mq->mq_len > MQ_LOWWATER)
      pthread_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }


  mb_enq_tail(mq, mb);

  pthread_mutex_unlock(&mp->mp_mutex);
}

/*
 * Must be called with mp locked
 */

void
mq_flush(media_queue_t *mq)
{
  media_buf_t *mb;

  while((mb = TAILQ_FIRST(&mq->mq_q)) != NULL) {
    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    media_buf_free(mb);
  }
  mq->mq_len = 0;
}


/*
 *
 */

void
mp_flush(media_pipe_t *mp, int reset)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  printf("audio flush\n");
  mq_flush(a);
  printf("video flush\n");
  mq_flush(v);

  pthread_cond_signal(&mp->mp_backpressure);

  printf("sending termination commands\n");

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_cw = NULL;
    mb->mb_data = NULL;
    mb->mb_data_type = reset ? MB_RESET : MB_FLUSH;
    mb_enq_tail(v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_cw = NULL;
    mb->mb_data = NULL;
    mb->mb_data_type = reset ? MB_RESET : MB_FLUSH;
    mb_enq_tail(a, mb);
  }

  pthread_mutex_unlock(&mp->mp_mutex);

}

/*
 *
 */

void
mp_wait(media_pipe_t *mp, int audio, int video)
{
  while(1) {
    usleep(100000);
    if(audio && mp->mp_audio.mq_len > 0)
      continue;

    if(video && mp->mp_video.mq_len > 0)
      continue;
    break;
  }
}

/*
 *
 */

void
mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data = NULL;
  mb->mb_data_type = cmd;
  mb_enq_tail(mq, mb);
  pthread_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d)
{
 media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = d;
  mb_enq_tail(mq, mb);
  pthread_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = NULL;
  mb->mb_data32 = u;
  mb_enq_head(mq, mb);
  pthread_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  pthread_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = NULL;
  mb->mb_data32 = u;
  mb_enq_tail(mq, mb);
  pthread_mutex_unlock(&mp->mp_mutex);
}



/*
 *
 */

void
wrap_lock_all_codecs(formatwrap_t *fw)
{
  codecwrap_t *cw;

  LIST_FOREACH(cw, &fw->codecs, format_link)
    wrap_lock_codec(cw);
}


void
wrap_unlock_all_codecs(formatwrap_t *fw)
{
  codecwrap_t *cw;

  LIST_FOREACH(cw, &fw->codecs, format_link)
    wrap_unlock_codec(cw);
}


/*
 * Assumes we're already locked
 */

void
wrap_format_purge(formatwrap_t *fw)
{
  if(LIST_FIRST(&fw->codecs) != NULL) {
    pthread_mutex_unlock(&fw->mutex);
    return;
  }

  if(fw->refcount > 0) {
    pthread_mutex_unlock(&fw->mutex);
    return;
  }

  if(fw->format == NULL) {
    printf("Unabled format closed\n");
  } else {
    printf("Format %s closed\n", fw->format->iformat->name);
    av_close_input_file(fw->format);
  }
  pthread_mutex_unlock(&fw->mutex);
  free(fw);
}

codecwrap_t *
wrap_codec_ref(codecwrap_t *cw)
{
  cw->refcount++;
  return cw;
}





void
wrap_codec_deref(codecwrap_t *cw, int lock)
{
  formatwrap_t *fw;

  if(lock)
    pthread_mutex_lock(&cw->mutex);

  if(cw->refcount > 1) {
    cw->refcount--;
    pthread_mutex_unlock(&cw->mutex);
    return;
  }

  printf("Closing codec %s\n", cw->codec->name);

  fw = cw->format;

  if(fw != NULL) {
    pthread_mutex_lock(&fw->mutex);
    printf("\tdelinking from format %s\n", 
	   fw->format ? fw->format->iformat->name : "<anon>");
    LIST_REMOVE(cw, format_link);
  }

  fflock();

  avcodec_close(cw->codec_ctx);
  if(fw == NULL)
    free(cw->codec_ctx);

  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);
  
  ffunlock();

  if(fw != NULL)
    wrap_format_purge(fw);

  cw->codec = NULL;
  cw->codec_ctx = NULL;
  cw->parser_ctx = NULL;

  pthread_mutex_unlock(&cw->mutex); /* XXX: not really needed */

  if(pthread_mutex_destroy(&cw->mutex))
    perror("pthread_mutex_destroy");

  free(cw);
}


codecwrap_t *
wrap_codec_create(enum CodecID id, enum CodecType type, int parser,
		  formatwrap_t *fw, AVCodecContext *ctx)
{
  codecwrap_t *cw = malloc(sizeof(codecwrap_t));

  cw->codec = avcodec_find_decoder(id);
  if(cw->codec == NULL) {
    free(cw);
    return NULL;
  }
  
  cw->codec_ctx = ctx ?: avcodec_alloc_context();

  fflock();

  if(avcodec_open(cw->codec_ctx, cw->codec) < 0) {
    if(ctx == NULL)
      free(cw->codec_ctx);
    free(cw);
    ffunlock();
    return NULL;
  }

  cw->parser_ctx = parser ? av_parser_init(id) : NULL;

  mq_mutex_init(&cw->mutex);
  cw->refcount = 1;
  cw->format = fw;
  
  if(fw != NULL) 
    LIST_INSERT_HEAD(&fw->codecs, cw, format_link);

  if(type == CODEC_TYPE_VIDEO)
    avcodec_thread_init(cw->codec_ctx, 2);

  ffunlock();

  return cw;
}


formatwrap_t *
wrap_format_create(AVFormatContext *fctx, int refcount)
{
  formatwrap_t *fw = malloc(sizeof(formatwrap_t));

  mq_mutex_init(&fw->mutex);
  LIST_INIT(&fw->codecs);
  fw->format = fctx;
  fw->refcount = 1;
  return fw;
}


void
wrap_format_wait(formatwrap_t *fw)
{
  codecwrap_t *cw;
  
  pthread_mutex_lock(&fw->mutex);

  while((cw = LIST_FIRST(&fw->codecs)) != NULL) {
    printf("cw %s is still around (ref=%d)\n", cw->codec->name, cw->refcount);
    pthread_mutex_unlock(&fw->mutex);
    usleep(100000);
    pthread_mutex_lock(&fw->mutex);
  }

  fw->refcount--;
  wrap_format_purge(fw);
}

/*
 *
 */

void
mp_set_playstatus(media_pipe_t *mp, int status)
{
  if(mp->mp_playstatus == status)
    return;

  printf("%s -> status = %d\n", mp->mp_name, status);

  pthread_mutex_lock(&mp->mp_mutex);

  mp->mp_playstatus = status;
  pthread_cond_signal(&mp->mp_audio.mq_avail);
  pthread_cond_signal(&mp->mp_video.mq_avail);
  
  pthread_mutex_unlock(&mp->mp_mutex);
  
  if(mp->mp_playstatus_update_callback != NULL)
    mp->mp_playstatus_update_callback(mp);

  audio_decoder_change_play_status(mp);
}


/*
 *
 */

void
mp_playpause(struct media_pipe *mp, int key)
{
  int t;

  switch(key) {
  case INPUT_KEY_PLAYPAUSE:
    t = mp->mp_playstatus == MP_PLAY ? MP_PAUSE : MP_PLAY;
    break;
  case INPUT_KEY_PLAY:
    t = MP_PLAY;
    break;
  case INPUT_KEY_PAUSE:
    t = MP_PAUSE;
    break;
  default:
    return;
  }
  mp_auto_display(mp);
  mp_set_playstatus(mp, t);
}

void
media_pipe_acquire_audio(struct media_pipe *mp)
{
  if(mp->mp_playstatus == MP_PLAY)
    primary_audio = mp;
}


void
nice_codec_name(char *buf, int len, AVCodecContext *ctx)
{
  const char *fill = NULL;

  switch(ctx->codec_id) {
  case CODEC_ID_AC3:
    fill = "ac3";
    break;

  case CODEC_ID_MPEG2VIDEO:
    fill = "mpeg2";
    break;
  default:
    fill = ctx->codec->name;
    break;
  }
  snprintf(buf, len, "%s", fill);
}

