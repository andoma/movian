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
#include <math.h>

#include "media.h"

#include "misc/minmax.h"

/**
 *
 */
static void
mq_flush_q(media_pipe_t *mp, media_queue_t *mq, struct media_buf_queue *q,
	   int full)
{
  media_buf_t *mb, *next;

  for(mb = TAILQ_FIRST(q); mb != NULL; mb = next) {
    next = TAILQ_NEXT(mb, mb_link);

    if(mb->mb_data_type == MB_CTRL_EXIT)
      continue;
    if(mb->mb_data_type == MB_CTRL_UNBLOCK && !full)
      continue;
    TAILQ_REMOVE(q, mb, mb_link);
    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb_buffered_size(mb);
    media_buf_free_locked(mp, mb);
  }
}

/**
 * Must be called with mp locked
 */
static void
mq_flush_locked(media_pipe_t *mp, media_queue_t *mq, int full)
{
  mq->mq_last_deq_dts = PTS_UNSET;
  mq_flush_q(mp, mq, &mq->mq_q_data, full);
  mq_flush_q(mp, mq, &mq->mq_q_ctrl, full);
  mq_flush_q(mp, mq, &mq->mq_q_aux, full);
  mq_update_stats(mp, mq, 1);
}


/**
 * Must be called with mp locked
 */
void
mq_flush(media_pipe_t *mp, media_queue_t *mq, int full)
{
  hts_mutex_lock(&mp->mp_mutex);
  mq_flush_locked(mp, mq, full);
  mp_check_underrun(mp);
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
void
mp_flush_locked(media_pipe_t *mp, int final)
{
  media_queue_t *v = &mp->mp_video;
  media_queue_t *a = &mp->mp_audio;
  media_buf_t *mb;

  mq_flush_locked(mp, a, 0);
  mq_flush_locked(mp, v, 0);

  mp->mp_epoch++;

  if(v->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_CTRL_FLUSH;
    mb->mb_data32 = final;
    mb_enq(mp, v, mb);
  }

  if(a->mq_stream >= 0) {
    mb = media_buf_alloc_locked(mp, 0);
    mb->mb_data_type = MB_CTRL_FLUSH;
    mb->mb_data32 = final;
    mb_enq(mp, a, mb);
  }

  if(mp->mp_satisfied == 0) {
    atomic_dec(&media_buffer_hungry);
    mp->mp_satisfied = 1;
  }
}


/**
 *
 */
void
mp_flush(media_pipe_t *mp)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_flush_locked(mp, 0);
  hts_mutex_unlock(&mp->mp_mutex);
}


static int
mq_get_buffer_delay(media_queue_t *mq)
{
  media_buf_t *f, *l;

  if(mq->mq_stream == -1)
    return 0;

  f = TAILQ_FIRST(&mq->mq_q_data);
  l = TAILQ_LAST(&mq->mq_q_data, media_buf_queue);

  if(f == NULL) {
    mq->mq_buffer_delay = 0;
    return 0;
  }

  int cnt;

  cnt = 20;

  while(f && f->mb_dts == PTS_UNSET && cnt > 0) {
    f = TAILQ_NEXT(f, mb_link);
    cnt--;
  }

  cnt = 20;
  while(l && l->mb_dts == PTS_UNSET && cnt > 0) {
    l = TAILQ_PREV(l, media_buf_queue, mb_link);
    cnt--;
  }

  if(f != NULL && l != NULL && f->mb_epoch == l->mb_epoch &&
     l->mb_dts != PTS_UNSET && f->mb_dts != PTS_UNSET) {
    mq->mq_buffer_delay = l->mb_dts - f->mb_dts;
    if(mq->mq_buffer_delay < 0)
      mq->mq_buffer_delay = 0;
  }

  return mq->mq_buffer_delay;
}


/**
 *
 */
void
mp_update_buffer_delay(media_pipe_t *mp)
{
  int vd = mq_get_buffer_delay(&mp->mp_video);
  int ad = mq_get_buffer_delay(&mp->mp_audio);

  mp->mp_buffer_delay = MAX(vd, ad);
}


/**
 * If we're in pre-buffering state and we have enough data, release hold
 */
static void
mp_enqueue_check_pre_buffering(media_pipe_t *mp)
{
  if(unlikely(mp->mp_hold_flags & MP_HOLD_PRE_BUFFERING)) {
    if(mp->mp_buffer_delay > mp->mp_pre_buffer_delay) {
      mp->mp_hold_flags &= ~MP_HOLD_PRE_BUFFERING;
      mp_set_playstatus_by_hold_locked(mp, NULL);
    }
  }
}


