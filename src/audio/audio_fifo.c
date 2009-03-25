/*
 *  Audio fifos
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "audio_fifo.h"

/**
 *
 */
audio_buf_t *
af_alloc(size_t size, media_pipe_t *mp)
{
  audio_buf_t *ab;
  ab = malloc(size + sizeof(audio_buf_t));
  ab->ab_mp = mp;
  assert(mp != NULL);
  if(mp != NULL)
    mp_ref_inc(mp);
  return ab;
}

/**
 *
 */
void
af_enq(audio_fifo_t *af, audio_buf_t *ab)
{
  hts_mutex_lock(&af->af_lock);

  while(af->af_len > af->af_maxlen)
    hts_cond_wait(&af->af_cond, &af->af_lock);

  af->af_len += ab->ab_frames;

  TAILQ_INSERT_TAIL(&af->af_queue, ab, link);
  hts_cond_broadcast(&af->af_cond);

  hts_mutex_unlock(&af->af_lock);
}


/**
 *
 */
audio_buf_t *
af_peek(audio_fifo_t *af)
{
  audio_buf_t *ab;

  ab = TAILQ_FIRST(&af->af_queue);

  if(af->af_hysteresis) {

    if(ab == NULL)
      af->af_satisfied = 0;
    else if(af->af_len < af->af_hysteresis && af->af_satisfied == 0)
      ab = NULL;
    else
      af->af_satisfied = 1;
  }
  
  return ab;
}

/**
 *
 */
static void
af_remove(audio_fifo_t *af, audio_buf_t *ab)
{
  af->af_len -= ab->ab_frames;
  TAILQ_REMOVE(&af->af_queue, ab, link);
  hts_cond_broadcast(&af->af_cond);
}


/**
 *
 */
audio_buf_t *
af_deq(audio_fifo_t *af, int wait)
{
  audio_buf_t *ab;

  af_lock(af);
  while(1) {
    ab = af_peek(af);
    
    if(ab != NULL || !wait)
      break;
    hts_cond_wait(&af->af_cond, &af->af_lock);
  }

  if(ab != NULL)
    af_remove(af, ab);

  af_unlock(af);
  return ab;
}

/**
 *
 */
void
ab_free(audio_buf_t *ab)
{
  if(ab->ab_mp != NULL)
    mp_ref_dec(ab->ab_mp);
  free(ab);
}


/**
 *
 */
void
audio_fifo_init(audio_fifo_t *af, int maxlen, int hysteresis)
{
  hts_mutex_init(&af->af_lock);
  hts_cond_init(&af->af_cond);
  TAILQ_INIT(&af->af_queue);
  af->af_satisfied = 0;
  af->af_hysteresis = hysteresis;
  af->af_len = 0;
  af->af_maxlen = maxlen;
}

/**
 * Remove all buffer entries from the given reference and
 * optionally put them on queue 'q'
 */
void
audio_fifo_purge(audio_fifo_t *af, void *ref, struct audio_buf_queue *q)
{
  audio_buf_t *ab, *n;

  hts_mutex_lock(&af->af_lock);

  for(ab = TAILQ_FIRST(&af->af_queue); ab != NULL; ab = n) {
    n = TAILQ_NEXT(ab, link);
    if(ref != NULL && ab->ab_ref != ref)
      continue;

    TAILQ_REMOVE(&af->af_queue, ab, link);
    af->af_len -= ab->ab_frames;

    if(q != NULL) {
      TAILQ_INSERT_TAIL(q, ab, link);
    } else {
      ab_free(ab);
    }
  }

  hts_cond_broadcast(&af->af_cond);
  hts_mutex_unlock(&af->af_lock);  
}


/**
 * Remove all buffer entries from the given reference and
 * optionally put them on queue 'q'
 */
void
audio_fifo_reinsert(audio_fifo_t *af, struct audio_buf_queue *q)
{
  audio_buf_t *ab;

  hts_mutex_lock(&af->af_lock);

  while((ab = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, ab, link);
    af->af_len += ab->ab_frames;
    TAILQ_INSERT_TAIL(&af->af_queue, ab, link);
  }

  hts_cond_broadcast(&af->af_cond);
  hts_mutex_unlock(&af->af_lock);  
}


/**
 *
 */
void
audio_fifo_clear_queue(struct audio_buf_queue *q)
{
  audio_buf_t *ab;

  while((ab = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, ab, link);
    ab_free(ab);
  }
}
