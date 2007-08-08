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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "audio_sched.h"
#include "audio_ui.h"

/*
 * Audio scheduler
 */

asched_t audio_scheduler;

/*
 *
 */

void
audio_sched_mp_activate(media_pipe_t *mp)
{
  asched_t *as = &audio_scheduler;

  if(mp == as->as_active)
    return;

  if(as->as_active != NULL) {
    printf("%s is active!!, stelaing audio\n", as->as_active->mp_name);
    audio_sched_mp_deactivate(as->as_active, 0);
  }
  pthread_mutex_lock(&as->as_lock);

  LIST_REMOVE(mp, mp_asched_link);
  as->as_active = mp;
  
  pthread_cond_signal(&as->as_cond);
  pthread_mutex_unlock(&as->as_lock);
}

/*
 *
 */

void
audio_sched_mp_deactivate(media_pipe_t *mp, int departing)
{
  asched_t *as = &audio_scheduler;

  if(as->as_active != mp)
    return;
  
  if(!(mp->mp_flags & MEDIA_PIPE_DONT_INVERVENT)) {
    if(mp->mp_playstatus == MP_PLAY)
      mp_set_playstatus(mp, departing ? MP_STOP : MP_PAUSE);
  }

  as->as_active = NULL;

  pthread_mutex_lock(&as->as_lock);

  mp_send_cmd(mp, &mp->mp_audio, MB_NOP);

  LIST_INSERT_HEAD(&as->as_inactive, mp, mp_asched_link);

  pthread_mutex_unlock(&as->as_lock);
}

/*
 *
 */

void
audio_sched_mp_init(media_pipe_t *mp)
{
  asched_t *as = &audio_scheduler;

  pthread_mutex_lock(&as->as_lock);
  LIST_INSERT_HEAD(&as->as_inactive, mp, mp_asched_link);
  pthread_mutex_unlock(&as->as_lock);
}


/*
 *
 */

void
audio_sched_mp_deinit(media_pipe_t *mp)
{
  asched_t *as = &audio_scheduler;

  audio_sched_mp_deactivate(mp, 1);

  pthread_mutex_lock(&as->as_lock);
  LIST_REMOVE(mp, mp_asched_link);
  pthread_mutex_unlock(&as->as_lock);
}

/*
 *
 */

media_pipe_t *
audio_sched_mp_get(void)
{
  asched_t *as = &audio_scheduler;
  media_pipe_t *mp;

  if((mp = as->as_active) == NULL)
    return NULL;

  return mp;
}

/*
 *
 */

static void *
audio_drain_thread(void *aux)
{
  asched_t *as = aux;
  media_pipe_t *mp;
  media_buf_t *mb;

  while(1) {
    pthread_mutex_lock(&as->as_lock);
    LIST_FOREACH(mp, &as->as_inactive, mp_asched_link) {
      while((mb = mb_dequeue(mp, &mp->mp_audio)) != NULL)
	media_buf_free(mb);
      mp->mp_clock_valid = 0;
    }
    pthread_mutex_unlock(&as->as_lock);
    sleep(1);
  }
}

/*
 *
 */

void
audio_sched_init(void)
{
  asched_t *as = &audio_scheduler;
  pthread_t ptid;

  pthread_mutex_init(&as->as_lock, NULL);
  pthread_cond_init(&as->as_cond, NULL);

#if 1 /* have alsa */
  {
    extern void alsa_audio_init(asched_t *as);
    extern void alsa_mixer_init(asched_t *as);
    alsa_audio_init(as);
    alsa_mixer_init(as);
  }
#endif

  pthread_create(&ptid, NULL, audio_drain_thread, as);

  audio_widget_make(as);
}
