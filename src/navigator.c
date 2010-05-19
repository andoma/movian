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
#include "backend/backend.h"
#include "event.h"
#include "notifications.h"

TAILQ_HEAD(nav_page_queue, nav_page);

/**
 *
 */
typedef struct navigator {

  struct nav_page_queue nav_pages;
  struct nav_page_queue nav_history;

  nav_page_t *nav_page_current;

  prop_t *nav_prop_root;
  prop_t *nav_prop_pages;
  prop_t *nav_prop_curpage;
  prop_t *nav_prop_can_go_back;
  prop_t *nav_prop_can_go_fwd;
  prop_t *nav_prop_can_go_home;

  prop_courier_t *nav_pc;

  prop_sub_t *nav_eventsink;
  prop_sub_t *nav_dtor_tracker;

} navigator_t;

static void nav_eventsink(void *opaque, prop_event_t event, ...);

static void nav_dtor_tracker(void *opaque, prop_event_t event, ...);

/**
 *
 */
static navigator_t *
nav_create(prop_t *parent)
{
  navigator_t *nav = calloc(1, sizeof(navigator_t));

  TAILQ_INIT(&nav->nav_pages);
  TAILQ_INIT(&nav->nav_history);

  nav->nav_prop_root        = prop_create(parent, "nav");
  nav->nav_prop_pages       = prop_create(nav->nav_prop_root, "pages");
  nav->nav_prop_curpage     = prop_create(nav->nav_prop_root, "currentpage");
  nav->nav_prop_can_go_back = prop_create(nav->nav_prop_root, "canGoBack");
  nav->nav_prop_can_go_fwd  = prop_create(nav->nav_prop_root, "canGoForward");
  nav->nav_prop_can_go_home = prop_create(nav->nav_prop_root, "canGoHome");
  prop_set_int(nav->nav_prop_can_go_home, 1);

  nav->nav_pc = prop_courier_create_thread(NULL, "navigator");

  nav->nav_eventsink = 
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "eventsink"),
		   PROP_TAG_CALLBACK, nav_eventsink, nav,
		   PROP_TAG_COURIER, nav->nav_pc,
		   PROP_TAG_ROOT, nav->nav_prop_root,
		   NULL);

  nav->nav_dtor_tracker =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, nav_dtor_tracker, nav,
		   PROP_TAG_COURIER, nav->nav_pc,
		   PROP_TAG_ROOT, nav->nav_prop_root,
		   NULL);

  return nav;
}


/**
 *
 */
prop_t *
nav_spawn(void)
{
  return nav_create(NULL)->nav_prop_root;
}


/**
 *
 */
void
nav_init(void)
{
  nav_create(prop_get_global());
}


/**
 *
 */
static void
nav_update_cango(navigator_t *nav)
{
  nav_page_t *np = nav->nav_page_current;

  if(np == NULL) {
    prop_set_int(nav->nav_prop_can_go_back, 0);
    prop_set_int(nav->nav_prop_can_go_fwd, 0);
    prop_set_int(nav->nav_prop_can_go_home, 1);
    return;
  }

  prop_set_int(nav->nav_prop_can_go_back,
	       !!TAILQ_PREV(np, nav_page_queue, np_history_link));
  prop_set_int(nav->nav_prop_can_go_fwd,
	       !!TAILQ_NEXT(np, np_history_link));
  prop_set_int(nav->nav_prop_can_go_home,
	       !!strcmp(np->np_url, NAV_HOME));
}


/**
 *
 */
static void
nav_remove_from_history(navigator_t *nav, nav_page_t *np)
{
  np->np_inhistory = 0;
  TAILQ_REMOVE(&nav->nav_history, np, np_history_link);
}


/**
 *
 */
static void
nav_close(nav_page_t *np, int with_prop)
{
  navigator_t *nav = np->np_nav;

  prop_unsubscribe(np->np_close_sub);

  if(nav->nav_page_current == np)
    nav->nav_page_current = NULL;

  if(np->np_inhistory)
    nav_remove_from_history(nav, np);

  TAILQ_REMOVE(&nav->nav_pages, np, np_global_link);

  if(with_prop) {
    prop_destroy(np->np_prop_root);
    nav_update_cango(nav);
  }
  free(np->np_url);
  free(np->np_view);
  free(np);
}


/**
 *
 */
static void
nav_close_all(navigator_t *nav, int with_prop)
{
  nav_page_t *np;

  while((np = TAILQ_LAST(&nav->nav_pages, nav_page_queue)) != NULL)
    nav_close(np, with_prop);
}


/**
 *
 */
