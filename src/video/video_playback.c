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

#include "showtime.h"
#include "video_playback.h"
#include "video_settings.h"
#include "event.h"
#include "media.h"
#include "backend/backend.h"
#include "notifications.h"
#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"
#include "misc/string.h"

static hts_mutex_t video_queue_mutex;


TAILQ_HEAD(video_queue_entry_queue, video_queue_entry);


/**
 *
 */
struct video_queue {
  prop_sub_t *vq_node_sub;
  struct video_queue_entry_queue vq_entries;
};


/**
 *
 */
typedef struct video_queue_entry {
  TAILQ_ENTRY(video_queue_entry) vqe_link;
  prop_t *vqe_root;
  rstr_t *vqe_url;
  rstr_t *vqe_type;
  prop_sub_t *vqe_url_sub;
  prop_sub_t *vqe_type_sub;
} video_queue_entry_t;


LIST_HEAD(vsource_list, vsource);

/**
 *
 */
typedef struct vsource {
  LIST_ENTRY(vsource) vs_link;
  char *vs_url;
  char *vs_mimetype;
  int vs_bitrate;
} vsource_t;


/**
 *
 */
static int
vs_cmp(const vsource_t *a, const vsource_t *b)
{
  return b->vs_bitrate - a->vs_bitrate;
}


/**
 *
 */
static void
vsource_insert(struct vsource_list *list, 
	       const char *url, const char *mimetype, int bitrate)
{
  if(backend_canhandle(url) == NULL)
    return;

  vsource_t *vs = malloc(sizeof(vsource_t));
  vs->vs_bitrate = bitrate;
  vs->vs_url = strdup(url);
  vs->vs_mimetype = mimetype ? strdup(mimetype) : NULL;
  LIST_INSERT_SORTED(list, vs, vs_link, vs_cmp);
}


/**
 *
 */
static void
vsource_cleanup(struct vsource_list *list)
{
  vsource_t *vs;
  while((vs = LIST_FIRST(list)) != NULL) {
    LIST_REMOVE(vs, vs_link);
    free(vs->vs_url);
    free(vs->vs_mimetype);
    free(vs);
  }
}


/**
 *
 */
static void
vsource_load_hls(struct vsource_list *list, const char *url,
		 const char *mimetype)
{
  int l;
  char *buf, *s;
  char errbuf[256];
  int flags = 0;

  s = buf = fa_load(url, NULL, NULL, errbuf, sizeof(errbuf), NULL, flags,
		    NULL, NULL);

  if(s == NULL) {
    TRACE(TRACE_INFO, "HLS", "Unable to open %s -- %s", url, errbuf);
    return;
  }

  if(mystrbegins(s, "#EXTM3U")) {

    int bandwidth = -1;

    for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
      s[l] = 0;
      if(l == 0)
	continue;

      const char *s2, *s3;
      if((s2 = mystrbegins(s, "#EXT-X-STREAM-INF:")) != NULL) {
	s3 = strstr(s2, "BANDWIDTH=");
	if(s3 != NULL)
	  bandwidth = atoi(s3 + strlen("BANDWIDTH="));
      } else if(*s == '#') {
	continue;
      } else if(bandwidth != -1) {
	char *playlist = url_resolve_relative_from_base(url, s);
	vsource_insert(list, playlist, mimetype, bandwidth);
	free(playlist);
	bandwidth = -1;
      }
    }
  } else {
    TRACE(TRACE_INFO, "HLS", "%s is not an EXTM3U file", url);
  }
  free(buf);
}


/**
 *
 */
