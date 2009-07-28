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
static prop_t *nav_prop_can_go_back;
static prop_t *nav_prop_can_go_fwd;
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
  nav_prop_can_go_back = prop_create(nav_prop_root, "canGoBack");
  nav_prop_can_go_fwd  = prop_create(nav_prop_root, "canGoForward");

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
nav_update_cango(void)
{
  nav_page_t *np = nav_page_current;

  if(np == NULL) {
    prop_set_int(nav_prop_can_go_back, 0);
    prop_set_int(nav_prop_can_go_fwd, 0);
    return;
  }

  prop_set_int(nav_prop_can_go_back,
	       !!TAILQ_PREV(np, nav_page_queue, np_history_link));
  prop_set_int(nav_prop_can_go_fwd,
	       !!TAILQ_NEXT(np, np_history_link));
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
  prop_unsubscribe(np->np_close_sub);

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

  nav_update_cango();
}


/**
 *
 */
static void
nav_open0(const char *url, const char *type, const char *parent)
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

    if(nb->nb_open(url, type, parent, &np, errbuf, sizeof(errbuf))) {
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
  nav_update_cango();
}

/**
 *
 */
void
nav_open(const char *url, const char *type, const char *parent, int flags)
{
  if(flags & NAV_OPEN_ASYNC)
    event_enqueue(&nav_eq, event_create_openurl(url, type, parent));
  else {
    hts_mutex_lock(&nav_mutex);
    nav_open0(url, type, parent);
    hts_mutex_unlock(&nav_mutex);
  }
}


/**
 *
 */
void
nav_back(int flags)
{
  nav_page_t *prev, *np;

  hts_mutex_lock(&nav_mutex);

  np = nav_page_current;

  if(np != NULL &&
     (prev = TAILQ_PREV(np, nav_page_queue, np_history_link)) != NULL) {

    if(flags & NAV_OPEN_ASYNC)
      event_enqueue(&nav_eq, event_create_openurl(prev->np_url, NULL, NULL));
    else 
      nav_open0(prev->np_url, NULL, NULL);

    if(!(np->np_flags & NAV_PAGE_DONT_CLOSE_ON_BACK))
      nav_close(np);
  }
  hts_mutex_unlock(&nav_mutex);
}


/**
 *
 */
void
nav_fwd(int flags)
{
  nav_page_t *next, *np;

  hts_mutex_lock(&nav_mutex);

  np = nav_page_current;

  if(np != NULL && (next = TAILQ_NEXT(np, np_history_link)) != NULL) {

    if(flags & NAV_OPEN_ASYNC)
      event_enqueue(&nav_eq, event_create_openurl(next->np_url, NULL, NULL));
    else 
      nav_open0(next->np_url, NULL, NULL);
  }
  hts_mutex_unlock(&nav_mutex);
}


/**
 *
 */
static void
nav_page_close_set(void *opaque, int value)
{
  nav_page_t *np = opaque, *next;
  
  if(!value)
    return;

  next = TAILQ_NEXT(np, np_history_link);

  nav_close(np);

  if(next == NULL)
    next = TAILQ_LAST(&nav_pages, nav_page_queue);

  if(next != NULL)
    nav_open0(next->np_url, NULL, NULL);
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

  np->np_close_sub = 
    prop_subscribe(0,
		   PROP_TAG_ROOT, prop_create(np->np_prop_root, "close"),
		   PROP_TAG_CALLBACK_INT, nav_page_close_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);

  prop_set_string(prop_create(np->np_prop_root, "url"), url);
  return np;
}


/**
 *
 */
static int
nav_input_event(event_t *e, void *opaque)
{
  if(event_is_type(e, EVENT_OPENURL) ||
     event_is_action(e, ACTION_BACKSPACE) ||
     event_is_action(e, ACTION_HOME)) {
    event_enqueue(&nav_eq, e);
    return 1;
  }
  return 0;
}


/**
 *
 */
static void *
navigator_thread(void *aux)
{
  event_t *e;
  event_openurl_t *ou;
 
  while(1) {
    e = event_get(-1, &nav_eq);

    if(event_is_action(e, ACTION_BACKSPACE)) {
      nav_back(0);
      
    } else if(event_is_action(e, ACTION_HOME)) {
      nav_open0("page://home", NULL, NULL);

    } else if(event_is_type(e, EVENT_OPENURL)) {
      ou = (event_openurl_t *)e;
      nav_open0(ou->url, ou->type, ou->parent);

    } else {
      abort();
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
prop_t *
nav_list(const char *url, char *errbuf, size_t errlen)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_list == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->nb_list(url, errbuf, errlen);
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
be_page_open(const char *url0, const char *type, const char *parent,
	     nav_page_t **npp, char *errbuf, size_t errlen)
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
int
nav_imageloader(const char *url, char *errbuf, size_t errlen,
		int *thumb, void **data, size_t *datasize,
		int *codecid, const char *theme, prop_pixmap_t **pixmap)
{
  nav_backend_t *nb;

  LIST_FOREACH(nb, &nav_backends, nb_global_link)
    if(nb->nb_canhandle(url))
      break;
  
  if(nb == NULL || nb->nb_imageloader == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return -1;
  }
  return nb->nb_imageloader(url, errbuf, errlen, thumb, data, 
			    datasize, codecid, theme, pixmap);
}
