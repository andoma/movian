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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "playqueue.h"
#include "media/media.h"
#include "event.h"
#include "usage.h"

/**
 *
 */
typedef struct playqueue_entry {

  atomic_t pqe_refcount;

  /**
   * Read only members
   */
  char *pqe_url;
  prop_t *pqe_psource;

  prop_t *pqe_node;
  prop_t *pqe_prop_url;

  /**
   * Entry is enqueued (ie, not from source list)
   */
  uint8_t pqe_enq;

  /**
   * Set if globally linked. Protected by playqueue_mutex
   */
  uint8_t pqe_linked;

  /**
   * Set if this entry is playable
   */
  uint8_t pqe_playable;

  /**
   * Set if this entry should be played ASAP
   */
  uint8_t pqe_startme;

  /**
   * Global link. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_linear_link;
  TAILQ_ENTRY(playqueue_entry) pqe_shuffled_link;


  /**
   * Points back into node prop from source siblings
   * A ref is held on this prop when it's not NULL.
   */
  prop_t *pqe_originator;

  /**
   * Subscribes to source.url
   * Used to match entries from source into the currently played track
   */
  prop_sub_t *pqe_urlsub;


  /**
   * Subscribes to source.type
   * Used to find out if we should play the entry or just skip over it
   */
  prop_sub_t *pqe_typesub;

  /**
   * Maintains order from source list. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_source_link;

  /**
   * Index in queue
   */
  int pqe_index;

} playqueue_entry_t;


/**
 *
 */
typedef struct playqueue_event {
  event_t h;
  playqueue_entry_t *pe_pqe;
} playqueue_event_t;



static int shuffle_lfg;

static int playqueue_shuffle_mode = 0;

static int playqueue_repeat_mode = 0;

#define PLAYQUEUE_URL "playqueue:"

static prop_t *playqueue_root;
static prop_t *playqueue_nodes;

static void *player_thread(void *aux);

static media_pipe_t *playqueue_mp;


/**
 *
 */
static hts_mutex_t playqueue_mutex;


TAILQ_HEAD(playqueue_entry_queue, playqueue_entry);

static struct playqueue_entry_queue playqueue_entries;
static struct playqueue_entry_queue playqueue_shuffled_entries;
static int playqueue_length;

playqueue_entry_t *pqe_current;


/**
 *
 */
static prop_t *playqueue_model; 
static prop_sub_t *playqueue_source_sub;
static struct playqueue_entry_queue playqueue_source_entries;
static prop_t *playqueue_startme;
static int playqueue_start_paused;

static void update_pq_meta(void);



/**
 *
 */
static void
pqe_unref(playqueue_entry_t *pqe)
{
  if(atomic_dec(&pqe->pqe_refcount))
    return;
  assert(pqe_current != pqe);
  assert(pqe->pqe_linked == 0);
  assert(pqe->pqe_originator == NULL);
  assert(pqe->pqe_urlsub == NULL);
  assert(pqe->pqe_typesub == NULL);

  free(pqe->pqe_url);
  if(pqe->pqe_originator != NULL)
    prop_ref_dec(pqe->pqe_originator);

  prop_destroy(pqe->pqe_node);

  free(pqe);
}

/**
 *
 */
static void
pqe_ref(playqueue_entry_t *pqe)
{
  atomic_inc(&pqe->pqe_refcount);
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
}

/**
 *
 */
static event_t *
pqe_event_createx(playqueue_entry_t *pqe, event_type_t event)
{
  // jump ? EVENT_PLAYQUEUE_JUMP : EVENT_PLAYQUEUE_ENQ,

   playqueue_event_t *e;

   e = event_create(event, sizeof(playqueue_event_t));
   e->h.e_dtor = pqe_event_dtor;

   e->pe_pqe = pqe;
   pqe_ref(pqe);

   return &e->h;
}


/**
 *
 */
static void
pqe_play(playqueue_entry_t *pqe, event_type_t how)
{
  event_t *e = pqe_event_createx(pqe, how);
  mp_enqueue_event(playqueue_mp, e);
  event_release(e);
}


/**
 *
 */