/**
 *
 */
event_t *
mb_enqueue_with_events(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  event_t *e = NULL;

  hts_mutex_lock(&mp->mp_mutex);
#if 0
  printf("ENQ %s %d %d/%d %d/%d\n",
         mq == &mp->mp_video ? "video" : "audio",
	 mq->mq_packets_current,
	 mp->mp_buffer_current,  mp->mp_buffer_limit,
         (int)mp->mp_buffer_delay, (int)mp->mp_max_realtime_delay);
#endif

  const int vminpkt = mp->mp_video.mq_stream != -1 ? 5 : 0;
  const int aminpkt = mp->mp_audio.mq_stream != -1 ? 5 : 0;

  mp_update_buffer_delay(mp);
  mp_enqueue_check_pre_buffering(mp);

  while(1) {

    e = TAILQ_FIRST(&mp->mp_eq);
    if(e != NULL)
      break;

    // Check if we are inside the realtime delay bounds
    if(mp->mp_buffer_delay < mp->mp_max_realtime_delay) {

      // Check if buffer is full
      if(mp->mp_buffer_current + mb_buffered_size(mb) < mp->mp_buffer_limit)
	break;
    }

    // These two safeguards so we don't run out of packets in any
    // of the queues

    if(mp->mp_video.mq_packets_current < vminpkt)
      break;

    if(mp->mp_audio.mq_packets_current < aminpkt)
      break;

    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);
  }

  if(e != NULL) {
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);
  } else {
    mb_enq(mp, mq, mb);
  }

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}



/**
 * Return -1 if queues are full. return 0 if enqueue succeeded.
 */
