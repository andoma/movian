/*
 *  Playback of video
 *  Copyright (C) 2007-2008 Andreas Öman
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

#include "video_playback.h"
#include "event.h"
#include "media.h"
#include "navigator.h"

/**
 *
 */
static void *
video_player_idle(void *aux)
{
  video_playback_t *vp = aux;
  int run = 1;
  event_t *e = NULL, *next;
  media_pipe_t *mp = vp->vp_mp;
  char errbuf[100];

  while(run) {

    if(e == NULL)
      e = mp_dequeue_event(mp);
    
    switch(e->e_type) {
    default:
      break;

    case EVENT_PLAY_URL:
      next = nav_play_video(e->e_payload, mp, errbuf, sizeof(errbuf));
      event_unref(e);
      e = next;
      continue;

    case EVENT_EXIT:
      run = 0;
      break;
    }
    event_unref(e);
  }
  return NULL;
}

/**
 *
 */
video_playback_t *
video_playback_create(media_pipe_t *mp)
{
  video_playback_t *vp = calloc(1, sizeof(video_playback_t));
  vp->vp_mp = mp;
  hts_thread_create_joinable(&vp->vp_thread, video_player_idle, vp);
  return vp;
}


/**
 *
 */
void
video_playback_destroy(video_playback_t *vp)
{
  event_t *e = event_create_simple(EVENT_EXIT);

  mp_enqueue_event(vp->vp_mp, e);
  event_unref(e);

  hts_thread_join(&vp->vp_thread);

  

  free(vp);
}
