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

static hts_mutex_t media_mutex;

static prop_t *media_prop_root;
static prop_t *media_prop_sources;
static prop_t *media_prop_current;

static struct media_pipe_list media_pipe_stack;
media_pipe_t *media_primary;

static void seek_by_propchange(struct prop_sub *sub, prop_event_t event, ...);

static int media_event_handler(event_t *e, void *opaque);


/**
 *
 */
void
media_init(void)
{
  hts_mutex_init(&media_mutex);

  LIST_INIT(&media_pipe_stack);

  media_prop_root    = prop_create(prop_get_global(), "media");
  media_prop_sources = prop_create(media_prop_root, "sources");
  media_prop_current = prop_create(media_prop_root, "current");
  event_handler_register("currentmedia", media_event_handler,
			 EVENTPRI_CURRENT_MEDIA, NULL);
}


/**
 *
 */
void
media_buf_free(media_buf_t *mb)
{
  if(mb->mb_data != NULL)
    free(mb->mb_data);

  if(mb->mb_cw != NULL)
    wrap_codec_deref(mb->mb_cw);
  
  free(mb);
}



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
static void
mq_destroy(media_queue_t *mq)
{
  hts_cond_destroy(&mq->mq_avail);
}


/**
 *
 */
media_pipe_t *
mp_create(const char *name, const char *type)
{
  media_pipe_t *mp;

  mp = calloc(1, sizeof(media_pipe_t));
  
  TAILQ_INIT(&mp->mp_eq);

  mp->mp_refcount = 1;

  mp->mp_name = name;

  hts_mutex_init(&mp->mp_mutex);
  hts_mutex_init(&mp->mp_clock_mutex);
  hts_cond_init(&mp->mp_backpressure);
  
  mp->mp_prop_root = prop_create(media_prop_sources, NULL);

  if(type != NULL) {
    mp->mp_flags |= MP_PRIMABLE;
    prop_set_string(prop_create(mp->mp_prop_root, "type"), type);
  }

  mq_init(&mp->mp_audio, prop_create(mp->mp_prop_root, "audio"));
  mq_init(&mp->mp_video, prop_create(mp->mp_prop_root, "video"));

  mp->mp_prop_metadata    = prop_create(mp->mp_prop_root, "metadata");
  mp->mp_prop_playstatus  = prop_create(mp->mp_prop_root, "playstatus");
  mp->mp_prop_currenttime = prop_create(mp->mp_prop_root, "currenttime");
 
  mp->mp_pc = prop_courier_create(&mp->mp_mutex);

  mp->mp_sub_currenttime = prop_subscribe(NULL,
					  seek_by_propchange,
					  mp, mp->mp_pc,
					  PROP_SUB_NO_INITIAL_UPDATE,
					  mp->mp_prop_currenttime, NULL);
  return mp;
}


/**
 *
 */
static void
mp_destroy(media_pipe_t *mp)
{
  event_t *e;

  /* Make sure a clean shutdown has been made */
  assert(mp->mp_audio_decoder == NULL);
  assert(mp != media_primary);
  assert(mp->mp_flags == 0);

  prop_unsubscribe(mp->mp_sub_currenttime);

  prop_courier_destroy(mp->mp_pc);

  hts_mutex_lock(&mp->mp_mutex);
  while((e = TAILQ_FIRST(&mp->mp_eq)) != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
    event_unref(e);
  }
  hts_mutex_unlock(&mp->mp_mutex);

  mq_destroy(&mp->mp_audio);
  mq_destroy(&mp->mp_video);

  prop_destroy(mp->mp_prop_root);

  hts_cond_destroy(&mp->mp_backpressure);
  hts_mutex_destroy(&mp->mp_mutex);
  hts_mutex_destroy(&mp->mp_clock_mutex);

  free(mp);
}


/**
 *
 */
void
mp_ref_dec(media_pipe_t *mp)
{
  if(atomic_add(&mp->mp_refcount, -1) == 1)
    mp_destroy(mp);
}


/**
 *
 */
