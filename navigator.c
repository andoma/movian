/*
 *  Navigator
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

#include "showtime.h"
#include "navigator.h"
#include "event.h"

static hts_mutex_t nav_mutex;

static struct nav_backend_list nav_backends;
static struct nav_page_queue nav_pages;
static struct nav_page_queue nav_history;

static nav_page_t *nav_page_current;

static prop_t *nav_prop_root;
static prop_t *nav_prop_path;
static prop_t *nav_prop_pages;
static prop_t *nav_prop_curpage;
static event_queue_t nav_eq;

static void *navigator_thread(void *aux);
static int nav_input_event(event_t *e, void *opaque);


/**
 *
 */
static void
nav_init_be(nav_backend_t *be)
{
  LIST_INSERT_HEAD(&nav_backends, be, nb_global_link);
}



/**
 *
 */
void
nav_init(void)
{
  hts_mutex_init(&nav_mutex);

  TAILQ_INIT(&nav_pages);
  TAILQ_INIT(&nav_history);
  event_handler_register("navigator", nav_input_event, EVENTPRI_NAV, NULL);

  nav_prop_root    = prop_create(prop_get_global(), "nav");
  nav_prop_path    = prop_create(nav_prop_root, "path");
  nav_prop_pages   = prop_create(nav_prop_root, "pages");
  nav_prop_curpage = prop_create(nav_prop_root, "currentpage");

  event_initqueue(&nav_eq);

#define NAV_INIT_BE(name) \
 {extern nav_backend_t be_ ## name; nav_init_be(&be_ ## name);}

  NAV_INIT_BE(page);
  NAV_INIT_BE(file);
  NAV_INIT_BE(settings);
  NAV_INIT_BE(playqueue);

  hts_thread_create_detached(navigator_thread, NULL);
}


/**
 *
 */
static void
nav_remove_from_history(nav_page_t *np)
{
  np->np_inhistory = 0;
  TAILQ_REMOVE(&nav_history, np, np_history_link);
}


/**
 *
 */
void
nav_close(nav_page_t *np)
{
  if(nav_page_current == np)
    nav_page_current = NULL;

  if(np->np_inhistory)
    nav_remove_from_history(np);

  if(np->np_close != NULL)
    np->np_close(np);

  TAILQ_REMOVE(&nav_pages, np, np_global_link);
  prop_destroy(np->np_prop_root);
  free(np->np_url);
  free(np);
}


/**
 *
 */
void
nav_open(const char *url)
{
  nav_page_t *np, *np2;
  nav_backend_t *nb;
  char errbuf[128];

  /* First, if a page is already open, go directly to it */

  TAILQ_FOREACH(np, &nav_pages, np_global_link) {
    if(!strcmp(np->np_url, url)) {
      prop_select(np->np_prop_root, 0);
      prop_link(np->np_prop_root, nav_prop_curpage);
      break;
    }
  }

  if(np == NULL) {

    LIST_FOREACH(nb, &nav_backends, nb_global_link)
      if(nb->nb_canhandle(url))
	break;
  
    if(nb == NULL) {
      fprintf(stderr, "Unable to open %s -- No handler\n", url);
      return;
    }

    if(nb->nb_open(url, &np, errbuf, sizeof(errbuf))) {
      fprintf(stderr, "Unable to open %s -- %s\n", url, errbuf);
      return;
    }
  
    if(np == NULL)
      return;

    prop_set_parent(np->np_prop_root, nav_prop_pages);
    prop_link(np->np_prop_root, nav_prop_curpage);

    prop_select(np->np_prop_root, 0);
  }

  if(np->np_inhistory == 0) {
    if(nav_page_current != NULL) {
      
      /* Destroy any previous "future" histories,
       * this happens if we back a few times and then jumps away
       * in another "direction"
       */
      
      while((np2 = TAILQ_NEXT(nav_page_current, np_history_link)) != NULL)
	nav_close(np2);
    }

    TAILQ_INSERT_TAIL(&nav_history, np, np_history_link);
    np->np_inhistory = 1;
  }

  nav_page_current = np;
}


/**
 *
 */
static void
nav_back(void)
{
  nav_page_t *prev, *np = nav_page_current;

  if(np == NULL ||
     (prev = TAILQ_PREV(np, nav_page_queue, np_history_link)) == NULL)
     return;

  nav_open(prev->np_url);

  if(!(np->np_flags & NAV_PAGE_DONT_CLOSE_ON_BACK))
    nav_close(np);
}


/**
 *
 */
void *
nav_page_create(const char *url, size_t allocsize,
		void (*closefunc)(struct nav_page *np), int flags)
{
  nav_page_t *np = calloc(1, allocsize);

  np->np_flags = flags;
  np->np_url = strdup(url);
  np->np_close = closefunc;

  TAILQ_INSERT_TAIL(&nav_pages, np, np_global_link);

  np->np_prop_root = prop_create(NULL, "page");

  prop_set_string(prop_create(np->np_prop_root, "url"), url);
  return np;
}


/**
 *
 */
static int
nav_input_event(event_t *e, void *opaque)
{
  event_generic_t *g;

  switch(e->e_type) {
  case EVENT_GENERIC:
    g = (event_generic_t *)e;
    if(!strcmp(g->target, "navigator"))
      break;
    return 0;

  case EVENT_BACKSPACE:
    break;

  default:
    return 0;
  }

  event_enqueue(&nav_eq, e);
  return 1;
}


/**
 *
 */
static void *
navigator_thread(void *aux)
{
  event_t *e;
  event_generic_t *g;
 
  while(1) {
    e = event_get(-1, &nav_eq);

    switch(e->e_type) {
    default:
      break;
      
    case EVENT_BACKSPACE:
      nav_back();
      break;


    case EVENT_GENERIC:
      g = (event_generic_t *)e;
    
      if(!strcmp(g->method, "open"))
	nav_open(g->argument);
      
      if(!strcmp(g->method, "back"))
	nav_back();
      break;
    }

    event_unref(e);
  }
}


/**
 *
 */
event_t *
nav_play_video( const char *url, struct media_pipe *mp,
	       char *errbuf, size_t errlen)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_play_video == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->nb_play_video(url, mp, errbuf, errlen);
}




/**
 * Static content
 */
static int
be_page_canhandle(const char *url)
{
  return !strncmp(url, "page://", strlen("page://"));
}


/**
 *
 */
static int
be_page_open(const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *n = nav_page_create(url0, sizeof(nav_page_t), NULL, 0);
  prop_t *p = n->np_prop_root;
  prop_set_string(prop_create(p, "type"), url0 + strlen("page://"));
  *npp = n;
  return 0;
}


/**
 *
 */
nav_backend_t be_page = {
  .nb_canhandle = be_page_canhandle,
  .nb_open = be_page_open,
};


