/*
 *  Audio fifo
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

#ifndef AUDIO_FIFO_H
#define AUDIO_FIFO_H

#include "media.h"

TAILQ_HEAD(audio_buf_queue, audio_buf);

typedef struct audio_buf {
  TAILQ_ENTRY(audio_buf) link;
  int64_t ts;
  int64_t pts;
  int payload_type;
  size_t size;
  char data[0];
} audio_buf_t;


typedef struct audio_fifo {

  pthread_mutex_t lock;
  pthread_cond_t cond;

  struct audio_buf_queue queue;
  int len;
  int maxlen;
  int bufsize;

  float avgdelay;

  int hysteresis;
  int satisfied;

} audio_fifo_t;

#define ab_dataptr(ab) ((void *)&(ab)->data[0])

audio_buf_t *af_alloc(audio_fifo_t *af);
audio_buf_t *af_alloc_dynamic(size_t size);
void af_enq(audio_fifo_t *af, audio_buf_t *ab);
audio_buf_t *af_deq(audio_fifo_t *af, int wait);
void af_free(audio_buf_t *ab);

void audio_fifo_init(audio_fifo_t *af, int maxlen, size_t size,
		     int hysteresis);

void audio_fifo_destroy(audio_fifo_t *af);

void audio_fifo_purge(audio_fifo_t *af);

#endif /* AUDIO_FIFO_H */
