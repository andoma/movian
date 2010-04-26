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
#include "backend.h"
#include "event.h"
#include "notifications.h"

static hts_mutex_t nav_mutex;

static struct nav_page_queue nav_pages;
static struct nav_page_queue nav_history;

static nav_page_t *nav_page_current;

static prop_t *nav_prop_root;
static prop_t *nav_prop_pages;
static prop_t *nav_prop_curpage;
static prop_t *nav_prop_can_go_back;
static prop_t *nav_prop_can_go_fwd;
static prop_t *nav_prop_can_go_home;

static void nav_eventsink(void *opaque, prop_event_t event, ...);

/**
 *
 */
void
nav_init(void)
{
  prop_courier_t *pc;

  hts_mutex_init(&nav_mutex);

  TAILQ_INIT(&nav_pages);
  TAILQ_INIT(&nav_history);

  nav_prop_root    = prop_create(prop_get_global(), "nav");
  nav_prop_pages   = prop_create(nav_prop_root, "pages");
  nav_prop_curpage = prop_create(nav_prop_root, "currentpage");
  nav_prop_can_go_back = prop_create(nav_prop_root, "canGoBack");
  nav_prop_can_go_fwd  = prop_create(nav_prop_root, "canGoForward");
  nav_prop_can_go_home = prop_create(nav_prop_root, "canGoHome");

  pc = prop_courier_create_thread(&nav_mutex, "navigator");

  prop_subscribe(0,
		 PROP_TAG_NAME("nav", "eventsink"),
		 PROP_TAG_CALLBACK, nav_eventsink, NULL,
		 PROP_TAG_COURIER, pc,
		 PROP_TAG_ROOT, nav_prop_root,
		 NULL);
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
    prop_set_int(nav_prop_can_go_home, 1);
    return;
  }

  prop_set_int(nav_prop_can_go_back,
	       !!TAILQ_PREV(np, nav_page_queue, np_history_link));
  prop_set_int(nav_prop_can_go_fwd,
	       !!TAILQ_NEXT(np, np_history_link));
  prop_set_int(nav_prop_can_go_home,
	       !!strcmp(np->np_url, NAV_HOME));
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
nav_open0(const char *url, const char *type, prop_t *psource)
{
  nav_page_t *np, *np2;
  backend_t *be;
  char errbuf[128];

  TRACE(TRACE_DEBUG, "navigator", "Opening %s", url);

  /* First, if a page is already open, go directly to it */

  TAILQ_FOREACH(np, &nav_pages, np_global_link) {
    if(!strcmp(np->np_url, url)) {
      prop_select(np->np_prop_root, 0);
      prop_link(np->np_prop_root, nav_prop_curpage);
      break;
    }
  }

  if(np == NULL) {

    be = backend_canhandle(url);

    if(be == NULL) {
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nNo handler for URL", url);
      return;
    }

    if(be->be_open(url, type, psource, &np, errbuf, sizeof(errbuf))) {
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
nav_open(const char *url, const char *type, prop_t *psource)
{
  event_dispatch(event_create_openurl(url, type, psource));
}


/**
 *
 */
static void
nav_back(void)
{
  nav_page_t *prev, *np;

  np = nav_page_current;

  if(np != NULL &&
     (prev = TAILQ_PREV(np, nav_page_queue, np_history_link)) != NULL) {

    nav_open0(prev->np_url, NULL, NULL);
    if(!(np->np_flags & NAV_PAGE_DONT_CLOSE_ON_BACK))
      nav_close(np);
  }
}


/**
 *
 */
static void
nav_fwd(void)
{
  nav_page_t *next, *np;

  np = nav_page_current;

  if(np != NULL && (next = TAILQ_NEXT(np, np_history_link)) != NULL)
    nav_open0(next->np_url, NULL, NULL);
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
static void
nav_eventsink(void *opaque, prop_event_t event, ...)
{
  event_t *e;
  event_openurl_t *ou;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_EXT_EVENT)
    return;

  e = va_arg(ap, event_t *);
  
  if(event_is_action(e, ACTION_NAV_BACK)) {
    nav_back();

  } else if(event_is_action(e, ACTION_NAV_FWD)) {
    nav_fwd();

  } else if(event_is_action(e, ACTION_HOME)) {
    nav_open0(NAV_HOME, NULL, NULL);

  } else if(event_is_type(e, EVENT_OPENURL)) {
    ou = (event_openurl_t *)e;
    nav_open0(ou->url, ou->type, ou->psource);
  }
}