int
mb_enqueue_no_block(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb,
		    int auxtype)
{
  assert(mb->mb_data_type < MB_CTRL);

  hts_mutex_lock(&mp->mp_mutex);

  mp_update_buffer_delay(mp);
  mp_enqueue_check_pre_buffering(mp);

  if(mp->mp_buffer_current + mb_buffered_size(mb) > mp->mp_buffer_limit &&
     mq->mq_packets_current < 5) {
      hts_mutex_unlock(&mp->mp_mutex);
    return -1;
  }

  if(auxtype != -1) {
    media_buf_t *after;
    TAILQ_FOREACH_REVERSE(after, &mq->mq_q_aux, media_buf_queue, mb_link) {
      if(after->mb_data_type == auxtype)
	break;
    }

    if(after == NULL)
      TAILQ_INSERT_HEAD(&mq->mq_q_aux, mb, mb_link);
    else
      TAILQ_INSERT_AFTER(&mq->mq_q_aux, after, mb, mb_link);

  } else {
    TAILQ_INSERT_TAIL(&mq->mq_q_data, mb, mb_link);
  }

  mq->mq_packets_current++;
  mp->mp_buffer_current += mb_buffered_size(mb);
  mb->mb_epoch = mp->mp_epoch;
  mq_update_stats(mp, mq, 0);
  hts_cond_signal(&mq->mq_avail);

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
  mb_enq(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
void
mq_update_stats(media_pipe_t *mp, media_queue_t *mq, int force)
{
  if(!force && --mp->mp_stats_update_limiter > 0)
    return;
  mp->mp_stats_update_limiter = 100;

  int satisfied = mp->mp_eof ||
    mp->mp_buffer_current == 0 ||
    mp->mp_buffer_current * 8 > mp->mp_buffer_limit * 7 ||
    mp->mp_flags & MP_ALWAYS_SATISFIED;

  if(satisfied) {
    if(mp->mp_satisfied == 0) {
      atomic_dec(&media_buffer_hungry);
      mp->mp_satisfied = 1;
    }
  } else {
    if(mp->mp_satisfied != 0) {
      atomic_inc(&media_buffer_hungry);
      mp->mp_satisfied = 0;
    }
  }

  mp_update_buffer_delay(mp);

  prop_set_int(mq->mq_prop_qlen_cur, mq->mq_packets_current);
  prop_set_int(mp->mp_prop_buffer_current, mp->mp_buffer_current);
  if(mp->mp_buffer_delay == INT32_MAX)
    prop_set_void(mp->mp_prop_buffer_delay);
  else
    prop_set_float(mp->mp_prop_buffer_delay, mp->mp_buffer_delay / 1000000.0);
}


/**
 *
 */
void
mq_init(media_queue_t *mq, prop_t *p, hts_mutex_t *mutex, media_pipe_t *mp)
{
  mq->mq_mp = mp;
  mq->mq_last_deq_dts = PTS_UNSET;
  TAILQ_INIT(&mq->mq_q_data);
  TAILQ_INIT(&mq->mq_q_ctrl);
  TAILQ_INIT(&mq->mq_q_aux);

  mq->mq_packets_current = 0;
  mq->mq_stream = -1;
  hts_cond_init(&mq->mq_avail, mutex);
  mq->mq_prop_qlen_cur = prop_create(p, "dqlen");
  mq->mq_prop_qlen_max = prop_create(p, "dqmax");

  mq->mq_prop_bitrate = prop_create(p, "bitrate");

  mq->mq_prop_decode_avg  = prop_create(p, "decodetime_avg");
  mq->mq_prop_decode_peak = prop_create(p, "decodetime_peak");

  mq->mq_prop_upload_avg  = prop_create(p, "uploadtime_avg");
  mq->mq_prop_upload_peak = prop_create(p, "uploadtime_peak");

  mq->mq_prop_codec       = prop_create(p, "codec");
  mq->mq_prop_too_slow    = prop_create(p, "too_slow");
}


/**
 *
 */
void
mq_destroy(media_queue_t *mq)
{
  hts_cond_destroy(&mq->mq_avail);
}


/**
 *
 */
event_t *
mp_wait_for_empty_queues(media_pipe_t *mp)
{
  event_t *e;
  hts_mutex_lock(&mp->mp_mutex);

  // Only wait for data queues to drain, aux (subtitles) might be stalled
  while((e = TAILQ_FIRST(&mp->mp_eq)) == NULL &&
	(TAILQ_FIRST(&mp->mp_audio.mq_q_data) != NULL ||
         TAILQ_FIRST(&mp->mp_video.mq_q_data) != NULL))
    hts_cond_wait(&mp->mp_backpressure, &mp->mp_mutex);

  if(e != NULL)
    TAILQ_REMOVE(&mp->mp_eq, e, e_link);

  hts_mutex_unlock(&mp->mp_mutex);
  return e;
}



/**
 *
 */
void
mp_send_cmd(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  hts_mutex_lock(&mp->mp_mutex);
  mp_send_cmd_locked(mp, mq, cmd);
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

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb->mb_data = d;
  mb_enq(mp, mq, mb);
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

  mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = cmd;
  mb->mb_data32 = u;
  mb_enq(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
mb_prop_dtor(media_buf_t *mb)
{
  prop_ref_dec(mb->mb_prop);
  av_packet_unref(&mb->mb_pkt);
}


/**
 *
 */
void
mp_send_prop_set_string(media_pipe_t *mp, media_queue_t *mq,
                        prop_t *prop, const char *str)
{
  media_buf_t *mb;

  int datasize = strlen(str) + 1;
  hts_mutex_lock(&mp->mp_mutex);

  mb = media_buf_alloc_locked(mp, datasize);
  memcpy(mb->mb_data, str, datasize);
  mb->mb_data_type = MB_SET_PROP_STRING;
  mb->mb_prop = prop_ref_inc(prop);
  mb->mb_dtor = mb_prop_dtor;
  mb_enq(mp, mq, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}



/**
 *
 */
void
mp_send_cmd_locked(media_pipe_t *mp, media_queue_t *mq, int cmd)
{
  media_buf_t *mb = pool_get(mp->mp_mb_pool);
  mb->mb_data_type = cmd;
  mb_enq(mp, mq, mb);
}


/**
 *
 */
void
mp_send_volume_update_locked(media_pipe_t *mp)
{
  float v = pow(10.0f, mp->mp_vol_user / 20.0f) * mp->mp_vol_ui;
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = MB_CTRL_SET_VOLUME_MULTIPLIER;
  mb->mb_float = v;
  mb_enq(mp, &mp->mp_audio, mb);
}


/**
 *
 */
void
mb_enq(media_pipe_t *mp, media_queue_t *mq, media_buf_t *mb)
{
  int do_signal = 1;

  if(mb->mb_data_type == MB_SUBTITLE) {
    TAILQ_INSERT_TAIL(&mq->mq_q_aux, mb, mb_link);
  } else if(mb->mb_data_type > MB_CTRL) {
    TAILQ_INSERT_TAIL(&mq->mq_q_ctrl, mb, mb_link);
  } else {
    TAILQ_INSERT_TAIL(&mq->mq_q_data, mb, mb_link);
    do_signal = !mq->mq_no_data_interest;
  }
  mq->mq_packets_current++;
  mb->mb_epoch = mp->mp_epoch;
  mp->mp_buffer_current += mb_buffered_size(mb);

  mq_update_stats(mp, mq, 0);

  if(do_signal)
    hts_cond_signal(&mq->mq_avail);
}