static event_t *
play_video(const char *url, struct media_pipe *mp,
	   int flags, int priority,
	   char *errbuf, size_t errlen,
	   video_queue_t *vq)
{
  htsmsg_t *subs, *sources;
  const char *str;
  htsmsg_field_t *f;
  vsource_t *vs;
  struct vsource_list vsources;

  mp_reinit_streams(mp);

  if(strncmp(url, "videoparams:", strlen("videoparams:"))) 
    return backend_play_video(url, mp, flags | BACKEND_VIDEO_SET_TITLE,
			      priority, errbuf, errlen, NULL, url, vq);

  url += strlen("videoparams:");
  htsmsg_t *m = htsmsg_json_deserialize(url);

  if(m == NULL) {
    snprintf(errbuf, errlen, "Invalid JSON");
    return NULL;
  }

  LIST_INIT(&vsources);

  const char *canonical_url = htsmsg_get_str(m, "canonicalUrl");

  // Sources

  if((sources = htsmsg_get_list(m, "sources")) == NULL) {
    snprintf(errbuf, errlen, "No sources list in JSON parameters");
    return NULL;
  }
  
  HTSMSG_FOREACH(f, sources) {
    htsmsg_t *src = &f->hmf_msg;
    const char *url      = htsmsg_get_str(src, "url");
    const char *mimetype = htsmsg_get_str(src, "mimetype");
    int bitrate          = htsmsg_get_u32_or_default(src, "bitrate", -1);

    if(url == NULL)
      continue;

    if(mimetype != NULL && 
       (!strcmp(mimetype, "application/vnd.apple.mpegurl") ||
	!strcmp(mimetype, "audio/mpegurl"))) {
      if(0)vsource_load_hls(&vsources, url, mimetype);
      continue;
    }

    vsource_insert(&vsources, url, mimetype, bitrate);
  }

  if(LIST_FIRST(&vsources) == NULL) {
    snprintf(errbuf, errlen, "No players found for sources");
    vsource_cleanup(&vsources);
    return NULL;
  }
  
  // Other metadata

  if((str = htsmsg_get_str(m, "title")) != NULL)
    prop_set_string(prop_create(mp->mp_prop_metadata, "title"), str);
  else
    flags |= BACKEND_VIDEO_SET_TITLE;

  // Subtitles

  if((subs = htsmsg_get_list(m, "subtitles")) != NULL) {
    HTSMSG_FOREACH(f, subs) {
      htsmsg_t *sub = &f->hmf_msg;
      const char *title = htsmsg_get_str(sub, "title");
      const char *url = htsmsg_get_str(sub, "url");
      const char *lang = htsmsg_get_str(sub, "language");
      const char *source = htsmsg_get_str(sub, "source");

      mp_add_track(mp->mp_prop_subtitle_tracks, title, url, 
		   NULL, NULL, lang, source, NULL, 0);
    }
  }

  // Check if we should disable filesystem scanning of related files (subtitles)

  if(htsmsg_get_u32_or_default(m, "no_fs_scan", 0))
    flags |= BACKEND_VIDEO_NO_FS_SCAN;

  vs = LIST_FIRST(&vsources);
  
  if(canonical_url == NULL)
    canonical_url = vs->vs_url;

  event_t *e;

  e = backend_play_video(vs->vs_url, mp, flags, priority, 
			 errbuf, errlen, vs->vs_mimetype,
			 canonical_url, vq);

  vsource_cleanup(&vsources);

  return e;
}


/**
 *
 */
static void
vq_entry_destroy(video_queue_t *vq, video_queue_entry_t *vqe)
{
  prop_unsubscribe(vqe->vqe_url_sub);
  prop_unsubscribe(vqe->vqe_type_sub);

  rstr_release(vqe->vqe_url);
  rstr_release(vqe->vqe_type);
  TAILQ_REMOVE(&vq->vq_entries, vqe, vqe_link);
  prop_ref_dec(vqe->vqe_root);
  free(vqe);
}



/**
 *
 */
static void
vqe_set_url(video_queue_entry_t *vqe, rstr_t *str)
{
  rstr_set(&vqe->vqe_url, str);
}


/**
 *
 */
static void
vqe_set_type(video_queue_entry_t *vqe, rstr_t *str)
{
  rstr_set(&vqe->vqe_type, str);
}


/**
 *
 */
static void
vq_add_node(video_queue_t *vq, prop_t *p, video_queue_entry_t *before)
{
  video_queue_entry_t *vqe = calloc(1, sizeof(video_queue_entry_t));

  prop_tag_set(p, vq, vqe);

  vqe->vqe_root = prop_ref_inc(p);

  vqe->vqe_url_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_RSTR, vqe_set_url, vqe,
		   PROP_TAG_MUTEX, &video_queue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  vqe->vqe_type_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "type"),
		   PROP_TAG_CALLBACK_RSTR, vqe_set_type, vqe,
		   PROP_TAG_MUTEX, &video_queue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, vqe, vqe_link);
  } else {
    TAILQ_INSERT_TAIL(&vq->vq_entries, vqe, vqe_link);
  }
}


