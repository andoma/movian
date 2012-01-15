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
#include "plugins.h"

TAILQ_HEAD(nav_page_queue, nav_page);


/**
 *
 */
typedef struct nav_page {
  struct navigator *np_nav;

  TAILQ_ENTRY(nav_page) np_global_link;
  TAILQ_ENTRY(nav_page) np_history_link;
  int np_inhistory;

  prop_t *np_prop_root;
  char *np_url;
  char *np_view;

  int np_direct_close;

  prop_sub_t *np_close_sub;

  prop_sub_t *np_direct_close_sub;

} nav_page_t;


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
nav_create(prop_t *prop)
{
  navigator_t *nav = calloc(1, sizeof(navigator_t));
  nav->nav_prop_root = prop;

  TAILQ_INIT(&nav->nav_pages);
  TAILQ_INIT(&nav->nav_history);

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
  return nav_create(prop_create_root("nav"))->nav_prop_root;
}


/**
 *
 */
void
nav_init(void)
{
  nav_create(prop_create(prop_get_global(), "nav"));
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
  prop_unsubscribe(np->np_direct_close_sub);

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
nav_select(navigator_t *nav, nav_page_t *np, prop_t *origin)
{
  prop_link(np->np_prop_root, nav->nav_prop_curpage);
  prop_select_ex(np->np_prop_root, origin, NULL);
  nav->nav_page_current = np;
  nav_update_cango(nav);
}


/**
 *
 */
static void
nav_insert_page(navigator_t *nav, nav_page_t *np, prop_t *origin)
{
  nav_page_t *np2;

  if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
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
  nav_select(nav, np, origin);
}



/**
 *
 */
static void
nav_page_close_set(void *opaque, int value)
{
  nav_page_t *np = opaque, *np2;
  navigator_t *nav = np->np_nav;
  if(!value)
    return;

  if(nav->nav_page_current == np) {
    np2 = TAILQ_PREV(np, nav_page_queue, np_history_link);
    nav_select(nav, np2, NULL);
  }

  nav_close(np, 1);
}


/**
 *
 */
static void
nav_page_direct_close_set(void *opaque, int v)
{
  nav_page_t *np = opaque;
  np->np_direct_close = v;
}


/**
 *
 */
static void
nav_page_setup_prop(navigator_t *nav, nav_page_t *np, const char *view)
{
  np->np_prop_root = prop_create_root("page");
  if(view != NULL) {
    np->np_view = strdup(view);
    prop_set_string(prop_create(np->np_prop_root, "requestedView"), view);
  }

  // XXX Change this into event-style subscription
  np->np_close_sub = 
    prop_subscribe(0,
		   PROP_TAG_ROOT, prop_create(np->np_prop_root, "close"),
		   PROP_TAG_CALLBACK_INT, nav_page_close_set, np,
		   PROP_TAG_COURIER, nav->nav_pc,
		   NULL);

  prop_set_string(prop_create(np->np_prop_root, "url"), np->np_url);

  np->np_direct_close_sub = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_ROOT, prop_create(np->np_prop_root, "directClose"),
		   PROP_TAG_CALLBACK_INT, nav_page_direct_close_set, np,
		   PROP_TAG_COURIER, nav->nav_pc,
		   NULL);
}


/**
 *
 */
static void
nav_open0(navigator_t *nav, const char *url, const char *view, prop_t *origin)
{
  nav_page_t *np = calloc(1, sizeof(nav_page_t));

  TRACE(TRACE_INFO, "navigator", "Opening %s", url);

  np->np_nav = nav;
  np->np_url = strdup(url);
  np->np_direct_close = 0;
  TAILQ_INSERT_TAIL(&nav->nav_pages, np, np_global_link);

  nav_page_setup_prop(nav, np, view);

  nav_insert_page(nav, np, origin);
  if(backend_open(np->np_prop_root, url))
    nav_open_errorf(np->np_prop_root, _("No handler for URL"));
}


/**
 *
 */
void
nav_open(const char *url, const char *view)
{
  event_dispatch(event_create_openurl(url, view, NULL));
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

    nav_select(nav, prev, NULL);

    if(np->np_direct_close)
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
    nav_select(nav, next, NULL);
}


/**
 *
 */
static void
nav_reload_current(navigator_t *nav)
{
  nav_page_t *np;

  if((np = nav->nav_page_current) == NULL)
    return;

  plugins_reload_dev_plugin();

  TRACE(TRACE_INFO, "navigator", "Reloading %s", np->np_url);

  prop_unsubscribe(np->np_close_sub);
  prop_unsubscribe(np->np_direct_close_sub);
  prop_destroy(np->np_prop_root);
  nav_page_setup_prop(nav, np, NULL);

  if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  nav_select(nav, np, NULL);
    
  if(backend_open(np->np_prop_root, np->np_url))
    nav_open_errorf(np->np_prop_root, _("No handler for URL"));
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
    nav_open0(nav, NAV_HOME, NULL, NULL);

  } else if(event_is_action(e, ACTION_RELOAD_DATA)) {
    nav_reload_current(nav);

  } else if(event_is_type(e, EVENT_OPENURL)) {
    ou = (event_openurl_t *)e;
    if(ou->url != NULL)
      nav_open0(nav, ou->url, ou->view, ou->origin);
    else
      TRACE(TRACE_INFO, "Navigator", "Tried to open NULL URL");
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


/**
 *
 */
int
nav_open_error(prop_t *root, const char *msg)
{
  prop_t *model = prop_create(root, "model");
  prop_set_string(prop_create(model, "type"), "openerror");
  prop_set_int(prop_create(model, "loading"), 0);
  prop_set_string(prop_create(model, "error"), msg);
  prop_set_int(prop_create(root, "directClose"), 1);
  return 0;
}

/**
 *
 */
int
nav_open_errorf(prop_t *root, rstr_t *fmt, ...)
{
  va_list ap;
  char buf[200];

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), rstr_get(fmt), ap);
  va_end(ap);
  rstr_release(fmt);
  return nav_open_error(root, buf);
}