static void
nav_open0(navigator_t *nav, const char *url, const char *view)
{
  nav_page_t *np, *np2;
  backend_t *be;
  char errbuf[128];
  char urlbuf[URL_MAX];

  be = backend_canhandle(url);

  if(be == NULL) {
    notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nNo handler for URL", url);
    return;
  }
  
  if(be->be_normalize != NULL && !be->be_normalize(url, urlbuf, sizeof(urlbuf)))
    url = urlbuf;

  TRACE(TRACE_DEBUG, "navigator", "Opening %s", url);

  /* First, if a page is already open, go directly to it */

  TAILQ_FOREACH(np, &nav->nav_pages, np_global_link) {
    if(!strcmp(np->np_url, url) && !strcmp(np->np_view ?:"" , view ?:"")) {
      prop_select(np->np_prop_root, 0);
      prop_link(np->np_prop_root, nav->nav_prop_curpage);
      break;
    }
  }

  if(np == NULL) {

    if((np = be->be_open(nav, url, view, errbuf, sizeof(errbuf))) == NULL) {
      notify_add(NOTIFY_ERROR, NULL, 5, "URL: %s\nError: %s", url, errbuf);
      return;
    }
  
    if(np == NULL)
      return;

    if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
      /* nav->nav_prop_pages is a zombie, this is an error */
      abort();
    }

    prop_link(np->np_prop_root, nav->nav_prop_curpage);

    prop_select(np->np_prop_root, 0);
  }

  if(np->np_inhistory == 0) {
    if(nav->nav_page_current != NULL) {
      
      /* Destroy any previous "future" histories,
       * this happens if we back a few times and then jumps away
       * in another "direction"
       */
      
      while((np2 = TAILQ_NEXT(nav->nav_page_current, np_history_link)) != NULL)
	nav_close(np2, 1);
    }

    TAILQ_INSERT_TAIL(&nav->nav_history, np, np_history_link);
    np->np_inhistory = 1;
  }

  nav->nav_page_current = np;
  nav_update_cango(nav);
}

/**
 *
 */
void
nav_open(const char *url)
{
  event_dispatch(event_create_openurl(url, NULL));
}


/**
 *
 */
static void
nav_back(navigator_t *nav)
{
  nav_page_t *prev, *np = nav->nav_page_current;

  if(np != NULL &&
     (prev = TAILQ_PREV(np, nav_page_queue, np_history_link)) != NULL) {

    nav_open0(nav, prev->np_url, prev->np_view);
    if(!(np->np_flags & NAV_PAGE_DONT_CLOSE_ON_BACK))
      nav_close(np, 1);
  }
}


/**
 *
 */
static void
nav_fwd(navigator_t *nav)
{
  nav_page_t *next, *np;

  np = nav->nav_page_current;

  if(np != NULL && (next = TAILQ_NEXT(np, np_history_link)) != NULL)
    nav_open0(nav, next->np_url, next->np_view);
}

/**
 *
 */
static void
nav_page_close_set(void *opaque, int value)
{
  nav_page_t *np = opaque, *next;
  navigator_t *nav = np->np_nav;

  if(!value)
    return;

  next = TAILQ_NEXT(np, np_history_link);

  nav_close(np, 1);

  if(next == NULL)
    next = TAILQ_LAST(&nav->nav_pages, nav_page_queue);

  if(next != NULL)
    nav_open0(nav, next->np_url, next->np_view);
}



/**
 *
 */
void *
nav_page_create(navigator_t *nav, const char *url, const char *view,
		size_t allocsize, int flags)
{
  nav_page_t *np = calloc(1, allocsize);

  np->np_nav = nav;
  np->np_url = url ? strdup(url) : NULL;
  TAILQ_INSERT_TAIL(&nav->nav_pages, np, np_global_link);

  np->np_prop_root = prop_create(NULL, "page");

  if(view != NULL) {
    np->np_view = strdup(view);
    prop_set_string(prop_create(np->np_prop_root, "view"), view);
    flags |= NAV_PAGE_PRESET_VIEW;
  }

  np->np_flags = flags;

  np->np_close_sub = 
    prop_subscribe(0,
		   PROP_TAG_ROOT, prop_create(np->np_prop_root, "close"),
		   PROP_TAG_CALLBACK_INT, nav_page_close_set, np,
		   PROP_TAG_COURIER, nav->nav_pc,
		   NULL);

  if(url != NULL)
    prop_set_string(prop_create(np->np_prop_root, "url"), url);
  return np;
}


/**
 *
 */
static void
nav_eventsink(void *opaque, prop_event_t event, ...)
{
  navigator_t *nav = opaque;
  event_t *e;
  event_openurl_t *ou;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_EXT_EVENT)
    return;

  e = va_arg(ap, event_t *);
  
  if(event_is_action(e, ACTION_NAV_BACK)) {
    nav_back(nav);

  } else if(event_is_action(e, ACTION_NAV_FWD)) {
    nav_fwd(nav);

  } else if(event_is_action(e, ACTION_HOME)) {
    nav_open0(nav, NAV_HOME, NULL);

  } else if(event_is_type(e, EVENT_OPENURL)) {
    ou = (event_openurl_t *)e;
    nav_open0(nav, ou->url, ou->view);
  }
}


/**
 *
 */
static void
nav_dtor_tracker(void *opaque, prop_event_t event, ...)
{
  navigator_t *nav = opaque;

  if(event != PROP_DESTROYED)
    return;

  prop_unsubscribe(nav->nav_eventsink);
  prop_unsubscribe(nav->nav_dtor_tracker);

  nav_close_all(nav, 0);

  prop_courier_stop(nav->nav_pc);
  free(nav);
}