static void
pqe_unsubscribe(playqueue_entry_t *pqe)
{
  if(pqe->pqe_urlsub != NULL) {
    prop_unsubscribe(pqe->pqe_urlsub);
    pqe->pqe_urlsub = NULL;
  }

  if(pqe->pqe_typesub != NULL) {
    prop_unsubscribe(pqe->pqe_typesub);
    pqe->pqe_typesub = NULL;
  }
}


/**
 *
 */
static void
pqe_remove_from_sourcequeue(playqueue_entry_t *pqe)
{
  TAILQ_REMOVE(&playqueue_source_entries, pqe, pqe_source_link);

  pqe_unsubscribe(pqe);

  prop_ref_dec(pqe->pqe_originator);
  pqe->pqe_originator = NULL;

  pqe_unref(pqe);
}


/**
 *
 */
static void
pq_renumber(playqueue_entry_t *pqe)
{
  int num;

  pqe = pqe ? TAILQ_PREV(pqe, playqueue_entry_queue, pqe_linear_link) : NULL;

  if(pqe == NULL) {
    pqe = TAILQ_FIRST(&playqueue_entries);
    num = 1;
  } else {
    num = pqe->pqe_index + 1;
    pqe = TAILQ_NEXT(pqe, pqe_linear_link);
  }

  for(; pqe != NULL; pqe = TAILQ_NEXT(pqe, pqe_linear_link)) {
    pqe->pqe_index = num++;
  }
}



/**
 *
 */
static void
pqe_remove_from_globalqueue(playqueue_entry_t *pqe)
{
  playqueue_entry_t *next = TAILQ_NEXT(pqe, pqe_linear_link);

  assert(pqe->pqe_linked == 1);
  prop_unparent(pqe->pqe_node);
  playqueue_length--;
  TAILQ_REMOVE(&playqueue_entries, pqe, pqe_linear_link);
  TAILQ_REMOVE(&playqueue_shuffled_entries, pqe, pqe_shuffled_link);
  pqe->pqe_linked = 0;
  pqe_unref(pqe);
  if(next != NULL)
    pq_renumber(next);
  update_pq_meta();
}



/**
 *
 */
static void
playqueue_clear(void)
{
  playqueue_entry_t *pqe;

  if(playqueue_model != NULL) {
    prop_unlink(prop_create(playqueue_mp->mp_prop_model, "metadata"));
    prop_destroy(playqueue_model);
    playqueue_model = NULL;
  }

  if(playqueue_source_sub != NULL) {
    prop_unsubscribe(playqueue_source_sub);
    playqueue_source_sub = NULL;
  }

  while((pqe = TAILQ_FIRST(&playqueue_source_entries)) != NULL)
    pqe_remove_from_sourcequeue(pqe);

  while((pqe = TAILQ_FIRST(&playqueue_entries)) != NULL)
    pqe_remove_from_globalqueue(pqe);

  if(playqueue_startme != NULL) {
    prop_ref_dec(playqueue_startme);
    playqueue_startme = NULL;
  }
}


/**
 *
 */
static void
pqe_insert_shuffled(playqueue_entry_t *pqe)
{
  int v;
  playqueue_entry_t *n;

  shuffle_lfg = shuffle_lfg * 1664525 + 1013904223;

  playqueue_length++;
  v = (unsigned int)shuffle_lfg % playqueue_length;

  n = TAILQ_FIRST(&playqueue_shuffled_entries);

  for(; n != NULL && v >= 0; v--) {
    n = TAILQ_NEXT(n, pqe_shuffled_link);
  }

  if(n != NULL) {
    TAILQ_INSERT_BEFORE(n, pqe, pqe_shuffled_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_shuffled_entries, pqe, pqe_shuffled_link);
  }
}


/**
 *
 */
static void
source_set_url(void *opaque, const char *str)
{
  playqueue_entry_t *pqe = opaque;

  if(str == NULL)
    return;

  free(pqe->pqe_url);
  pqe->pqe_url = strdup(str);

  if(pqe->pqe_startme) {
    pqe_play(pqe, pqe->pqe_startme == 2 ? 
	     EVENT_PLAYQUEUE_JUMP_AND_PAUSE : EVENT_PLAYQUEUE_JUMP);
    pqe->pqe_startme = 0;
  }
}


/**
 *
 */
