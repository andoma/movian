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

#include "config.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "showtime.h"
#include "navigator.h"
#include "event.h"
#include "notifications.h"

static hts_mutex_t nav_mutex;

static struct nav_backend_list nav_backends;
static struct nav_page_queue nav_pages;
static struct nav_page_queue nav_history;

static nav_page_t *nav_page_current;

static prop_t *nav_prop_root;
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
  if(be->nb_init != NULL && be->nb_init())
    return;
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
  nav_prop_pages   = prop_create(nav_prop_root, "pages");
  nav_prop_curpage = prop_create(nav_prop_root, "currentpage");

  event_initqueue(&nav_eq);

#define NAV_INIT_BE(name) \
 {extern nav_backend_t be_ ## name; nav_init_be(&be_ ## name);}

  NAV_INIT_BE(page);
  NAV_INIT_BE(file);
  NAV_INIT_BE(settings);
  NAV_INIT_BE(playqueue);
  NAV_INIT_BE(htsp);
#ifdef CONFIG_SPOTIFY
  NAV_INIT_BE(spotify);
#endif

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
static void
nav_open0(const char *url)
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
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nNo handler for URL", url);
      return;
    }

    if(nb->nb_open(url, &np, errbuf, sizeof(errbuf))) {
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nError: %s", url, errbuf);
      return;
    }
  
    if(np == NULL)
      return;

    if(prop_set_parent(np->np_prop_root, nav_prop_pages)) {
      /* nav_prop_pages is a zombie, this is an error */
      abort();
    }

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
void
nav_open(const char *url, int flags)
{
  if(flags & NAV_OPEN_ASYNC)
    event_enqueue(&nav_eq, event_create_url(EVENT_OPENURL, url));
  else
    nav_open0(url);
}


/**
 *
 */
void
nav_back(void)
{
  nav_page_t *prev, *np = nav_page_current;

  if(np == NULL ||
     (prev = TAILQ_PREV(np, nav_page_queue, np_history_link)) == NULL)
     return;

  nav_open0(prev->np_url);

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
  case EVENT_MAINMENU:
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

    case EVENT_MAINMENU:
      nav_open0("page://mainmenu");
      break;

    case EVENT_OPENURL:
      nav_open0(e->e_payload);
      break;

    case EVENT_GENERIC:
      g = (event_generic_t *)e;
    
      if(!strcmp(g->method, "open"))
	nav_open0(g->argument);
      
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
nav_play_video(const char *url, struct media_pipe *mp,
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
 *
 */
event_t *
nav_play_audio(const char *url, struct media_pipe *mp,
	       char *errbuf, size_t errlen)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_play_audio == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->nb_play_audio(url, mp, errbuf, errlen);
}


/**
 *
 */
unsigned int nav_probe(prop_t *proproot, const char *url,
		       char *newurl, size_t newurlsize,
		       char *errbuf, size_t errsize)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_probe == NULL) {
    snprintf(errbuf, errsize, "No backend for URL");
    return CONTENT_UNKNOWN;
  }
  return nb->nb_probe(proproot, url, newurl, newurlsize, errbuf, errsize);
}


/**
 *
 */
nav_dir_t *
nav_scandir(const char *url, char *errbuf, size_t errlen)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_scandir == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->nb_scandir(url, errbuf, errlen);
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



/**
 *
 */
nav_dir_t *
nav_dir_alloc(void)
{
  nav_dir_t *nd = malloc(sizeof(nav_dir_t));
  TAILQ_INIT(&nd->nd_entries);
  nd->nd_count = 0;
  return nd;
}

/**
 *
 */
void
nav_dir_free(nav_dir_t *nd)
{
  nav_dir_entry_t *nde;

  while((nde = TAILQ_FIRST(&nd->nd_entries)) != NULL) {
    TAILQ_REMOVE(&nd->nd_entries, nde, nde_link);
    if(nde->nde_metadata)
      prop_destroy(nde->nde_metadata);

    free(nde->nde_filename);
    free(nde->nde_url);
    free(nde);
  }
  free(nd);
}

/**
 *
 */
void
nav_dir_add(nav_dir_t *nd, const char *url, const char *filename, int type,
	    prop_t *metadata)
{
  nav_dir_entry_t *nde;

  if(filename[0] == '.')
    return; /* Skip all dot-filenames */

  nde = malloc(sizeof(nav_dir_entry_t));

  nde->nde_url      = strdup(url);
  nde->nde_filename = strdup(filename);
  nde->nde_type     = type;
  nde->nde_metadata = metadata;
  nde->nde_opaque   = NULL;
  TAILQ_INSERT_TAIL(&nd->nd_entries, nde, nde_link);
  nd->nd_count++;
}



static int 
nav_dir_sort_compar(const void *A, const void *B)
{
  const nav_dir_entry_t *a = *(nav_dir_entry_t * const *)A;
  const nav_dir_entry_t *b = *(nav_dir_entry_t * const *)B;

  return strcasecmp(a->nde_filename, b->nde_filename);
}

/**
 *
 */
void
nav_dir_sort(nav_dir_t *nd)
{
  nav_dir_entry_t **v;
  nav_dir_entry_t *nde;
  int i = 0;

  if(nd->nd_count == 0)
    return;

  v = malloc(nd->nd_count * sizeof(nav_dir_entry_t *));

  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link)
    v[i++] = nde;

  qsort(v, nd->nd_count, sizeof(nav_dir_entry_t *), nav_dir_sort_compar);

  TAILQ_INIT(&nd->nd_entries);
  for(i = 0; i < nd->nd_count; i++)
    TAILQ_INSERT_TAIL(&nd->nd_entries, v[i], nde_link);
  
  free(v);
}

