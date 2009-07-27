/*
 *  Playqueue
 *  Copyright (C) 2008 Andreas Öman
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


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "showtime.h"
#include "navigator.h"
#include "playqueue.h"
#include "media.h"
#include "notifications.h"

static int playqueue_event_handler(event_t *e, void *opaque);

#define PLAYQUEUE_URL "playqueue:"

static prop_t *playqueue_root;

static void *player_thread(void *aux);

static media_pipe_t *playqueue_mp;

/**
 *
 */
static hts_mutex_t playqueue_mutex;


TAILQ_HEAD(playqueue_entry_queue, playqueue_entry);

static struct playqueue_entry_queue playqueue_entries;

typedef struct playqueue_entry {

  int pqe_refcount;

  /**
   * Read only members
   */
  char *pqe_url;
  char *pqe_parent;

  prop_t *pqe_node;


  /**
   * Entry is enqueued (ie, not from source list)
   */
  int pqe_enq;

  /**
   * Global link. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_link;

  /**
   * Set if globally linked. Protected by playqueue_mutex
   */
  int pqe_linked;

  /**
   * Points back into node prop from source siblings
   * A ref is held on this prop when it's not NULL.
   */
  prop_t *pqe_source;

  /**
   * Subscribes to source.url
   * Used to match entries from source into the currently played track
   */
  prop_sub_t *pqe_urlsub;

  /**
   * Maintains order from source list. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_source_link;

} playqueue_entry_t;

playqueue_entry_t *pqe_current;


/**
 *
 */
static hts_mutex_t playqueue_request_mutex;
static hts_cond_t  playqueue_request_cond;

TAILQ_HEAD(playqueue_request_queue, playqueue_request);

static struct playqueue_request_queue playqueue_requests;

typedef struct playqueue_request {
  TAILQ_ENTRY(playqueue_request) pqr_link;
  char *pqr_url;
  char *pqr_parent;
  prop_t *pqr_meta;
  int pqr_enq;

} playqueue_request_t;

/**
 *
 */
typedef struct playqueue_event {
  event_t h;
  playqueue_entry_t *pe_pqe;
} playqueue_event_t;

/**
 *
 */
static prop_t *playqueue_source; 
static prop_sub_t *playqueue_source_sub;
static playqueue_entry_t *playqueue_source_justadded;
static struct playqueue_entry_queue playqueue_source_entries;
static char *playqueue_source_parent;

/**
 *
 */
static void
pqe_unref(playqueue_entry_t *pqe)
{
  if(atomic_add(&pqe->pqe_refcount, -1) != 1)
    return;

  assert(pqe->pqe_linked == 0);
  assert(pqe->pqe_source == NULL);
  assert(pqe->pqe_urlsub == NULL);

  free(pqe->pqe_url);
  free(pqe->pqe_parent);
  prop_destroy(pqe->pqe_node);

  free(pqe);
}

/**
 *
 */
static void
pqe_ref(playqueue_entry_t *pqe)
{
  atomic_add(&pqe->pqe_refcount, 1);
}

/**
 *
 */
static void
pqe_event_dtor(event_t *e)
{
  playqueue_event_t *pe = (playqueue_event_t *)e;
  if(pe->pe_pqe != NULL)
    pqe_unref(pe->pe_pqe);
  free(e);
}

/**
 *
 */
static event_t *
pqe_event_create(playqueue_entry_t *pqe, int jump)
{
   playqueue_event_t *e;

   e = event_create(jump ? EVENT_PLAYQUEUE_JUMP : EVENT_PLAYQUEUE_ENQ,
		    sizeof(playqueue_event_t));
   e->h.e_dtor = pqe_event_dtor;

   e->pe_pqe = pqe;
   pqe_ref(pqe);

   return &e->h;
}

/**
 *
 */
static void
pqe_remove_from_sourcequeue(playqueue_entry_t *pqe)
{
  TAILQ_REMOVE(&playqueue_source_entries, pqe, pqe_source_link);

  prop_ref_dec(pqe->pqe_source);
  pqe->pqe_source = NULL;

  if(pqe->pqe_urlsub != NULL) {
    prop_unsubscribe(pqe->pqe_urlsub);
    pqe->pqe_urlsub = NULL;
  }

  pqe_unref(pqe);
}


/**
 *
 */
static void
pqe_remove_from_globalqueue(playqueue_entry_t *pqe)
{
  TAILQ_REMOVE(&playqueue_entries, pqe, pqe_link);
  pqe->pqe_linked = 0;
  pqe_unref(pqe);
}



/**
 *
 */
static void
playqueue_clear(void)
{
  playqueue_entry_t *pqe;

  if(playqueue_source_parent != NULL) {
    free(playqueue_source_parent);
    playqueue_source_parent = NULL;
  }

  if(playqueue_source != NULL) {
    prop_destroy(playqueue_source);
    playqueue_source = NULL;
  }

  if(playqueue_source_sub != NULL) {
    prop_unsubscribe(playqueue_source_sub);
    playqueue_source_sub = NULL;
  }

  if(playqueue_source_justadded != NULL) {
    pqe_unref(playqueue_source_justadded);
    playqueue_source_justadded = NULL;
  }

  while((pqe = TAILQ_FIRST(&playqueue_source_entries)) != NULL)
    pqe_remove_from_sourcequeue(pqe);

  while((pqe = TAILQ_FIRST(&playqueue_entries)) != NULL)
    pqe_remove_from_globalqueue(pqe);
}


/**
 *
 */
static void
pq_fill_from_source_backwards(playqueue_entry_t *pqe)
{
  playqueue_entry_t *b = pqe;

  while(1) {
    pqe = TAILQ_PREV(pqe, playqueue_entry_queue, pqe_source_link);
    if(pqe == NULL)
      break;

    assert(pqe->pqe_linked == 0);
    pqe->pqe_linked = 1;
    pqe_ref(pqe);

    TAILQ_INSERT_BEFORE(b, pqe, pqe_link);
    
    if(prop_set_parent_ex(pqe->pqe_node, playqueue_root, b->pqe_node, NULL))
      abort();
    
    b = pqe;
  }
}


/**
 *
 */
static void
pq_fill_from_source_forwards(playqueue_entry_t *pqe)
{
  while(1) {
    pqe = TAILQ_NEXT(pqe, pqe_source_link);
    if(pqe == NULL)
      break;

    assert(pqe->pqe_linked == 0);
    pqe->pqe_linked = 1;
    pqe_ref(pqe);

    TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);

    if(prop_set_parent_ex(pqe->pqe_node, playqueue_root, NULL, NULL))
      abort();
  }
}