static void
source_set_type(void *opaque, const char *str)
{
  playqueue_entry_t *pqe = opaque;

  if(str != NULL)
    pqe->pqe_playable =
      !strcmp(str, "audio") || !strcmp(str, "track") || !strcmp(str, "station");
}


/**
 *
 */
static playqueue_entry_t *
find_source_entry_by_prop(prop_t *p)
{
  playqueue_entry_t *pqe;

  TAILQ_FOREACH(pqe, &playqueue_source_entries, pqe_source_link)
    if(pqe->pqe_originator == p)
      break;
  return pqe;
}


/**
 *
 */
static void
add_from_source(prop_t *p, playqueue_entry_t *before)
{
  playqueue_entry_t *pqe;

  pqe = calloc(1, sizeof(playqueue_entry_t));
  atomic_set(&pqe->pqe_refcount, 1);
  pqe->pqe_originator = prop_ref_inc(p);

  if(playqueue_startme != NULL) {
    prop_t *q = prop_follow(p);

    if(q == playqueue_startme) {
      pqe->pqe_startme = 1 + playqueue_start_paused;
      prop_ref_dec(playqueue_startme);
      playqueue_startme = NULL;
      playqueue_start_paused = 0;
    }
    prop_ref_dec(q);
  }

  /**
   * We assume it's playable until we know better (see source_set_type)
   */
  pqe->pqe_playable = 1;

  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, pqe, pqe_source_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_source_entries, pqe, pqe_source_link);
  }

  p = prop_ref_inc(p); // TODO: do we need this?

  pqe->pqe_node = prop_create_root(NULL);
  prop_link(p, pqe->pqe_node);

  pqe->pqe_urlsub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_STRING, source_set_url, pqe,
		   PROP_TAG_MUTEX, &playqueue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  pqe->pqe_typesub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "type"),
		   PROP_TAG_CALLBACK_STRING, source_set_type, pqe,
		   PROP_TAG_MUTEX, &playqueue_mutex,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);


  pqe_ref(pqe); // Ref for global queue

  pqe->pqe_linked = 1;
  if(before != NULL) {
    assert(before->pqe_linked == 1);
    TAILQ_INSERT_BEFORE(before, pqe, pqe_linear_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_linear_link);
  }
  pq_renumber(pqe);

  pqe_insert_shuffled(pqe);
  update_pq_meta();

  if(prop_set_parent_ex(pqe->pqe_node, playqueue_nodes, 
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
  if(pqe->pqe_linked)
    pqe_remove_from_globalqueue(pqe);
}


/**
 *
 */
static void
move_track(playqueue_entry_t *pqe, playqueue_entry_t *before)
{
  playqueue_length--; // pqe_insert_shuffled() will increase it

  TAILQ_REMOVE(&playqueue_source_entries, pqe, pqe_source_link);
  TAILQ_REMOVE(&playqueue_entries, pqe, pqe_linear_link);
  TAILQ_REMOVE(&playqueue_shuffled_entries, pqe, pqe_shuffled_link);
  
  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, pqe, pqe_source_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_source_entries, pqe, pqe_source_link);
  }

  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, pqe, pqe_linear_link);
  } else {
    TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_linear_link);
  }

  pq_renumber(NULL);

  pqe_insert_shuffled(pqe);

  prop_move(pqe->pqe_node, before ? before->pqe_node : NULL);

  update_pq_meta();
}


/**
 *
 */
static void
siblings_populate(void *opaque, prop_event_t event, ...)
{
  prop_t *p;
  playqueue_entry_t *pqe;
  prop_vec_t *pv;
  int i;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    add_from_source(p, NULL);
    break;

  case PROP_ADD_CHILD_VECTOR:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      add_from_source(prop_vec_get(pv, i), NULL);
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

  case PROP_MOVE_CHILD:
    pqe  = find_source_entry_by_prop(va_arg(ap, prop_t *));
    assert(pqe  != NULL);
    move_track(pqe, find_source_entry_by_prop(va_arg(ap, prop_t *)));
    break;

  case PROP_REQ_DELETE_VECTOR:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_EXT_EVENT:
  case PROP_REQ_MOVE_CHILD:
    break;

  default:
    fprintf(stderr, "siblings_populate(): Can't handle event %d, aborting\n",
	    event);
    abort();
  }
}


