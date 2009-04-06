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
  prop_t *pqe_root;
  prop_t *pqe_metadata;
  int pqe_enq;

  /**
   * playqueue_mutex must be held when accessing these
   */
  TAILQ_ENTRY(playqueue_entry) pqe_link;
  int pqe_linked;

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
static void
pqe_unref(playqueue_entry_t *pqe)
{
  if(atomic_add(&pqe->pqe_refcount, -1) != 1)
    return;

  assert(pqe->pqe_linked == 0);

  free(pqe->pqe_url);
  free(pqe->pqe_parent);
  prop_destroy(pqe->pqe_root);

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
playqueue_clear(void)
{
  playqueue_entry_t *pqe;

  while((pqe = TAILQ_FIRST(&playqueue_entries)) != NULL) {
    TAILQ_REMOVE(&playqueue_entries, pqe, pqe_link);
    pqe->pqe_linked = 0;
    pqe_unref(pqe);
  }
}


/**
 * Load siblings to the 'justadded' track.
 *
 * We do this by scanning the parent directory of the track.
 *
 * The idea is that even if a user just comes to as with a single URL
 * we are able to grab info about all tracks on the album.
 *
 */
static void
playqueue_load_siblings(const char *url, playqueue_entry_t *justadded)
{
  nav_dir_t *nd;
  int before = 1;
  playqueue_entry_t *pqe;
  nav_dir_entry_t *nde;
  prop_t *metadata;
  int r;

  if((nd = nav_scandir(url, NULL, 0)) == NULL)
    return;

  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {
    if(!strcmp(nde->nde_url, justadded->pqe_url)) {
      before = 0;
      continue;
    }

    if(nde->nde_type == CONTENT_DIR)
      continue;
    
    if(nde->nde_type == CONTENT_AUDIO && nde->nde_metadata != NULL) {
      metadata = nde->nde_metadata;
      nde->nde_metadata = NULL;

    } else {

      metadata = prop_create(NULL, "metadata");
      r = nav_probe(metadata, nde->nde_url, NULL, 0, NULL, 0);

      if(r != CONTENT_AUDIO) {
	prop_destroy(metadata);
	continue;
      }
    }

    pqe = malloc(sizeof(playqueue_entry_t));
    pqe->pqe_url    = strdup(nde->nde_url);
    pqe->pqe_parent = strdup(url);
    pqe->pqe_root   = prop_create(NULL, NULL);
    pqe->pqe_enq    = 0;
    pqe->pqe_refcount = 1;
    pqe->pqe_linked = 1;
    pqe->pqe_metadata = metadata;
    if(prop_set_parent(metadata, pqe->pqe_root))
      abort();
    
    prop_set_string(prop_create(pqe->pqe_root, "url"), pqe->pqe_url);

    if(before) {
      TAILQ_INSERT_BEFORE(justadded, pqe, pqe_link);
      if(prop_set_parent_ex(pqe->pqe_root, playqueue_root, 
			    justadded->pqe_root, NULL))
	abort();

    } else {
      TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_link);
      if(prop_set_parent(pqe->pqe_root, playqueue_root))
	abort();
    }

  }
  nav_dir_free(nd);
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

  hts_mutex_lock(&playqueue_mutex);

  TAILQ_FOREACH(pqe, &playqueue_entries, pqe_link) {
    if(!strcmp(pqe->pqe_url, url) && !strcmp(pqe->pqe_parent, parent)) {
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


  pqe = malloc(sizeof(playqueue_entry_t));
  pqe->pqe_url    = strdup(url);
  pqe->pqe_parent = strdup(parent);
  pqe->pqe_root   = prop_create(NULL, NULL);
  pqe->pqe_enq    = enq;
  pqe->pqe_refcount = 1;
  pqe->pqe_linked = 1;
  pqe->pqe_metadata = metadata;
  if(prop_set_parent(metadata, pqe->pqe_root))
    abort();

  prop_set_string(prop_create(pqe->pqe_root, "url"), url);

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
  if(prop_set_parent(pqe->pqe_root, playqueue_root))
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

  playqueue_mp = mp_create("playqueue");

  hts_mutex_init(&playqueue_request_mutex);
  hts_cond_init(&playqueue_request_cond);
  TAILQ_INIT(&playqueue_entries);
  TAILQ_INIT(&playqueue_requests);
   
  playqueue_root = prop_create(prop_get_global(), "playqueue");

  hts_thread_create_detached(playqueue_thread, NULL);
  hts_thread_create_detached(player_thread, NULL);
  return 0;
}




/**
 *
 */
static int
be_playqueue_open(const char *url0, nav_page_t **npp, 
		  char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *type, *nodes;

  *npp = n = nav_page_create(url0, sizeof(nav_page_t), NULL,
			     NAV_PAGE_DONT_CLOSE_ON_BACK);

  type  = prop_create(n->np_prop_root, "type");
  prop_set_string(type, "playqueue");

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
 *
 */
static int
pqe_event_handler(event_t *e, void *opaque)
{
  
  switch(e->e_type) {

  case EVENT_SEEK_FAST_BACKWARD:
  case EVENT_SEEK_BACKWARD:
  case EVENT_SEEK_FAST_FORWARD:
  case EVENT_SEEK_FORWARD:
  case EVENT_PLAYPAUSE:
  case EVENT_PLAY:
  case EVENT_PAUSE:
  case EVENT_STOP:
  case EVENT_PREV:
  case EVENT_NEXT:
  case EVENT_RESTART_TRACK:
    break;
  default:
    return 0;
  }

  mp_enqueue_event(playqueue_mp, e);
  return 1;
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
  void *eh;
  char errbuf[100];

  while(1) {
    
    while(pqe == NULL) {
      /* Got nothing to play, enter STOP mode */

      prop_unlink(mp->mp_prop_metadata);

      /* Drain queues */
      e = mp_wait_for_empty_queues(mp);
      if(e != NULL) {
	/* Got event while waiting for drain */
	mp_flush(mp);
      } else {
	/* Nothing and media queues empty. Wait for an event */
	mp_hibernate(mp);
	e = mp_dequeue_event(playqueue_mp);
      }

      if(e->e_type == EVENT_PLAYQUEUE_JUMP || 
	 e->e_type == EVENT_PLAYQUEUE_ENQ) {
	pe = (playqueue_event_t *)e;
	pqe = pe->pe_pqe;
	pe->pe_pqe = NULL;
      }
      event_unref(e);
    }

    prop_link(pqe->pqe_metadata, mp->mp_prop_metadata);
    eh = event_handler_register("playqueue", pqe_event_handler,
				EVENTPRI_MEDIACONTROLS_PLAYQUEUE, NULL);

    e = nav_play_audio(pqe->pqe_url, mp, errbuf, sizeof(errbuf));
    event_handler_unregister(eh);
    
    if(e == NULL) {
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nPlayqueue error:%s",
		 pqe->pqe_url, errbuf);
      pqe = playqueue_advance(pqe, 0);
      continue;
    }
    switch(e->e_type) {
     case EVENT_PREV:
       pqe = playqueue_advance(pqe, 1);
       break;

     case EVENT_NEXT:
     case EVENT_EOF:
       pqe = playqueue_advance(pqe, 0);
       break;

     case EVENT_STOP:
       pqe_unref(pqe);
       pqe = NULL;
       break;

    case EVENT_PLAYQUEUE_JUMP:
      pqe_unref(pqe);

      pe = (playqueue_event_t *)e;
      pqe = pe->pe_pqe;
      pe->pe_pqe = NULL; // Avoid deref upon event unref
      break;

    default:
      abort();
    }
    event_unref(e);
  }
}
