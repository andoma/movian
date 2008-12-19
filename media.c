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

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include "media.h"
#include "showtime.h"
#include "audio/audio_decoder.h"
#include "event.h"
#include "fileaccess/fileaccess.h"


extern int concurrency;


/**
 *
 */
static void
mq_init(media_queue_t *mq, prop_t *p)
{
  TAILQ_INIT(&mq->mq_q);
  mq->mq_len = 0;
  mq->mq_stream = -1;
  hts_cond_init(&mq->mq_avail);
  mq->mq_prop_qlen_cur = prop_create(p, "qlen");
  mq->mq_prop_qlen_max = prop_create(p, "qmax");
}


/**
 *
 */
media_pipe_t *
mp_create(const char *name)
{
  media_pipe_t *mp;

  mp = calloc(1, sizeof(media_pipe_t));
  
  mp->mp_refcount = 1;

  mp->mp_name = name;
  mp->mp_speed_gain = 1.0f;

  hts_mutex_init(&mp->mp_mutex);
  hts_cond_init(&mp->mp_backpressure);
  
  mp->mp_prop_root = prop_create(NULL, "media");

  mq_init(&mp->mp_audio, prop_create(mp->mp_prop_root, "audio"));
  mq_init(&mp->mp_video, prop_create(mp->mp_prop_root, "video"));

  mp->mp_prop_meta        = prop_create(mp->mp_prop_root, "meta");
  mp->mp_prop_playstatus  = prop_create(mp->mp_prop_root, "playstatus");
  mp->mp_prop_currenttime = prop_create(mp->mp_prop_root, "currenttime");
 
  return mp;
}


/**
 *
 */
static void
mp_destroy(media_pipe_t *mp)
{
  mp_set_playstatus(mp, MP_STOP, 0);
  prop_destroy(mp->mp_prop_root);
  free(mp);
}


/**
 *
 */
void
mp_unref(media_pipe_t *mp)
{
  if(atomic_add(&mp->mp_refcount, -1) == 0)
    mp_destroy(mp);
}


#if 0
/*
 *
 */

media_buf_t *
mb_dequeue_wait(media_pipe_t *mp, media_queue_t *mq)
{
  media_buf_t *mb;
  hts_mutex_lock(&mp->mp_mutex);

  while(1) {
    if(mp->mp_playstatus == MP_STOP) {
      hts_mutex_unlock(&mp->mp_mutex);
      return NULL;
    }

    if(mp->mp_playstatus >= MP_PLAY && (mb = TAILQ_FIRST(&mq->mq_q)) != NULL)
      break;
    hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
  }

  TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
  mq->mq_len--;
  hts_cond_signal(&mp->mp_backpressure);
  hts_mutex_unlock(&mp->mp_mutex);
  return mb;
}
#endif

/*
 *
 */

static void
mb_enq_tail(media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_TAIL(&mq->mq_q, mb, mb_link);
  mq->mq_len++;
  hts_cond_signal(&mq->mq_avail);
  prop_set_int(mq->mq_prop_qlen_cur, mq->mq_len);
}

/*
 *
 */

static void
mb_enq_head(media_queue_t *mq, media_buf_t *mb)
{
  TAILQ_INSERT_HEAD(&mq->mq_q, mb, mb_link);
  mq->mq_len++;
  hts_cond_signal(&mq->mq_avail);
  prop_set_int(mq->mq_prop_qlen_cur, mq->mq_len);
}


/*
 *
 */