/**
 *
 */
void
playqueue_load_with_source(prop_t *track, prop_t *source, int flags)
{
  playqueue_entry_t *pqe;

  hts_mutex_lock(&playqueue_mutex);

  TAILQ_FOREACH(pqe, &playqueue_entries, pqe_linear_link) {
    if(prop_compare(track, pqe->pqe_originator)) {
      pqe_play(pqe, EVENT_PLAYQUEUE_JUMP);
      hts_mutex_unlock(&playqueue_mutex);
      return;
    }
  }

  playqueue_clear();

  if(!(flags & PQ_NO_SKIP)) {
    playqueue_startme = prop_follow(track);
    playqueue_start_paused = flags & PQ_PAUSED ? 1 : 0;
  }

  playqueue_source_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "nodes"),
		   PROP_TAG_CALLBACK, siblings_populate, NULL,
		   PROP_TAG_MUTEX, &playqueue_mutex,
		   PROP_TAG_NAMED_ROOT, source, "self", 
		   NULL);

  playqueue_model = prop_xref_addref(source);

  prop_link(prop_create(source, "metadata"), 
	    prop_create(playqueue_mp->mp_prop_model, "metadata"));

  hts_mutex_unlock(&playqueue_mutex);
}


/**
 *
 */
static void
playqueue_enqueue(prop_t *track)
{
  playqueue_entry_t *pqe, *before;
  rstr_t *url;
  int doplay = 0;

  url = prop_get_string(track, "url", NULL);

  if(url == NULL)
    return;

  hts_mutex_lock(&playqueue_mutex);

  pqe = calloc(1, sizeof(playqueue_entry_t));
  pqe->pqe_url = strdup(rstr_get(url));

  pqe->pqe_node = prop_create_root(NULL);
  pqe->pqe_enq = 1;
  atomic_set(&pqe->pqe_refcount, 1);
  pqe->pqe_linked = 1;
  pqe->pqe_playable = 1;

  prop_link_ex(prop_create(track, "metadata"),
	       prop_create(pqe->pqe_node, "metadata"),
	       NULL, PROP_LINK_XREFED, 0);

  prop_set(pqe->pqe_node, "url", PROP_SET_RSTRING, url);
  prop_set(pqe->pqe_node, "type", PROP_SET_STRING, "audio");

  doplay = pqe_current == NULL;

  before = pqe_current ? TAILQ_NEXT(pqe_current, pqe_linear_link) : NULL;

  /* Skip past any previously enqueued entries */
  while(before != NULL && before->pqe_enq)
    before = TAILQ_NEXT(before, pqe_linear_link);

  if(before == NULL) {
    TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_linear_link);

    if(prop_set_parent(pqe->pqe_node, playqueue_nodes))
      abort();

  } else {
    TAILQ_INSERT_BEFORE(before, pqe, pqe_linear_link);

    if(prop_set_parent_ex(pqe->pqe_node, playqueue_nodes,
			  before->pqe_node, NULL))
      abort();
  }
  pq_renumber(pqe);
  pqe_insert_shuffled(pqe);

  update_pq_meta();

  if(doplay)
    pqe_play(pqe, EVENT_PLAYQUEUE_JUMP);

  hts_mutex_unlock(&playqueue_mutex);
  rstr_release(url);
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
void
playqueue_play(const char *url, prop_t *metadata, int paused)
{
  playqueue_entry_t *pqe;

  hts_mutex_lock(&playqueue_mutex);

  pqe = calloc(1, sizeof(playqueue_entry_t));
  pqe->pqe_url = strdup(url);

  pqe->pqe_node = prop_create_root(NULL);
  atomic_set(&pqe->pqe_refcount, 1);
  pqe->pqe_linked = 1;
  pqe->pqe_playable = 1;
  if(prop_set_parent(metadata, pqe->pqe_node))
    abort();

  prop_set(pqe->pqe_node, "url", PROP_SET_STRING, url);
  prop_set(pqe->pqe_node, "type", PROP_SET_STRING, "audio");

  /* Clear out the current playqueue */
  playqueue_clear();

  /* Enqueue our new entry */
  TAILQ_INSERT_TAIL(&playqueue_entries, pqe, pqe_linear_link);
  pq_renumber(pqe);
  pqe_insert_shuffled(pqe);
  update_pq_meta();
  if(prop_set_parent(pqe->pqe_node, playqueue_nodes))
    abort();

  /* Tick player to play it */
  pqe_play(pqe, paused ? EVENT_PLAYQUEUE_JUMP_AND_PAUSE : EVENT_PLAYQUEUE_JUMP);
  hts_mutex_unlock(&playqueue_mutex);
}