/**
 *
 */
static void
vq_move_node(video_queue_t *vq, video_queue_entry_t *vqe,
	     video_queue_entry_t *before)
{
  TAILQ_REMOVE(&vq->vq_entries, vqe, vqe_link);
  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, vqe, vqe_link);
  } else {
    TAILQ_INSERT_TAIL(&vq->vq_entries, vqe, vqe_link);
  }
}

/**
 *
 */
static void
vq_add_nodes(video_queue_t *vq, prop_vec_t *pv, video_queue_entry_t *before)
{
  int i;
  for(i = 0; i < prop_vec_len(pv); i++)
    vq_add_node(vq, prop_vec_get(pv, i), before);
}



static void
vq_del_node(video_queue_t *vq, video_queue_entry_t *vqe)
{
  vq_entry_destroy(vq, vqe);
}


/**
 *
 */
static void
vq_clear(video_queue_t *vq)
{
  video_queue_entry_t *vqe;

  while((vqe = TAILQ_FIRST(&vq->vq_entries)) != NULL) {
    prop_tag_clear(vqe->vqe_root, vq);
    vq_entry_destroy(vq, vqe);
  }
}


/**
 *
 */
static void
vq_entries_callback(void *opaque, prop_event_t event, ...)
{
  video_queue_t *vq = opaque;
  prop_t *p1, *p2;
  prop_vec_t *pv;

  va_list ap;
  va_start(ap, event);

  switch(event) {

  case PROP_ADD_CHILD:
    vq_add_node(vq, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    vq_add_node(vq, p1, prop_tag_get(p2, vq));
    break;

  case PROP_ADD_CHILD_VECTOR:
    vq_add_nodes(vq, va_arg(ap, prop_vec_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    vq_add_nodes(vq, pv, prop_tag_get(va_arg(ap, prop_t *), vq));
    break;

  case PROP_DEL_CHILD:
    p1 = va_arg(ap, prop_t *);
    vq_del_node(vq, prop_tag_clear(p1, vq));
    break;

  case PROP_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    vq_move_node(vq, prop_tag_get(p1, vq), prop_tag_get(p2, vq));
    break;

  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SET_VOID:
    vq_clear(vq);
    break;

  case PROP_DESTROYED:
    break;

  default:
    printf("Cant handle event %d\n", event);
    abort();
  }
  va_end(ap);
}


/**
 *
 */
static video_queue_t *
video_queue_create(prop_t *model)
{
  video_queue_t *vq = calloc(1, sizeof(video_queue_t));
  TAILQ_INIT(&vq->vq_entries);

  vq->vq_node_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "nodes"),
		   PROP_TAG_CALLBACK, vq_entries_callback, vq,
		   PROP_TAG_MUTEX, &video_queue_mutex,
		   PROP_TAG_NAMED_ROOT, model, "self", 
		   NULL);
  return vq;
}


/**
 *
 */
static void
video_queue_destroy(video_queue_t *vq)
{
  hts_mutex_lock(&video_queue_mutex);
  prop_unsubscribe(vq->vq_node_sub);
  vq_clear(vq);
  hts_mutex_unlock(&video_queue_mutex);
  free(vq);
}



/**
 *
 */
rstr_t *
video_queue_find_next(video_queue_t *vq, const char *url, int reverse,
		      int wrap)
{
  rstr_t *r = NULL;
  video_queue_entry_t *vqe;
  const char *cu = NULL;

  if(url == NULL)
    return NULL;

  if(!strncmp(url, "videoparams:", strlen("videoparams:"))) {
    htsmsg_t *m = htsmsg_json_deserialize(url + strlen("videoparams:"));
    cu = m ? htsmsg_get_str(m, "canonicalUrl") : NULL;
    cu = cu ? mystrdupa(cu) : NULL;
    htsmsg_destroy(m);
  }

  hts_mutex_lock(&video_queue_mutex);

  TAILQ_FOREACH(vqe, &vq->vq_entries, vqe_link) {
    if(vqe->vqe_url != NULL &&
       (!strcmp(url, rstr_get(vqe->vqe_url)) ||
	(cu && !strcmp(cu, rstr_get(vqe->vqe_url)))) &&
       vqe->vqe_type != NULL && 
       (!strcmp("video", rstr_get(vqe->vqe_type)) || 
	!strcmp("tvchannel", rstr_get(vqe->vqe_type))))
      break;
  }

  if(vqe != NULL) {

    if(reverse) {
      vqe = TAILQ_PREV(vqe, video_queue_entry_queue, vqe_link);
      if(vqe == NULL)
	vqe = TAILQ_LAST(&vq->vq_entries, video_queue_entry_queue);
    } else {
      vqe = TAILQ_NEXT(vqe, vqe_link);
      if(vqe == NULL)
	vqe = TAILQ_FIRST(&vq->vq_entries);
    }
  }

  if(vqe != NULL)
    r = rstr_dup(vqe->vqe_url);
  hts_mutex_unlock(&video_queue_mutex);
  return r;
}


/**
 *
 */
static void *
video_player_idle(void *aux)
{
  int run = 1;
  event_t *e = NULL;
  media_pipe_t *mp = aux;
  char errbuf[256];
  prop_t *errprop = prop_ref_inc(prop_create(mp->mp_prop_root, "error"));
  video_queue_t *vq = NULL;
  int play_flags = 0;
  int play_priority = 0;
  rstr_t *play_url = NULL;
  int force_continuous = 0;

  while(run) {
    
    if(play_url != NULL) {
      prop_set_void(errprop);
      e = play_video(rstr_get(play_url), mp, 
		     play_flags, play_priority, 
		     errbuf, sizeof(errbuf), vq);
      if(e == NULL)
	prop_set_string(errprop, errbuf);
    }

    if(e == NULL) {
      TRACE(TRACE_DEBUG, "vp", "Waiting for event");
      rstr_set(&play_url, NULL);
      e = mp_dequeue_event(mp);
    }
    if(event_is_type(e, EVENT_PLAY_URL)) {
      force_continuous = 0;
      prop_set_void(errprop);
      event_playurl_t *ep = (event_playurl_t *)e;
      play_flags = 0;
      if(ep->primary)
	play_flags |= BACKEND_VIDEO_PRIMARY;
      if(ep->no_audio)
	play_flags |= BACKEND_VIDEO_NO_AUDIO;
      play_priority = ep->priority;

      TRACE(TRACE_DEBUG, "vp", "Playing %s flags:0x%x", ep->url,
	    play_flags);

      if(vq != NULL)
	video_queue_destroy(vq);

      if(ep->model)
	vq = video_queue_create(ep->model);
      else
	vq = NULL;
      rstr_release(play_url);
      play_url = rstr_alloc(ep->url);

      if(ep->how) {
	if(!strcmp(ep->how, "beginning"))
	  play_flags |= BACKEND_VIDEO_START_FROM_BEGINNING;
	if(!strcmp(ep->how, "resume"))
	  play_flags |= BACKEND_VIDEO_RESUME;
	if(!strcmp(ep->how, "continuous"))
	  force_continuous = 1;
      }

    } else if(event_is_type(e, EVENT_EXIT)) {
      event_release(e);
      break;
    } else if(event_is_type(e, EVENT_EOF) || 
	      event_is_action(e, ACTION_SKIP_FORWARD) || 
	      event_is_action(e, ACTION_SKIP_BACKWARD)) {
      rstr_t *next = NULL;
      int skp = event_is_action(e, ACTION_SKIP_FORWARD) ||
	event_is_action(e, ACTION_SKIP_BACKWARD);
      if(vq && (video_settings.continuous_playback || force_continuous || skp))
	next = video_queue_find_next(vq, rstr_get(play_url),
				     event_is_action(e, ACTION_SKIP_BACKWARD), 
				     0);
      
      if(skp)
	play_flags |= BACKEND_VIDEO_START_FROM_BEGINNING;

      rstr_release(play_url);
      play_url = rstr_dup(next);
      if(play_url == NULL)
	mp_set_playstatus_stop(mp);
    }
    
    event_release(e);
    e = NULL;
  }

  rstr_release(play_url);
  if(vq != NULL)
    video_queue_destroy(vq);
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


static void __attribute__((constructor)) video_playback_init(void)
{
  hts_mutex_init(&video_queue_mutex);
}
