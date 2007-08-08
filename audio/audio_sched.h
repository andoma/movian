/*
 *  Audio scheduling
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

#ifndef AUDIO_SCHED_H
#define AUDIO_SCHED_H

#include "media.h"

typedef struct asched {
  
  struct media_pipe_list as_inactive;
  media_pipe_t *as_active;

  pthread_mutex_t as_lock;
  pthread_cond_t as_cond;

  int as_mute;
  float as_mastervol;

} asched_t;


extern asched_t audio_scheduler;

/*
 * Audio scheduler api
 */ 

void audio_sched_mp_activate(media_pipe_t *mp);

void audio_sched_mp_deactivate(media_pipe_t *mp, int departing);

void audio_sched_mp_init(media_pipe_t *mp);

void audio_sched_mp_deinit(media_pipe_t *mp);

media_pipe_t *audio_sched_mp_get(void);

void audio_sched_init(void);

#endif /* AUDIO_SCHED_H */