/**
 *
 */
static void
source_set_url(void *opaque, const char *str)
{
  playqueue_entry_t *pqe = opaque;
  playqueue_entry_t *ja = playqueue_source_justadded;
  playqueue_entry_t *n;

  if(str == NULL)
    return;

  free(pqe->pqe_url);
  pqe->pqe_url = strdup(str);

  if(ja == NULL || strcmp(ja->pqe_url, str))
    return;

  /* 'pqe' from the source list matches 'ja'.
   * Transfer the location / binding of pqe in the source list to ja
   * and destroy pqe
   */

  ja->pqe_source = pqe->pqe_source; // refcount transfered
  pqe->pqe_source = NULL;

  n = TAILQ_NEXT(pqe, pqe_source_link);

  TAILQ_REMOVE(&playqueue_source_entries, pqe, pqe_source_link);

  prop_unsubscribe(pqe->pqe_urlsub);
  pqe->pqe_urlsub = NULL;

  if(n == NULL) {
    TAILQ_INSERT_TAIL(&playqueue_source_entries, ja, pqe_source_link);
  } else {
    TAILQ_INSERT_BEFORE(n, ja, pqe_source_link);
  }

  pqe_ref(ja); // Needs to refs now (for both queues)

  pqe_unref(pqe); // Should go away

  pqe_unref(playqueue_source_justadded);
  playqueue_source_justadded = NULL;

  pq_fill_from_source_backwards(ja);
  pq_fill_from_source_forwards(ja);
}


/**
 *
 */
static playqueue_entry_t *
find_source_entry_by_prop(prop_t *p)
{
  playqueue_entry_t *pqe;

  TAILQ_FOREACH(pqe, &playqueue_source_entries, pqe_source_link)
    if(pqe->pqe_source == p)
      return pqe;
  return NULL;
}


/**
 *
 */