/**
 *
 */
static void
playqueue_set_shuffle(void *opaque, int v)
{
  playqueue_shuffle_mode = v;
  TRACE(TRACE_DEBUG, "playqueue", "Shuffle set to %s", v ? "on" : "off");
  update_pq_meta();
}


/**
 *
 */
static void
playqueue_set_repeat(void *opaque, int v)
{
  playqueue_repeat_mode = v;
  TRACE(TRACE_DEBUG, "playqueue", "Repeat set to %s", v ? "on" : "off");
  update_pq_meta();
}


/**
 *
 */
static void
pq_eventsink(void *opaque, prop_event_t event, ...)
{
  event_t *e;
  event_playtrack_t *ep;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_EXT_EVENT)
    return;

  e = va_arg(ap, event_t *);
  if(!event_is_type(e, EVENT_PLAYTRACK))
    return;

  ep = (event_playtrack_t *)e;
  if(ep->source == NULL)
    playqueue_enqueue(ep->track);
  else
    playqueue_load_with_source(ep->track, ep->source,
			       ep->mode ? PQ_NO_SKIP : 0);
}


/**
 *
 */
static int
playqueue_init(void)
{
  shuffle_lfg = time(NULL);

  hts_mutex_init(&playqueue_mutex);

  playqueue_mp = mp_create("playqueue", MP_PRIMABLE);

  TAILQ_INIT(&playqueue_entries);
  TAILQ_INIT(&playqueue_source_entries);
  TAILQ_INIT(&playqueue_shuffled_entries);

  prop_set_int(playqueue_mp->mp_prop_canShuffle, 1);
  prop_set_int(playqueue_mp->mp_prop_canRepeat, 1);

  prop_subscribe(0,
		 PROP_TAG_NAME("self", "shuffle"),
		 PROP_TAG_CALLBACK_INT, playqueue_set_shuffle, NULL,
		 PROP_TAG_NAMED_ROOT, playqueue_mp->mp_prop_root, "self",
		 PROP_TAG_MUTEX, &playqueue_mutex,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("self", "repeat"),
		 PROP_TAG_CALLBACK_INT, playqueue_set_repeat, NULL,
		 PROP_TAG_NAMED_ROOT, playqueue_mp->mp_prop_root, "self",
		 PROP_TAG_MUTEX, &playqueue_mutex,
		 NULL);

  playqueue_root = prop_create(prop_get_global(), "playqueue");
  playqueue_nodes = prop_create(playqueue_root, "nodes");

  hts_thread_create_detached("audioplayer", player_thread, NULL,
			     THREAD_PRIO_DEMUXER);

  prop_subscribe(0,
		 PROP_TAG_NAME("playqueue", "eventSink"),
		 PROP_TAG_CALLBACK, pq_eventsink, NULL,
		 PROP_TAG_ROOT, playqueue_root,
		 NULL);
  return 0;
}


void
playqueue_fini(void)
{
  event_dispatch(event_create_action(ACTION_STOP));
  playqueue_clear();
}


/**
 *
 */
int
playqueue_open(prop_t *page)
{
  prop_t *model;

  model = prop_create_r(page, "model");

  prop_set(model, "type", PROP_SET_STRING, "directory");
  prop_setv(model, "metadata", "title", NULL, PROP_ADOPT_RSTRING,
            _("Playqueue"));

  prop_t *nodes = prop_create_r(model, "nodes");

  prop_link(playqueue_nodes, nodes);

  prop_ref_dec(nodes);
  prop_ref_dec(model);
  return 0;
}


/**
 *
 */
