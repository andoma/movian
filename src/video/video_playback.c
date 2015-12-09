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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "main.h"
#include "video_playback.h"
#include "video_settings.h"
#include "event.h"
#include "media/media.h"
#include "backend/backend.h"
#include "notifications.h"
#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "usage.h"

static HTS_MUTEX_DECL(video_queue_mutex);


TAILQ_HEAD(video_queue_entry_queue, video_queue_entry);



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
  struct video_queue *vqe_vq;
} video_queue_entry_t;


/**
 *
 */
struct video_queue {
  prop_sub_t *vq_node_sub;
  struct video_queue_entry_queue vq_entries;
  prop_t *vq_current_prop;
  video_queue_entry_t *vq_current;
  media_pipe_t *vq_mp;
};


LIST_HEAD(vsource_list, vsource);

/**
 *
 */
typedef struct vsource {
  LIST_ENTRY(vsource) vs_link;
  char *vs_url;
  char *vs_mimetype;
  int vs_bitrate;
  int vs_flags;
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
	       const char *url, const char *mimetype, int bitrate, int flags)
{
  if(backend_canhandle(url) == NULL)
    return;

  vsource_t *vs = malloc(sizeof(vsource_t));
  vs->vs_bitrate = bitrate;
  vs->vs_url = strdup(url);
  vs->vs_mimetype = mimetype ? strdup(mimetype) : NULL;
  vs->vs_flags = flags;
  LIST_INSERT_SORTED(list, vs, vs_link, vs_cmp, vsource_t);
}


/**
 *
 */
static void
vsource_free(vsource_t *vs)
{
  free(vs->vs_url);
  free(vs->vs_mimetype);
  free(vs);
}


/**
 *
 */