static void
add_from_source(prop_t *p, playqueue_entry_t *before)
{
  playqueue_entry_t *pqe;

  pqe = calloc(1, sizeof(playqueue_entry_t));
  pqe->pqe_parent = strdup(playqueue_source_parent);
  pqe->pqe_refcount = 1;
  pqe->pqe_source = p;

  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, pqe, pqe_source_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_source_entries, pqe, pqe_source_link);
  }

  prop_ref_inc(p);

  pqe->pqe_node = prop_create(NULL, NULL);
  prop_link(p, pqe->pqe_node);

  pqe->pqe_urlsub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_STRING, source_set_url, pqe,
		   PROP_TAG_MUTEX, &playqueue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);


  if(playqueue_source_justadded != NULL)
    return;  // Still not jacked into global queue

  pqe_ref(pqe); // Ref for global queue

  pqe->pqe_linked = 1;
  if(before != NULL) {
    assert(before->pqe_linked == 1);
    TAILQ_INSERT_BEFORE(before, pqe, pqe_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
  }

  if(prop_set_parent_ex(pqe->pqe_node, playqueue_root, 
			before ? before->pqe_node : NULL, NULL))
    abort();
}


/**
 *
 */
static void
del_from_source(playqueue_entry_t *pqe)
{
  pqe_remove_from_sourcequeue(pqe);
  pqe_remove_from_globalqueue(pqe);
}


/**
 *
 */
static void
siblings_populate(void *opaque, prop_event_t event, ...)
{
  prop_t *p;
  playqueue_entry_t *pqe;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    add_from_source(p, NULL);
    break;

 case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    pqe = find_source_entry_by_prop(va_arg(ap, prop_t *));
    assert(pqe != NULL);
    add_from_source(p, pqe);
    break;
    
  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  case PROP_DEL_CHILD:
    pqe = find_source_entry_by_prop(va_arg(ap, prop_t *));
    assert(pqe != NULL);
    del_from_source(pqe);
    break;

  default:
    fprintf(stderr, "siblings_populate(): Can't handle event %d, aborting\n",
	    event);
    abort();
  }
}


/**
 * Load siblings to the 'justadded' track.
 */