void
mb_enqueue(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;

  hts_mutex_lock(&mp->mp_mutex);
  
  if(a->mq_stream >= 0 && v->mq_stream >= 0) {
    while(mp->mp_playstatus >= MP_PLAY &&
	  ((a->mq_len > MQ_LOWWATER && v->mq_len > MQ_LOWWATER) ||
	   a->mq_len > MQ_HIWATER || v->mq_len > MQ_HIWATER)) {
      hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
    }
  } else {
    while(mq->mq_len > MQ_LOWWATER && mp->mp_playstatus >= MP_PLAY)
      hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }
  
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 * Return -1 if queues are full. return 0 if enqueue succeeded.
 */
int
mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;

  hts_mutex_lock(&mp->mp_mutex);
  
  if(a->mq_stream >= 0 && v->mq_stream >= 0) {

    if((a->mq_len > MQ_LOWWATER && v->mq_len > MQ_LOWWATER) ||
       a->mq_len > MQ_HIWATER || v->mq_len > MQ_HIWATER) {
      hts_mutex_unlock(&mp->mp_mutex);
      return -1;
    }

  } else {

    if(mq->mq_len > MQ_LOWWATER) {
      hts_mutex_unlock(&mp->mp_mutex);
      return -1;
    }
  }
  
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
  return 0;
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
mp_flush(media_pipe_t *mp)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mq_flush(a);
  mq_flush(v);

  hts_cond_signal(&mp->mp_backpressure);

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_data_type = MB_RESET;
    mb_enq_tail(v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_data_type = MB_RESET;
    mb_enq_tail(a, mb);
  }

  hts_mutex_unlock(&mp->mp_mutex);

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

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data = NULL;
  mb->mb_data_type = cmd;
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_data(media_pipe_t *mp, media_queue_t *mq, int cmd, void *d)
{
 media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = d;
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_u32_head(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = NULL;
  mb->mb_data32 = u;
  mb_enq_head(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}

/*
 *
 */

void
mp_send_cmd_u32(media_pipe_t *mp, media_queue_t *mq, int cmd, uint32_t u)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data_type = cmd;
  mb->mb_data = NULL;
  mb->mb_data32 = u;
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
codecwrap_t *
wrap_codec_ref(codecwrap_t *cw)
{
  atomic_add(&cw->refcount, 1);
  return cw;
}

/**
 *
 */
void
wrap_codec_deref(codecwrap_t *cw)
{
  formatwrap_t *fw = cw->format;

  if(atomic_add(&cw->refcount, -1) > 1)
    return;

  fflock();

  avcodec_close(cw->codec_ctx);
  if(fw == NULL)
    free(cw->codec_ctx);

  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);
  
  ffunlock();

  if((fw = cw->format) != NULL) {
    hts_mutex_lock(&fw->fw_mutex);
    LIST_REMOVE(cw, format_link);
    hts_cond_signal(&fw->fw_cond);
    hts_mutex_unlock(&fw->fw_mutex);
  }
  free(cw);
}


/**
 *
 */
codecwrap_t *
wrap_codec_create(enum CodecID id, enum CodecType type, int parser,
		  formatwrap_t *fw, AVCodecContext *ctx,
		  int cheat_for_speed)
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

  cw->refcount = 1;
  cw->format = fw;
  
  if(fw != NULL) {
    hts_mutex_lock(&fw->fw_mutex);
    LIST_INSERT_HEAD(&fw->codecs, cw, format_link);
    hts_mutex_unlock(&fw->fw_mutex);
  }

  if(type == CODEC_TYPE_VIDEO && concurrency > 1) {
    avcodec_thread_init(cw->codec_ctx, concurrency);
    
    if(cheat_for_speed)
      cw->codec_ctx->flags2 |= CODEC_FLAG2_FAST;
  }

  ffunlock();

  return cw;
}

/**
 *
 */
formatwrap_t *
wrap_format_create(AVFormatContext *fctx)
{
  formatwrap_t *fw = malloc(sizeof(formatwrap_t));

  hts_mutex_init(&fw->fw_mutex);
  hts_cond_init(&fw->fw_cond);
  LIST_INIT(&fw->codecs);
  fw->format = fctx;
  return fw;
}


/**
 *
 */
void
wrap_format_destroy(formatwrap_t *fw)
{
  /* Wait for all codecs to be released */
  hts_mutex_lock(&fw->fw_mutex);
  while((LIST_FIRST(&fw->codecs)) != NULL)
    hts_cond_wait(&fw->fw_cond, &fw->fw_mutex);
  hts_mutex_unlock(&fw->fw_mutex);

  if(fw->format != NULL)
    av_close_input_file(fw->format);
  free(fw);
}


/**
 * mp_set_playstatus() is responsible for starting and stopping
 * decoder threads
 */
void
mp_set_playstatus(media_pipe_t *mp, int status, int flags)
{
  if(mp->mp_playstatus == status)
    return;

  switch(status) {

  case MP_PLAY:
  case MP_VIDEOSEEK_PLAY:
  case MP_VIDEOSEEK_PAUSE:
  case MP_PAUSE:

    hts_mutex_lock(&mp->mp_mutex);
    mp->mp_playstatus = status;
    hts_cond_signal(&mp->mp_backpressure);
    hts_cond_signal(&mp->mp_audio.mq_avail);
    hts_cond_signal(&mp->mp_video.mq_avail);
    hts_mutex_unlock(&mp->mp_mutex);

    if(mp->mp_audio_decoder == NULL)
      mp->mp_audio_decoder = audio_decoder_create(mp);

    if(status == MP_PLAY && !(flags & MP_DONT_GRAB_AUDIO))
      audio_decoder_acquire_output(mp->mp_audio_decoder);
    break;

    
  case MP_STOP:

    /* Lock queues */

    hts_mutex_lock(&mp->mp_mutex);

    /* Flush all media in queues */

    mq_flush(&mp->mp_audio);
    mq_flush(&mp->mp_video);

    /* set playstatus to STOP and signal on conditioners,
       this will make mb_dequeue_wait return NULL next time it returns */
    
    mp->mp_playstatus = MP_STOP;
    hts_cond_signal(&mp->mp_audio.mq_avail);
    hts_cond_signal(&mp->mp_video.mq_avail);
    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

    /* We should now be able to collect the threads */

    if(mp->mp_audio_decoder != NULL) {
      audio_decoder_destroy(mp->mp_audio_decoder);
      mp->mp_audio_decoder = NULL;
    }

    prop_set_void(mp->mp_prop_currenttime);
    media_update_playstatus_prop(mp->mp_prop_playstatus, MP_STOP);
    break;
  }

}





/*
 *
 */

void
mp_playpause(struct media_pipe *mp, int key)
{
  int t;

  switch(key) {
  case EVENT_KEY_PLAYPAUSE:
    t = mp->mp_playstatus == MP_PLAY ? MP_PAUSE : MP_PLAY;
    break;
  case EVENT_KEY_PLAY:
    t = MP_PLAY;
    break;
  case EVENT_KEY_PAUSE:
    t = MP_PAUSE;
    break;
  default:
    return;
  }
  mp_set_playstatus(mp, t, 0);
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



/**
 *
 */
int
mp_is_audio_silenced(media_pipe_t *mp)
{
  return mp->mp_audio_decoder &&
    audio_decoder_is_silenced(mp->mp_audio_decoder);
}


/**
 *
 */
static void
codec_details(AVCodecContext *ctx, char *buf, size_t size, const char *lead)
{
  const char *cfg;

  if(ctx->codec_type == CODEC_TYPE_AUDIO) {

    if(ctx->sample_rate % 1000 == 0) {
      snprintf(buf, size, "%s%d kHz", lead, ctx->sample_rate / 1000);
    } else {
      snprintf(buf, size, "%s%.1f kHz", lead, (float)ctx->sample_rate / 1000);
    }
    lead = ", ";

    switch(ctx->channels) {
    case 1: 
      cfg = "mono";
      break;
    case 2: 
      cfg = "stereo";
      break;
    case 6: 
      cfg = "5.1";
      break;
    default:
      snprintf(buf + strlen(buf), size - strlen(buf), ", %d channels",
	       ctx->channels);
      cfg = NULL;
      break;
    }
    if(cfg != NULL) {
      snprintf(buf + strlen(buf), size - strlen(buf), ", %s", cfg);
    }
  }

  if(ctx->width) {
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%dx%d", lead, ctx->width, ctx->height);
    lead = ", ";
  }

  if(ctx->bit_rate > 2000000)
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%.1f Mb/s", lead, (float)ctx->bit_rate / 1000000);
  else if(ctx->bit_rate)
    snprintf(buf + strlen(buf), size - strlen(buf),
	     "%s%d kb/s", lead, ctx->bit_rate / 1000);
}

/**
 * Update codec info in property
 */ 
void
media_update_codec_info_prop(prop_t *p, AVCodecContext *ctx)
{
  char tmp[100];

  if(ctx == NULL) {
    tmp[0] = 0;
  } else {
    snprintf(tmp, sizeof(tmp), "%s", ctx->codec->long_name);
    codec_details(ctx, tmp + strlen(tmp), sizeof(tmp) - strlen(tmp), ", ");
  }
  prop_set_string(p, tmp);
}



/**
 *
 */
void
media_update_playstatus_prop(prop_t *p, mp_playstatus_t mps)
{
  const char *s;

  switch(mps) {
  case MP_PLAY:
    s = "play";
    break;

  case MP_PAUSE:
    s = "pause";
    break;

  case MP_VIDEOSEEK_PLAY:
  case MP_VIDEOSEEK_PAUSE:
    s = "seek";
    break;

  default:
    s = "stop";
    break;
  }
  prop_set_string(p, s);
}



/**
 * Update codec info in text widgets
 */ 
void
media_get_codec_info(AVCodecContext *ctx, char *buf, size_t size)
{
  snprintf(buf, size, "%s\n", ctx->codec->long_name);
  codec_details(ctx, buf + strlen(buf), size - strlen(buf), "");
}


/**
 *
 */
void
media_set_currentmedia(media_pipe_t *mp)
{
  prop_t *p;
  static media_pipe_t *lastmp;

  if(lastmp == mp)
    return;

  lastmp = mp;
  p = prop_create(prop_get_global(), "currentmedia");
  prop_link(mp->mp_prop_root, p);

  p = prop_create(prop_get_global(), "currentmediasource");
  prop_set_string(p, mp->mp_name);
}


/**
 *
 */
void
media_set_metatree(media_pipe_t *mp, prop_t *src)
{
  //  abort(); //hts_prop_linktree(src, mp->mp_prop_meta);
}


/**
 *
 */
void
media_clear_metatree(media_pipe_t *mp)
{
  //  abort(); //hts_prop_unlink(mp->mp_prop_meta);
}
