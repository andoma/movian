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
#include "backend/backend.h"
#include "notifications.h"
#include "htsmsg/htsmsg_json.h"


typedef struct vsource {
  const char *vs_url;
  const char *vs_mimetype;
  int vs_bitrate;
} vsource_t;

/**
 *
 */
static event_t *
play_videoparams(const char *json, struct media_pipe *mp,
		 int flags, int priority,
		 char *errbuf, size_t errlen)
{
  htsmsg_t *m = htsmsg_json_deserialize(json);
  htsmsg_t *subs, *sources;
  const char *str;
  htsmsg_field_t *f;
  int nsources = 0, i;
  vsource_t *vsvec, *vs;
  const char *canonical_url;

  if(m == NULL) {
    snprintf(errbuf, errlen, "Invalid JSON");
    return NULL;
  }

  canonical_url = htsmsg_get_str(m, "canonicalUrl");

  // Sources

  if((sources = htsmsg_get_list(m, "sources")) == NULL) {
    snprintf(errbuf, errlen, "No sources list in JSON parameters");
    return NULL;
  }
  
  HTSMSG_FOREACH(f, sources)
    nsources++;
  
  if(nsources == 0) {
    snprintf(errbuf, errlen, "No sources in JSON list");
    return NULL;
  }

  vsvec = alloca(nsources * sizeof(vsource_t));

  i = 0;
  HTSMSG_FOREACH(f, sources) {
    htsmsg_t *src = &f->hmf_msg;
    vsvec[i].vs_url = htsmsg_get_str(src, "url");
    if(vsvec[i].vs_url == NULL)
      continue;

    if(backend_canhandle(vsvec[i].vs_url) == NULL)
      continue;

    vsvec[i].vs_bitrate = htsmsg_get_u32_or_default(src, "bitrate", -1);
    vsvec[i].vs_mimetype = htsmsg_get_str(src, "mimetype");
    i++;
  }

  nsources = i;

  if(nsources == 0) {
    snprintf(errbuf, errlen, "No players found for sources");
    return NULL;
  }
  
  // Other metadata

  if((str = htsmsg_get_str(m, "title")) != NULL)
    prop_set_string(prop_create(mp->mp_prop_metadata, "title"), str);

  // Subtitles

  if((subs = htsmsg_get_list(m, "subtitles")) != NULL) {
    HTSMSG_FOREACH(f, subs) {
      htsmsg_t *sub = &f->hmf_msg;
      const char *url = htsmsg_get_str(sub, "url");
      const char *lang = htsmsg_get_str(sub, "language");
      const char *source = htsmsg_get_str(sub, "source");

      mp_add_track(mp->mp_prop_subtitle_tracks, NULL, url, 
		   NULL, NULL, lang, source, NULL, 0);
    }
  }

  // Check if we should disable filesystem scanning of related files (subtitles)

  if(htsmsg_get_u32_or_default(m, "no_fs_scan", 0))
    flags |= BACKEND_VIDEO_NO_FS_SCAN;

  vs = vsvec;
  
  if(canonical_url == NULL)
    canonical_url = vs->vs_url;

  return backend_play_video(vs->vs_url, mp, flags, priority, 
			    errbuf, errlen, vs->vs_mimetype,
			    canonical_url);
}


/**
 *
 */
static void *
video_player_idle(void *aux)
{
  int run = 1;
  event_t *e = NULL, *next;
  media_pipe_t *mp = aux;
  char errbuf[256];
  prop_t *errprop = prop_ref_inc(prop_create(mp->mp_prop_root, "error"));

  while(run) {

    if(e == NULL)
      e = mp_dequeue_event(mp);
    

    if(event_is_type(e, EVENT_PLAY_URL)) {
      prop_set_void(errprop);
      event_playurl_t *ep = (event_playurl_t *)e;
      int flags = 0;
      if(ep->primary)
	flags |= BACKEND_VIDEO_PRIMARY;
      if(ep->no_audio)
	flags |= BACKEND_VIDEO_NO_AUDIO;

      if(!strncmp(ep->url, "videoparams:", strlen("videoparams:"))) {
	next = play_videoparams(ep->url + strlen("videoparams:"),
				mp, flags, ep->priority,
				errbuf, sizeof(errbuf));
      } else {
	next = backend_play_video(ep->url, mp, flags, ep->priority,
				  errbuf, sizeof(errbuf), NULL, ep->url);
      }

      if(next == NULL)
	prop_set_string(errprop, errbuf);

      event_release(e);
      e = next;
      continue;

    } else if(event_is_type(e, EVENT_EXIT)) {
      event_release(e);
      break;
    }
    event_release(e);
    e = NULL;
  }
  prop_ref_dec(errprop);
  mp_ref_dec(mp);
  return NULL;
}

/**
 *
 */
void
video_playback_create(media_pipe_t *mp)
{
  mp_ref_inc(mp);
  hts_thread_create_detached("video player",  video_player_idle, mp,
			     THREAD_PRIO_NORMAL);
}


/**
 *
 */
void
video_playback_destroy(media_pipe_t *mp)
{
  event_t *e = event_create_type(EVENT_EXIT);
  mp_enqueue_event(mp, e);
  event_release(e);
}