static vsource_t *
vsource_dup(const vsource_t *src)
{
  vsource_t *dst = malloc(sizeof(vsource_t));
  *dst = *src;
  dst->vs_url = strdup(dst->vs_url);
  dst->vs_mimetype = dst->vs_mimetype ? strdup(src->vs_mimetype) : NULL;
  return dst;
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
static event_t *
play_video(const char *url, struct media_pipe *mp,
	   int flags, int priority,
	   char *errbuf, size_t errlen,
	   video_queue_t *vq,
           const char *parent_url, const char *parent_title,
           prop_t *origin, int resume_mode,
           int64_t load_request_timestamp)
{
  htsmsg_t *subs, *sources;
  const char *str;
  htsmsg_field_t *f;
  vsource_t *vs;
  struct vsource_list vsources;
  event_t *e;
  const char *canonical_url;
  htsmsg_t *m = NULL;

  video_args_t va;

  if(parent_url != NULL && parent_url[0] == 0)
    parent_url = NULL;

  memset(&va, 0, sizeof(va));
  va.episode = -1;
  va.season = -1;
  va.origin = origin;
  va.priority = priority;
  va.resume_mode = resume_mode;
  va.load_request_timestamp = load_request_timestamp;

  LIST_INIT(&vsources);

  mp_reset(mp);

  prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 1);

  if(strncmp(url, "videoparams:", strlen("videoparams:"))) {
    backend_t *be = backend_canhandle(url);

    if(be == NULL || be->be_play_video == NULL) {
      prop_t *p = prop_create_root(NULL);
      if(backend_open(p, url, 1)) {
	prop_destroy(p);
	snprintf(errbuf, errlen, "No backend for URL");
	return NULL;
      }

      rstr_t *r = prop_get_string(p, "source", NULL);
      if(r != NULL) {
	TRACE(TRACE_DEBUG, "vp", "Page %s redirects to video source %s\n",
	      url, rstr_get(r));
	event_t *e = play_video(rstr_get(r), mp, flags, priority,
				errbuf, errlen, vq, parent_title, parent_url,
                                origin, resume_mode, load_request_timestamp);
        prop_destroy(p);
	rstr_release(r);
	return e;
      }

      rstr_t *type  = prop_get_string(p, "model", "type", NULL);
      rstr_t *err   = prop_get_string(p, "model", "error", NULL);
      rstr_t *title = prop_get_string(p, "model", "metadata", "title", NULL);

      if(type != NULL && !strcmp(rstr_get(type), "openerror")) {

        prop_set(mp->mp_prop_metadata, "title", PROP_SET_RSTRING, title);
        snprintf(errbuf, errlen, "%s",
                 err ? rstr_get(err) : "Unable to open URL");

      } else {
        snprintf(errbuf, errlen,
                 "Page model for '%s' does not provide sufficient data", url);
      }
      rstr_release(type);
      rstr_release(err);
      rstr_release(title);
      prop_destroy(p);
      return NULL;

    }

    va.canonical_url = canonical_url = url;
    va.parent_title = parent_title;
    va.parent_url = parent_url;
    va.flags = flags | BACKEND_VIDEO_SET_TITLE;

    e = be->be_play_video(url, mp, errbuf, errlen, vq, &vsources, &va);

  } else {

    url += strlen("videoparams:");
    m = htsmsg_json_deserialize(url);

    if(m == NULL) {
      snprintf(errbuf, errlen, "Invalid JSON");
      return NULL;
    }

    canonical_url = htsmsg_get_str(m, "canonicalUrl");

    // Metadata

    if((str = htsmsg_get_str(m, "title")) != NULL) {
      prop_set(mp->mp_prop_metadata, "title", PROP_SET_STRING, str);
      va.title = str;
    } else {
      flags |= BACKEND_VIDEO_SET_TITLE;
    }

    uint32_t u32;
    if(!htsmsg_get_u32(m, "year", &u32)) {
      prop_set(mp->mp_prop_metadata, "year", PROP_SET_INT, u32);
      va.year = u32;
    }
    if(!htsmsg_get_u32(m, "season", &u32)) {
      prop_set(mp->mp_prop_metadata, "season", PROP_SET_INT, u32);
      va.season = u32;
    }
    if(!htsmsg_get_u32(m, "episode", &u32)) {
      prop_set(mp->mp_prop_metadata, "episode", PROP_SET_INT, u32);
      va.episode = u32;
    }


    if((str = htsmsg_get_str(m, "imdbid")) != NULL)
      va.imdb = str;


    // Sources

    if((sources = htsmsg_get_list(m, "sources")) == NULL) {
      snprintf(errbuf, errlen, "No sources list in JSON parameters");
      return NULL;
    }

    HTSMSG_FOREACH(f, sources) {
      htsmsg_t *src = f->hmf_childs;
      const char *url      = htsmsg_get_str(src, "url");
      const char *mimetype = htsmsg_get_str(src, "mimetype");
      int bitrate          = htsmsg_get_u32_or_default(src, "bitrate", -1);

      if(url == NULL)
        continue;

      vsource_insert(&vsources, url, mimetype, bitrate,
		     BACKEND_VIDEO_NO_FS_SCAN);
    }


    if(LIST_FIRST(&vsources) == NULL) {
      snprintf(errbuf, errlen, "No players found for sources");
      vsource_cleanup(&vsources);
      return NULL;
    }
  

    // Subtitles

    if((subs = htsmsg_get_list(m, "subtitles")) != NULL) {
      HTSMSG_FOREACH(f, subs) {
        htsmsg_t *sub = f->hmf_childs;
        const char *title = htsmsg_get_str(sub, "title");
        const char *url = htsmsg_get_str(sub, "url");
        const char *lang = htsmsg_get_str(sub, "language");
        const char *source = htsmsg_get_str(sub, "source");

        mp_add_track(mp->mp_prop_subtitle_tracks, title, url, 
                     NULL, NULL, lang, source, NULL, 90000, 1);
      }
    }

    // Check if we should disable filesystem scanning (subtitles)
    if(htsmsg_get_u32_or_default(m, "no_fs_scan", 0))
      flags |= BACKEND_VIDEO_NO_FS_SCAN;

    // Subtitle scanning can be turned of completely
    if(htsmsg_get_u32_or_default(m, "no_subtitle_scan", 0))
      flags |= BACKEND_VIDEO_NO_SUBTITLE_SCAN;

    vs = LIST_FIRST(&vsources);
  
    if(canonical_url == NULL)
      canonical_url = vs->vs_url;

    TRACE(TRACE_DEBUG, "Video", "Playing %s", vs->vs_url);

    vs = vsource_dup(vs);

    va.canonical_url = canonical_url;
    va.flags = flags | vs->vs_flags;
    va.mimetype = vs->vs_mimetype;
    va.parent_title = parent_title;
    va.parent_url = parent_url;

    e = backend_play_video(vs->vs_url, mp, errbuf, errlen, vq, &vsources, &va);
    vsource_free(vs);
  }

  while(e != NULL) {

    if(event_is_type(e, EVENT_REOPEN)) {

      vsource_t *vs = LIST_FIRST(&vsources);
      if(vs == NULL) {
        snprintf(errbuf, errlen, "No alternate video sources");
        e = NULL;
        break;
      }

      TRACE(TRACE_DEBUG, "Video", "Playing %s", vs->vs_url);

      vs = vsource_dup(vs);

      va.canonical_url = canonical_url;
      va.flags = flags | vs->vs_flags;
      va.mimetype = vs->vs_mimetype;

      e = backend_play_video(vs->vs_url, mp, errbuf, errlen,
			     vq, &vsources, &va);
      vsource_free(vs);

    } else {
      break;
    }
  }

  vsource_cleanup(&vsources);
  if(m)
    htsmsg_release(m);
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
vq_update_metadata(video_queue_t *vq)
{
  video_queue_entry_t *vqe;

  vqe = vq->vq_current;
  if(vqe != NULL) {
    while((vqe = TAILQ_NEXT(vqe, vqe_link)) != NULL) {
      const char *t = rstr_get(vqe->vqe_type);
      if(t != NULL && (!strcmp(t, "video") || !strcmp(t, "tvchannel")))
        break;
    }
  }

  prop_set_int(vq->vq_mp->mp_prop_canSkipForward, vqe != NULL);

  vqe = vq->vq_current;
  if(vqe != NULL) {
    while((vqe = TAILQ_PREV(vqe, video_queue_entry_queue, vqe_link)) != NULL) {
      const char *t = rstr_get(vqe->vqe_type);
      if(t != NULL && (!strcmp(t, "video") || !strcmp(t, "tvchannel")))
        break;
    }
  }

  prop_set_int(vq->vq_mp->mp_prop_canSkipBackward, vqe != NULL);
}

/**
 *
 */
static void
vq_update_current(video_queue_t *vq)
{
  video_queue_entry_t *vqe = NULL;

  if(vq->vq_current_prop != NULL) {
    TAILQ_FOREACH(vqe, &vq->vq_entries, vqe_link) {
      if(prop_compare(vqe->vqe_root, vq->vq_current_prop)) {
        break;
      }
    }
  }

  if(vqe != NULL)
    vq->vq_current = vqe;
  else
    vq->vq_current = NULL;
  vq_update_metadata(vq);
}


/**
 *
 */
static void
vqe_set_url(video_queue_entry_t *vqe, rstr_t *str)
{
  rstr_set(&vqe->vqe_url, str);
  vq_update_metadata(vqe->vqe_vq);
}


/**
 *
 */
static void
vqe_set_type(video_queue_entry_t *vqe, rstr_t *str)
{
  rstr_set(&vqe->vqe_type, str);
  vq_update_metadata(vqe->vqe_vq);
}


/**
 *
 */
static void
vq_add_node(video_queue_t *vq, prop_t *p, video_queue_entry_t *before)
{
  video_queue_entry_t *vqe = calloc(1, sizeof(video_queue_entry_t));

  vqe->vqe_vq = vq;

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
  vq_update_current(vq);
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
  if(vqe == vq->vq_current)
    vq->vq_current = NULL;
  vq_entry_destroy(vq, vqe);
  vq_update_metadata(vq);
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
  vq->vq_current = NULL;
  vq_update_metadata(vq);
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
    vq_move_node(vq, prop_tag_get(p1, vq), p2 ? prop_tag_get(p2, vq) : NULL);
    break;

  case PROP_SET_DIR:
  case PROP_WANT_MORE_CHILDS:
    break;

  case PROP_SET_VOID:
    vq_clear(vq);
    break;

  case PROP_DESTROYED:
    break;

  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_SUGGEST_FOCUS:
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
video_queue_create(prop_t *model, media_pipe_t *mp)
{
  video_queue_t *vq = calloc(1, sizeof(video_queue_t));
  TAILQ_INIT(&vq->vq_entries);
  vq->vq_mp = mp;
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
video_queue_set_current(video_queue_t *vq, prop_t *item)
{
  hts_mutex_lock(&video_queue_mutex);
  prop_ref_dec(vq->vq_current_prop);
  vq->vq_current_prop = prop_ref_inc(item);
  vq_update_current(vq);
  printf("Current set to\n");
  prop_print_tree(item, 1);
  hts_mutex_unlock(&video_queue_mutex);
}


/**
 *
 */
static void
video_queue_destroy(video_queue_t *vq)
{
  hts_mutex_lock(&video_queue_mutex);
  prop_unsubscribe(vq->vq_node_sub);
  prop_ref_dec(vq->vq_current_prop);
  vq_clear(vq);
  hts_mutex_unlock(&video_queue_mutex);
  free(vq);
}



/**
 *
 */
prop_t *
video_queue_find_next(video_queue_t *vq, prop_t *current, int reverse,
		      int wrap)
{
  video_queue_entry_t *vqe;

  if(current == NULL)
    return NULL;

  hts_mutex_lock(&video_queue_mutex);

  TAILQ_FOREACH(vqe, &vq->vq_entries, vqe_link)
    if(prop_compare(vqe->vqe_root, current))
      break;

  while(vqe != NULL) {

    if(reverse) {
      vqe = TAILQ_PREV(vqe, video_queue_entry_queue, vqe_link);
    } else {
      vqe = TAILQ_NEXT(vqe, vqe_link);
    }
    
    if(vqe == NULL)
      break;

    const char *t = rstr_get(vqe->vqe_type);
    if(t != NULL) {
      if(strcmp(t, "video") && strcmp(t, "tvchannel"))
	continue;
    }
    break;
  }

  if(vqe == NULL) {
    hts_mutex_unlock(&video_queue_mutex);
    return NULL;
  }
  
  prop_t *p = prop_follow(vqe->vqe_root);
  hts_mutex_unlock(&video_queue_mutex);
  return p;
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
  prop_t *errprop = prop_create_r(mp->mp_prop_root, "error");
  video_queue_t *vq = NULL;
  int play_flags_permanent = 0;
  int play_priority = 0;
  rstr_t *play_url = NULL;
  int force_continuous = 0;
  prop_t *item_model = NULL;
  rstr_t *parent_url = NULL;
  rstr_t *parent_title = NULL;
  int64_t load_request_timestamp = 0;
  enum {
    RESUME_NO,
    RESUME_AS_GLOBAL_SETTING,
    RESUME_YES,
  } resume_ctrl = RESUME_NO;


  while(run) {

    if(play_url != NULL) {
      prop_set_void(errprop);

      int play_flags = play_flags_permanent;

      int resume_mode = VIDEO_RESUME_NO;
      switch(resume_ctrl) {
      case RESUME_NO:
        break;

      case RESUME_AS_GLOBAL_SETTING:
        resume_mode = video_settings.resume_mode;
        break;

      case RESUME_YES:
        resume_mode = VIDEO_RESUME_YES;
        break;
      }

      resume_ctrl = RESUME_NO; // For next item during continuous play

      TRACE(TRACE_DEBUG, "vp", "Playing '%s'%s%s, resume:%s%s",
            rstr_get(play_url),
            play_flags & BACKEND_VIDEO_PRIMARY  ? ", primary" : "",
            play_flags & BACKEND_VIDEO_NO_AUDIO ? ", no-audio" : "",
            resume_mode == VIDEO_RESUME_YES ? "yes" :
            resume_mode == VIDEO_RESUME_NO  ? "no" : "ask user",
            resume_ctrl == RESUME_AS_GLOBAL_SETTING ? "" : " (overridden)");

      prop_set(mp->mp_prop_metadata, "title", PROP_SET_VOID);
      if(vq != NULL)
        video_queue_set_current(vq, item_model);
      e = play_video(rstr_get(play_url), mp,
		     play_flags, play_priority,
		     errbuf, sizeof(errbuf), vq,
                     rstr_get(parent_url), rstr_get(parent_title),
                     item_model, resume_mode, load_request_timestamp);
      mp_bump_epoch(mp);
      prop_set(mp->mp_prop_root, "loading", PROP_SET_INT, 0);
      if(e == NULL) {
	prop_set_string(errprop, errbuf);
      } else {
        prop_set(mp->mp_prop_metadata, "title", PROP_SET_VOID);
      }
    }

    if(e == NULL) {
      TRACE(TRACE_DEBUG, "vp", "Waiting for event");
      rstr_set(&play_url, NULL);
      e = mp_dequeue_event(mp);
    }
    load_request_timestamp = e->e_timestamp;
    if(event_is_type(e, EVENT_EOF) && mp->mp_auto_standby) {
      app_shutdown(APP_EXIT_STANDBY);
      event_release(e);
      e = NULL;
      break;
    }

    if(event_is_type(e, EVENT_PLAY_URL)) {
      force_continuous = 0;
      prop_set_void(errprop);
      event_playurl_t *ep = (event_playurl_t *)e;
      play_flags_permanent = 0;
      if(ep->primary)
	play_flags_permanent |= BACKEND_VIDEO_PRIMARY;
      if(ep->no_audio)
	play_flags_permanent |= BACKEND_VIDEO_NO_AUDIO;
      play_priority = ep->priority;

      prop_ref_dec(item_model);
      if(vq != NULL) {
	video_queue_destroy(vq);
	vq = NULL;
      }

      rstr_release(parent_url);
      parent_url = backend_normalize(rstr_alloc(ep->parent_url));

      rstr_release(parent_title);

      if(ep->parent_model != NULL) {
        prop_t *x = prop_follow(ep->parent_model);
        parent_title = prop_get_string(x, "metadata", "title", NULL);
        prop_ref_dec(x);
      } else {
        parent_title = NULL;
      }

      item_model = prop_ref_inc(ep->item_model);
      if(ep->parent_model != NULL && item_model != NULL) {
	vq = video_queue_create(ep->parent_model, mp);
      } else {
	vq = NULL;
      }
      rstr_release(play_url);
      play_url = rstr_alloc(ep->url);

      resume_ctrl = RESUME_AS_GLOBAL_SETTING;

      if(ep->how) {
	if(!strcmp(ep->how, "beginning"))
          resume_ctrl = RESUME_NO;
	else if(!strcmp(ep->how, "resume"))
          resume_ctrl = RESUME_YES;
	else if(!strcmp(ep->how, "continuous"))
	  force_continuous = 1;
      }

    } else if(event_is_type(e, EVENT_EXIT)) {
      event_release(e);
      break;

    } else if(event_is_type(e, EVENT_EOF) ||
              event_is_action(e, ACTION_SKIP_FORWARD) ||
              event_is_action(e, ACTION_SKIP_BACKWARD)) {

      // Try to figure out which track to play next

      prop_t *next = NULL;

      int skp =
        event_is_action(e, ACTION_SKIP_FORWARD) ||
	event_is_action(e, ACTION_SKIP_BACKWARD);

      if(vq && (video_settings.continuous_playback || force_continuous || skp))
	next = video_queue_find_next(vq, item_model,
				     event_is_action(e, ACTION_SKIP_BACKWARD),
				     0);

      prop_ref_dec(item_model);
      item_model = NULL;

      rstr_release(play_url);

      if(next != NULL) {
	play_url = prop_get_string(next, "url", NULL);
	item_model = next;

        prop_suggest_focus(item_model);

      } else {
	play_url = NULL;
      }
      if(play_url == NULL)
        prop_set_string(mp->mp_prop_playstatus, "stop");
    }

    event_release(e);
    e = NULL;
  }

  rstr_release(parent_url);
  rstr_release(parent_title);
  rstr_release(play_url);
  if(vq != NULL)
    video_queue_destroy(vq);
  prop_ref_dec(item_model);
  prop_ref_dec(errprop);
  mp_shutdown(mp);
  mp_release(mp);
  return NULL;
}

/**
 *
 */
void
video_playback_create(media_pipe_t *mp)
{
  hts_thread_create_detached("video player",  video_player_idle,
                             mp_retain(mp),
			     THREAD_PRIO_DEMUXER);
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


htsmsg_t *
video_playback_info_create(const struct video_args *va)
{
  htsmsg_t *vpi = htsmsg_create_map();
  htsmsg_add_str(vpi, "canonical_url", va->canonical_url);
  htsmsg_add_str(vpi, "title", va->title);
  return vpi;
}


static LIST_HEAD(, video_playback_info_handler) vpi_handlers;

void
register_video_playback_info_handler(video_playback_info_handler_t *vpih)
{
  LIST_INSERT_HEAD(&vpi_handlers, vpih, link);
}


void
video_playback_info_invoke(vpi_op_t op, struct htsmsg *vpi, struct prop *p)
{
  video_playback_info_handler_t *vpih;
  LIST_FOREACH(vpih, &vpi_handlers, link)
    vpih->invoke(op, vpi, p);
}
