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


audio_buf_t *
af_alloc_dynamic(size_t size)
{
  audio_buf_t *ab;
  ab = malloc(size + sizeof(audio_buf_t));
  ab->size = size;
  return ab;
}

audio_buf_t *
af_alloc(audio_fifo_t *af)
{
  return af_alloc_dynamic(af->bufsize);
}

void
af_enq(audio_fifo_t *af, audio_buf_t *ab)
{
  ab->ts = wallclock;
  assert(ab->payload_type != 0);

  pthread_mutex_lock(&af->lock);

  while(af->len == af->maxlen)
    pthread_cond_wait(&af->cond, &af->lock);

  af->len++;
  TAILQ_INSERT_TAIL(&af->queue, ab, link);
  pthread_cond_signal(&af->cond);

  pthread_mutex_unlock(&af->lock);
}


audio_buf_t *
af_deq(audio_fifo_t *af, int wait)
{
  audio_buf_t *ab;
  int64_t delay;

  pthread_mutex_lock(&af->lock);

  while(1) {
    ab = af->hold ? NULL : TAILQ_FIRST(&af->queue);

    if(af->hysteresis) {

      if(ab == NULL)
	af->satisfied = 0;
      else if(af->len < af->hysteresis && af->satisfied == 0)
	ab = NULL;
      else
	af->satisfied = 1;
    }

    if(ab != NULL)
      break;

    if(wait)
      pthread_cond_wait(&af->cond, &af->lock);
    else
      break;
  }
  if(ab != NULL) {
    af->len--;
    TAILQ_REMOVE(&af->queue, ab, link);
    pthread_cond_signal(&af->cond);

    delay = wallclock - ab->ts;
    af->avgdelay = (af->avgdelay + delay) / 2.0;
  }

  pthread_mutex_unlock(&af->lock);
  return ab;
}





void
af_free(audio_buf_t *ab)
{
  free(ab);
}


void
audio_fifo_init(audio_fifo_t *af, int maxlen, size_t size, int hysteresis)
{
  pthread_mutex_init(&af->lock, NULL);
  pthread_cond_init(&af->cond, NULL);
  TAILQ_INIT(&af->queue);
  af->satisfied = 0;
  af->hysteresis = hysteresis;
  af->len = 0;
  af->maxlen = maxlen;
  af->bufsize = size;
}

void 
audio_fifo_purge(audio_fifo_t *af)
{
  audio_buf_t *ab;

  pthread_mutex_lock(&af->lock);

  while((ab = TAILQ_FIRST(&af->queue)) != NULL) {
    TAILQ_REMOVE(&af->queue, ab, link);
    free(ab);
  }
  af->len = 0;

  pthread_cond_signal(&af->cond);
  pthread_mutex_unlock(&af->lock);
}


void 
audio_fifo_destroy(audio_fifo_t *af)
{
  audio_fifo_purge(af);
}
