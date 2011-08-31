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
  unsigned int ab_flush;
  unsigned int ab_format;
  unsigned int ab_samplerate;
  unsigned int ab_channels;
  char ab_isfloat;
  int64_t ab_pts;
  int ab_epoch;
  media_pipe_t *ab_mp;
  void *ab_ref;
  int ab_frames;
  int ab_alloced;
  int ab_tmp;    // For output devices only
  char ab_data[0];
} audio_buf_t;


typedef struct audio_fifo {

  hts_mutex_t af_lock;
  hts_cond_t af_cond;

  struct audio_buf_queue af_queue;

  int af_len;
  int af_maxlen;
  int af_hysteresis;
  int af_satisfied;

} audio_fifo_t;

#define ab_dataptr(ab) ((void *)&(ab)->af_data[0])

audio_buf_t *af_alloc(size_t size, media_pipe_t *mp);

void af_enq(audio_fifo_t *af, audio_buf_t *ab);

#define af_lock(af) hts_mutex_lock(&(af)->af_lock);

#define af_unlock(af) hts_mutex_unlock(&(af)->af_lock);

audio_buf_t *af_peek(audio_fifo_t *af);

audio_buf_t *af_wait(audio_fifo_t *af);

struct audio_mode;
audio_buf_t *af_deq2(audio_fifo_t *af, int wait, struct audio_mode *am);

#define AF_EXIT ((void *)-1)

void ab_free(audio_buf_t *ab);

void audio_fifo_init(audio_fifo_t *af, int maxlen, int hysteresis);

void audio_fifo_purge(audio_fifo_t *af, void *ref, struct audio_buf_queue *q);

void audio_fifo_reinsert(audio_fifo_t *af, struct audio_buf_queue *q);

void audio_fifo_clear_queue(struct audio_buf_queue *q);


#endif /* AUDIO_FIFO_H */