static void
playqueue_load_siblings(const char *url, playqueue_entry_t *justadded)
{
  prop_t *p;
  char errbuf[200];

  assert(playqueue_source == NULL);

  assert(TAILQ_FIRST(&playqueue_source_entries) == NULL);
  
  if((p = nav_list(url, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_ERROR, "playqueue", "Unable to scan %s: %s", url, errbuf);
    return;
  }

  playqueue_source_parent = strdup(url);

  pqe_ref(justadded);
  playqueue_source_justadded = justadded;

  playqueue_source_sub = 
    prop_subscribe(PROP_SUB_DEBUG,
		   PROP_TAG_NAME("self", "nodes"),
		   PROP_TAG_CALLBACK, siblings_populate, NULL,
		   PROP_TAG_MUTEX, &playqueue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self", 
		   NULL);

  playqueue_source = p;

}


/**
 * Load playqueue based on the given url.
 *
 * This function is responsible for freeing (or using) the
 * supplied meta prop tree.
 *
 * If enq is set we don't clear the playqueue, instead we insert the
 * entry after the current track (or after the last enqueued track)
 *
 * That way users may 'stick in' track in the current playqueue
 */
static void
playqueue_load(const char *url, const char *parent, prop_t *metadata, int enq)
{
  playqueue_entry_t *pqe, *prev;
  event_t *e;

  if(parent != NULL && !strcmp(parent, "playqueue:"))
    parent = NULL;

  hts_mutex_lock(&playqueue_mutex);

  TAILQ_FOREACH(pqe, &playqueue_entries, pqe_link) {
    if(pqe->pqe_url != NULL && !strcmp(pqe->pqe_url, url) && 
       (parent == NULL || !strcmp(pqe->pqe_parent, parent))) {
      /* Already in, go to it */
      e = pqe_event_create(pqe, 1);
      mp_enqueue_event(playqueue_mp, e);
      event_unref(e);

      hts_mutex_unlock(&playqueue_mutex);

      if(metadata != NULL)
	prop_destroy(metadata);
      return;
    }
  }


  pqe = calloc(1, sizeof(playqueue_entry_t));
  pqe->pqe_url    = strdup(url);
  pqe->pqe_parent = strdup(parent);
  pqe->pqe_node   = prop_create(NULL, NULL);
  pqe->pqe_enq    = enq;
  pqe->pqe_refcount = 1;
  pqe->pqe_linked = 1;
  if(prop_set_parent(metadata, pqe->pqe_node))
    abort();

  prop_set_string(prop_create(pqe->pqe_node, "url"), url);
  prop_set_string(prop_create(pqe->pqe_node, "type"), "audio");

  if(enq) {

    prev = pqe_current;

    /* Skip past any previously enqueued entries */
    while(prev != NULL && prev->pqe_enq)
      prev = TAILQ_NEXT(prev, pqe_link);

    if(prev == NULL) {
      TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
    } else {
      TAILQ_INSERT_AFTER(&playqueue_entries, prev, pqe, pqe_link);
    }
    
    abort(); /* Not fully implemented */

  }

  
  /* Clear out the current playqueue */
  playqueue_clear();

  /* Enqueue our new entry */
  TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
  if(prop_set_parent(pqe->pqe_node, playqueue_root))
    abort();

  /* Tick player to play it */
  e = pqe_event_create(pqe, 1);
  mp_enqueue_event(playqueue_mp, e);
  event_unref(e);

  /* Scan dir (if provided) for additional tracks (siblings) */
  playqueue_load_siblings(parent, pqe);

  hts_mutex_unlock(&playqueue_mutex);
}

/**
 * Dequeue requests
 */
static void *
playqueue_thread(void *aux)
{
  playqueue_request_t *pqr;

  hts_mutex_lock(&playqueue_request_mutex);

  while(1) {
    
    while((pqr = TAILQ_FIRST(&playqueue_requests)) == NULL)
      hts_cond_wait(&playqueue_request_cond, &playqueue_request_mutex);

    TAILQ_REMOVE(&playqueue_requests, pqr, pqr_link);
    
    hts_mutex_unlock(&playqueue_request_mutex);

    playqueue_load(pqr->pqr_url, pqr->pqr_parent, pqr->pqr_meta,
		   pqr->pqr_enq);
    
    free(pqr->pqr_parent);
    free(pqr->pqr_url);
    free(pqr);

    hts_mutex_lock(&playqueue_request_mutex);
  }
}



/**
 * We don't want to hog caller, so we dispatch the request to a worker thread.
 */
void
playqueue_play(const char *url, const char *parent, prop_t *meta,
	       int enq)
{
  playqueue_request_t *pqr = malloc(sizeof(playqueue_request_t));
  char *x;

  pqr->pqr_url = strdup(url);

  if(parent == NULL) {
    pqr->pqr_parent = strdup(url);
    if((x = strrchr(pqr->pqr_parent, '/')) != NULL)
      *x = 0;
    
  } else {
    pqr->pqr_parent = strdup(parent);
  }
  pqr->pqr_meta = meta;
  pqr->pqr_enq = enq;

  hts_mutex_lock(&playqueue_request_mutex);
  TAILQ_INSERT_TAIL(&playqueue_requests, pqr, pqr_link);
  hts_cond_signal(&playqueue_request_cond);
  hts_mutex_unlock(&playqueue_request_mutex);
}



/**
 *
 */
static int
playqueue_init(void)
{
  hts_mutex_init(&playqueue_mutex);

  playqueue_mp = mp_create("playqueue", "tracks", 0);

  hts_mutex_init(&playqueue_request_mutex);
  hts_cond_init(&playqueue_request_cond);
  TAILQ_INIT(&playqueue_entries);
  TAILQ_INIT(&playqueue_requests);
  TAILQ_INIT(&playqueue_source_entries);

  playqueue_root = prop_create(prop_get_global(), "playqueue");

  hts_thread_create_detached(playqueue_thread, NULL);
  hts_thread_create_detached(player_thread, NULL);

  event_handler_register("playqueue", playqueue_event_handler,
			 EVENTPRI_PLAYQUEUE, NULL);
  return 0;
}




/**
 *
 */
static int
be_playqueue_open(const char *url0, const char *type0, const char *parent0,
		  nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *type, *nodes;

  *npp = n = nav_page_create(url0, sizeof(nav_page_t), NULL,
			     NAV_PAGE_DONT_CLOSE_ON_BACK);

  type  = prop_create(n->np_prop_root, "type");
  prop_set_string(type, "playqueue");

  type  = prop_create(n->np_prop_root, "type");
  prop_set_string(prop_create(n->np_prop_root, "view"), "list");

  nodes = prop_create(n->np_prop_root, "nodes");
  prop_set_string(prop_create(n->np_prop_root, "title"), "Playqueue");

  prop_link(playqueue_root, nodes);
  return 0;
}


/**
 *
 */
static int
be_playqueue_canhandle(const char *url)
{
  return !strncmp(url, PLAYQUEUE_URL, strlen(PLAYQUEUE_URL));
}



/**
 *
 */
nav_backend_t be_playqueue = {
  .nb_init = playqueue_init,
  .nb_canhandle = be_playqueue_canhandle,
  .nb_open = be_playqueue_open,
};


/**
 *
 */
static playqueue_entry_t *
playqueue_advance(playqueue_entry_t *pqe, int prev)
{
  playqueue_entry_t *r;

  hts_mutex_lock(&playqueue_mutex);

  if(pqe->pqe_linked) {

    if(prev) {
      r = TAILQ_PREV(pqe, playqueue_entry_queue, pqe_link);
    } else {
      r = TAILQ_NEXT(pqe, pqe_link);
    }

  } else {
    r = NULL;
  }

  if(r != NULL)
    pqe_ref(r);

  pqe_unref(pqe);

  hts_mutex_unlock(&playqueue_mutex);
  return r;
}


/**
 * Thread for actual playback
 */
static void *
player_thread(void *aux)
{
  media_pipe_t *mp = playqueue_mp;
  playqueue_entry_t *pqe = NULL;
  playqueue_event_t *pe;
  event_t *e;
  prop_t *p;
  char errbuf[100];

  while(1) {
    
    while(pqe == NULL) {
      /* Got nothing to play, enter STOP mode */

      prop_unlink(mp->mp_prop_metadata);

      /* Drain queues */
      e = mp_wait_for_empty_queues(mp, 0);
      if(e != NULL) {
	/* Got event while waiting for drain */
	mp_flush(mp);
      } else {
	/* Nothing and media queues empty. */

	TRACE(TRACE_DEBUG, "playqueue", "Nothing on queue, waiting");
	/* Make sure we no longer claim current playback focus */
	mp_shutdown(playqueue_mp);

	/* ... and wait for an event */
	e = mp_dequeue_event(playqueue_mp);
      }


      if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	 event_is_type(e, EVENT_PLAYQUEUE_ENQ)) {
	pe = (playqueue_event_t *)e;
	pqe = pe->pe_pqe;
	pe->pe_pqe = NULL;

      } else if(event_is_action(e, ACTION_PLAY) ||
		event_is_action(e, ACTION_PLAYPAUSE)) {
	hts_mutex_lock(&playqueue_mutex);

	pqe = TAILQ_FIRST(&playqueue_entries);
	if(pqe != NULL)
	  pqe_ref(pqe);

	hts_mutex_unlock(&playqueue_mutex);
      }

      event_unref(e);
    }

    p = prop_get_by_name(PNVEC("self", "metadata"), 1,
			 PROP_TAG_NAMED_ROOT, pqe->pqe_node, "self",
			 NULL);
    prop_link(p, mp->mp_prop_metadata);
    prop_ref_dec(p);

    if(pqe->pqe_url == NULL) {
      notify_add(NOTIFY_ERROR, NULL, 5, "Playqueue error: An entry lacks URL");
      pqe = playqueue_advance(pqe, 0);
      continue;
    }

    e = nav_play_audio(pqe->pqe_url, mp, errbuf, sizeof(errbuf));
    
    if(e == NULL) {
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nPlayqueue error: %s",
		 pqe->pqe_url, errbuf);
      pqe = playqueue_advance(pqe, 0);
      continue;
    }

    if(event_is_action(e, ACTION_PREV_TRACK)) {
       pqe = playqueue_advance(pqe, 1);

    } else if(event_is_action(e, ACTION_NEXT_TRACK) ||
	      event_is_type  (e, EVENT_EOF)) {
       pqe = playqueue_advance(pqe, 0);

    } else if(event_is_action(e, ACTION_STOP)) {
       pqe_unref(pqe);
       pqe = NULL;

    } else if(event_is_type(e, EVENT_PLAYQUEUE_JUMP)) {
      pqe_unref(pqe);

      pe = (playqueue_event_t *)e;
      pqe = pe->pe_pqe;
      pe->pe_pqe = NULL; // Avoid deref upon event unref

    } else {
      abort();
    }
    event_unref(e);
  }
}


/**
 *
 */
static int
playqueue_event_handler(event_t *e, void *opaque)
{
  if(event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PLAYPAUSE)) {
    mp_enqueue_event(playqueue_mp, e);
    return 1;

  }
  return 0;
}