static int
be_playqueue_open(prop_t *page, const char *url, int sync)
{
  usage_page_open(sync, "Playqueue");
  return playqueue_open(page);
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
static backend_t be_playqueue = {
  .be_init = playqueue_init,
  .be_canhandle = be_playqueue_canhandle,
  .be_open = be_playqueue_open,
};

BE_REGISTER(playqueue);

/**
 *
 */
static playqueue_entry_t *
playqueue_advance0(playqueue_entry_t *pqe, int reverse)
{
  playqueue_entry_t *cur = pqe;

  do {

    if(pqe->pqe_linked) {

      if(playqueue_shuffle_mode) {
	if(reverse) {
	  pqe = TAILQ_PREV(pqe, playqueue_entry_queue, pqe_shuffled_link);

	  if(playqueue_repeat_mode && pqe == NULL)
	    pqe = TAILQ_LAST(&playqueue_shuffled_entries,
			     playqueue_entry_queue);

	} else {
	  pqe = TAILQ_NEXT(pqe, pqe_shuffled_link);

	  if(playqueue_repeat_mode && pqe == NULL)
	    pqe = TAILQ_FIRST(&playqueue_shuffled_entries);
	}

      } else {

	if(reverse) {
	  pqe = TAILQ_PREV(pqe, playqueue_entry_queue, pqe_linear_link);

	  if(playqueue_repeat_mode && pqe == NULL)
	    pqe = TAILQ_LAST(&playqueue_entries, playqueue_entry_queue);

	} else {
	  pqe = TAILQ_NEXT(pqe, pqe_linear_link);

	  if(playqueue_repeat_mode && pqe == NULL)
	    pqe = TAILQ_FIRST(&playqueue_entries);
	}
      }

    } else {
      pqe = TAILQ_FIRST(&playqueue_entries);
    }
  } while(pqe != NULL && pqe != cur && pqe->pqe_playable == 0);
  return pqe;
}

/**
 *
 */
static void
update_pq_meta(void)
{
  media_pipe_t *mp = playqueue_mp;
  playqueue_entry_t *pqe = pqe_current;

  int can_skip_next = pqe && playqueue_advance0(pqe, 0);
  int can_skip_prev = pqe && playqueue_advance0(pqe, 1);

  prop_set_int(mp->mp_prop_canSkipForward,  can_skip_next);
  prop_set_int(mp->mp_prop_canSkipBackward, can_skip_prev);

  prop_set(mp->mp_prop_root, "totalTracks", PROP_SET_INT, playqueue_length);
  if(pqe != NULL)
    prop_set(mp->mp_prop_root, "currentTrack", PROP_SET_INT, pqe->pqe_index);
  else
    prop_set(mp->mp_prop_root, "currentTrack", PROP_SET_VOID);
}


/**
 *
 */
static playqueue_entry_t *
playqueue_advance(playqueue_entry_t *pqe, int reverse)
{
  playqueue_entry_t *nxt;

  hts_mutex_lock(&playqueue_mutex);

  nxt = playqueue_advance0(pqe, reverse);

  if(nxt != NULL)
    pqe_ref(nxt);

  update_pq_meta();

  pqe_unref(pqe);

  hts_mutex_unlock(&playqueue_mutex);
  return nxt;
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
  prop_t *p, *m;
  char errbuf[100];
  int startpaused = 0;
  while(1) {
    
    while(pqe == NULL) {
      /* Got nothing to play, enter STOP mode */

      hts_mutex_lock(&playqueue_mutex);
      pqe_current = NULL;
      update_pq_meta();
      hts_mutex_unlock(&playqueue_mutex);

      /* Drain queues */
      e = mp_wait_for_empty_queues(mp);
      if(e != NULL) {
	/* Got event while waiting for drain */
	mp_flush(mp);
      } else {
	/* Nothing and media queues empty. */

	TRACE(TRACE_DEBUG, "playqueue", "Nothing on queue, waiting");
        prop_set(playqueue_root, "active", PROP_SET_INT, 0);
	/* Make sure we no longer claim current playback focus */
	mp_set_url(mp, NULL, NULL, NULL);
	mp_shutdown(playqueue_mp);
    
	prop_unlink(mp->mp_prop_metadata);

	/* ... and wait for an event */
	e = mp_dequeue_event(playqueue_mp);
      }


      if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) ||
	 event_is_type(e, EVENT_PLAYQUEUE_JUMP_AND_PAUSE)) {
	pe = (playqueue_event_t *)e;
	pqe = pe->pe_pqe;
	pe->pe_pqe = NULL;
	startpaused = event_is_type(e, EVENT_PLAYQUEUE_JUMP_AND_PAUSE);

      } else if(event_is_action(e, ACTION_PLAY) ||
		event_is_action(e, ACTION_PLAYPAUSE)) {
	hts_mutex_lock(&playqueue_mutex);

	pqe = TAILQ_FIRST(&playqueue_entries);
	if(pqe != NULL)
	  pqe_ref(pqe);

	hts_mutex_unlock(&playqueue_mutex);
      }

      event_release(e);
    }

    if(pqe->pqe_url == NULL) {
      pqe = playqueue_advance(pqe, 0);
      continue;
    }

    prop_set(playqueue_root, "active", PROP_SET_INT, 1);
    mp_reset(mp);

    prop_t *sm = prop_get_by_name(PNVEC("self", "metadata"), 1,
                                  PROP_TAG_NAMED_ROOT, pqe->pqe_node, "self",
                                  NULL);
    prop_link_ex(sm, mp->mp_prop_metadata, NULL, PROP_LINK_XREFED, 0);

    mp->mp_prop_metadata_source = sm;

    m = prop_get_by_name(PNVEC("self", "media"), 1,
			 PROP_TAG_NAMED_ROOT, pqe->pqe_node, "self",
			 NULL);
    prop_link(mp->mp_prop_root, m);

    hts_mutex_lock(&playqueue_mutex);

    mp_set_url(mp, pqe->pqe_url, NULL, NULL);
    pqe_current = pqe;
    update_pq_meta();

    if(playqueue_advance0(pqe, 0) == NULL && playqueue_source_sub != NULL)
      prop_want_more_childs(playqueue_source_sub);

    hts_mutex_unlock(&playqueue_mutex);

    p = prop_get_by_name(PNVEC("self", "playing"), 1,
			 PROP_TAG_NAMED_ROOT, pqe->pqe_node, "self",
			 NULL);

    prop_set_int(p, 1);

    if(startpaused)
      mp_hold(mp, MP_HOLD_PAUSE, NULL);

    e = backend_play_audio(pqe->pqe_url, mp, errbuf, sizeof(errbuf),
			   startpaused, NULL);
    prop_ref_dec(sm);
    startpaused = 0;

    prop_set_int(p, 0);
    prop_ref_dec(p);

    // Unlink $self.media
    prop_unlink(m);
    prop_ref_dec(m);

    hts_mutex_lock(&playqueue_mutex);
    pqe_current = NULL;
    hts_mutex_unlock(&playqueue_mutex);

    prop_set(mp->mp_prop_root, "format", PROP_SET_VOID);

    if(e == NULL) {
      TRACE(TRACE_ERROR, "Playqueue", "Unable to play %s -- %s", pqe->pqe_url, errbuf);
      pqe = playqueue_advance(pqe, 0);
      continue;
    }

    if(event_is_action(e, ACTION_SKIP_BACKWARD)) {
      pqe = playqueue_advance(pqe, 1);

    } else if(event_is_action(e, ACTION_SKIP_FORWARD) ||
	      event_is_type  (e, EVENT_EOF)) {

      pqe = playqueue_advance(pqe, 0);

    } else if(event_is_action(e, ACTION_STOP) ||
	      event_is_action(e, ACTION_EJECT)) {
      pqe_unref(pqe);
      pqe = NULL;

    } else if(event_is_type(e, EVENT_PLAYQUEUE_JUMP) || 
	      event_is_type(e, EVENT_PLAYQUEUE_JUMP_AND_PAUSE)) {
      pqe_unref(pqe);

      pe = (playqueue_event_t *)e;
      pqe = pe->pe_pqe;
      pe->pe_pqe = NULL; // Avoid deref upon event unref
      startpaused = event_is_type(e, EVENT_PLAYQUEUE_JUMP_AND_PAUSE);

    } else {
      abort();
    }
    event_release(e);
  }
}


/**
 *
 */
void
playqueue_event_handler(event_t *e)
{
  if(event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PLAYPAUSE))
    mp_enqueue_event(playqueue_mp, e);
}