static void
mp_enqueue_event_locked(media_pipe_t *mp, event_t *e)
{
  atomic_add(&e->e_refcount, 1);
  TAILQ_INSERT_TAIL(&mp->mp_eq, e, e_link);
  hts_cond_signal(&mp->mp_backpressure);
}

/**
 *
 */
void
mp_enqueue_event(media_pipe_t *mp, event_t *e)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_enqueue_event_locked(mp, e);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
event_t *
mp_dequeue_event(media_pipe_t *mp)
{
  event_t *e;
  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL)
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);

  TAILQ_REMOVE(&mp->mp_eq, e, e_link);
  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}


/**
 *
 */
event_t *
mp_dequeue_event_deadline(media_pipe_t *mp, time_t deadline)
{
  event_t *e;
  struct timespec ts;

  ts.tv_sec = deadline;
  ts.tv_nsec = 0;

  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL) {
    if(hts_cond_wait_timeout(&mp->mp_backpressure, 
			     &mp->mp_mutex, &ts) == ETIMEDOUT)
      break;
  }
  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}


/**
 *
 */
event_t *
mp_wait_for_empty_queues(media_pipe_t *mp)
{
  event_t *e;
  hts_mutex_lock(&mp->mp_mutex);

  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL &&
	(mp->mp_audio.mq_len > 0 || mp->mp_video.mq_len > 0))
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);

  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}



/**
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

/**
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

/**
 *
 */
event_t *
mb_enqueue_with_events(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  event_t *e = NULL;

  hts_mutex_lock(&mp->mp_mutex);

  if(a->mq_stream >= 0 && v->mq_stream >= 0) {
    while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL &&
	  ((a->mq_len > MQ_LOWWATER && v->mq_len > MQ_LOWWATER) ||
	   a->mq_len > MQ_HIWATER || v->mq_len > MQ_HIWATER)) {
      hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
    }
  } else {
    while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL && mq->mq_len > MQ_LOWWATER)
      hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }

  if(e != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
    hts_mutex_unlock(&mp->mp_mutex);
    return e;
  }

  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
  return NULL;
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


/**
 *
 */
void
mb_enqueue_always(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  hts_mutex_lock(&mp->mp_mutex);
  mb_enq_tail(mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
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

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_data_type = MB_FLUSH;
    mb_enq_tail(v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc();
    mb->mb_data_type = MB_FLUSH;
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
mp_send_cmd_head(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  media_buf_t *mb;

  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc();
  mb->mb_cw = NULL;
  mb->mb_data = NULL;
  mb->mb_data_type = cmd;
  mb_enq_head(mq, mb);
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
  formatwrap_t *fw = cw->fw;

  if(atomic_add(&cw->refcount, -1) > 1)
    return;

  fflock();

  avcodec_close(cw->codec_ctx);

  if(fw == NULL)
    free(cw->codec_ctx);

  if(cw->parser_ctx != NULL)
    av_parser_close(cw->parser_ctx);
  
  ffunlock();

  if(fw != NULL)
    wrap_format_deref(fw);

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
  extern int concurrency;
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
  cw->fw = fw;
  
  if(fw != NULL)
    atomic_add(&fw->refcount, 1);

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
  fw->refcount = 1;
  fw->fctx = fctx;
  return fw;
}


/**
 *
 */
void
wrap_format_deref(formatwrap_t *fw)
{
  if(atomic_add(&fw->refcount, -1) > 1)
    return;
  av_close_input_file(fw->fctx);
  free(fw);
}



/**
 *
 */
int
mp_update_hold_by_event(int hold, event_type_t et)
{
  switch(et) {
  case EVENT_PLAYPAUSE:
    return !hold;
  case EVENT_PAUSE:
    return 1;
  case EVENT_PLAY:
    return 0;
  default:
    abort();
  }
}


/**
 * 
 */
static void
mp_set_primary(media_pipe_t *mp)
{
  media_primary = mp;
  mp_enqueue_event(mp, event_create_simple(EVENT_MP_IS_PRIMARY));

  prop_select(mp->mp_prop_root, 0);
}


/**
 * 
 */
void
mp_become_primary(struct media_pipe *mp)
{
  if(mp->mp_audio_decoder == NULL)
    mp->mp_audio_decoder = audio_decoder_create(mp);
    
  if(media_primary == mp)
    return;

  hts_mutex_lock(&media_mutex);

  assert(mp->mp_flags & MP_PRIMABLE);

  if(media_primary != NULL) {
    
    LIST_INSERT_HEAD(&media_pipe_stack, media_primary, mp_stack_link);
    media_primary->mp_flags |= MP_ON_STACK;

    mp_enqueue_event(media_primary, 
		     event_create_simple(EVENT_MP_NO_LONGER_PRIMARY));
  }

  mp_ref_inc(mp);
  mp_set_primary(mp);

  hts_mutex_unlock(&media_mutex);
}


/**
 *
 */
void
mp_shutdown(struct media_pipe *mp)
{
  if(mp->mp_audio_decoder != NULL) {
    audio_decoder_destroy(mp->mp_audio_decoder);
    mp->mp_audio_decoder = NULL;
  }

  hts_mutex_lock(&media_mutex);

  assert(mp->mp_flags & MP_PRIMABLE);
  mp->mp_flags &= ~MP_PRIMABLE;

  if(media_primary == mp) {
    /* We were primary */

    media_primary = NULL;
    mp_ref_dec(mp); // mp could be free'd here */

    /* Anyone waiting to regain playback focus? */
    if((mp = LIST_FIRST(&media_pipe_stack)) != NULL) {

      assert(mp->mp_flags & MP_ON_STACK);
      LIST_REMOVE(mp, mp_stack_link);
      mp->mp_flags &= ~MP_ON_STACK;
      mp_set_primary(mp);
    }

  } else {

    // We must be on the stack
    assert(mp->mp_flags & MP_ON_STACK);
    LIST_REMOVE(mp, mp_stack_link);
    mp->mp_flags &= ~MP_ON_STACK;

    mp_ref_dec(mp); // mp could be free'd here */
  }
  hts_mutex_unlock(&media_mutex);
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
static void
seek_by_propchange(struct prop_sub *sub, prop_event_t event, ...)
{
  event_seek_t *es;
  event_t *e;
  media_pipe_t *mp = sub->hps_opaque;
  int64_t t;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_INT:
    t = va_arg(ap, int) * 1000000LL;
    break;
  case PROP_SET_FLOAT:
    t = va_arg(ap, double) * 1000000.0;
    break;
  default:
    return;
  }

  /* If there already is a seek event enqueued, update it */
  TAILQ_FOREACH(e, &mp->mp_eq, e_link) {
    if(e->e_type != EVENT_SEEK)
      continue;

    es = (event_seek_t *)e;
    es->ts = t;
    return;
  }

  es = event_create(EVENT_SEEK, sizeof(event_seek_t));
  es->ts = t;
  mp_enqueue_event_locked(mp, &es->h);
  event_unref(&es->h);
}

/**
 *
 */
void
mp_set_current_time(media_pipe_t *mp, int64_t pts)
{
  double d = pts / 1000000.0;
  prop_set_float_ex(mp->mp_prop_currenttime, mp->mp_sub_currenttime, d);
}


/**
 *
 */
static int
media_event_handler(event_t *e, void *opaque)
{
  if(media_primary == NULL)
    return 0;
  
  switch(e->e_type) {

  case EVENT_SEEK_FAST_BACKWARD:
  case EVENT_SEEK_BACKWARD:
  case EVENT_SEEK_FAST_FORWARD:
  case EVENT_SEEK_FORWARD:
  case EVENT_PLAYPAUSE:
  case EVENT_PLAY:
  case EVENT_PAUSE:
  case EVENT_STOP:
  case EVENT_PREV:
  case EVENT_NEXT:
  case EVENT_RESTART_TRACK:
    break;
  default:
    return 0;
  }

  mp_enqueue_event(media_primary, e);
  return 1;
}
