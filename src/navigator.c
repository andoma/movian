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
#include "backend/backend_prop.h"
#include "event.h"
#include "plugins.h"
#include "service.h"
#include "settings.h"
#include "notifications.h"
#include "misc/str.h"
#include "db/kvstore.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/fa_vfs.h"

TAILQ_HEAD(nav_page_queue, nav_page);
LIST_HEAD(bookmark_list, bookmark);
LIST_HEAD(navigator_list, navigator);

static prop_courier_t *nav_courier;
static prop_t *bookmark_root;
static prop_t *bookmark_nodes;

static void bookmarks_init(void);
static void bookmark_add(const char *title, const char *url, const char *type,
			 const char *icon, const char *id, int vfs);
static void bookmarks_save(void);

static struct bookmark_list bookmarks;
static struct navigator_list navigators;

/**
 *
 */
typedef struct bookmark {
  LIST_ENTRY(bookmark) bm_link;
  prop_t *bm_root;
  prop_sub_t *bm_title_sub;
  prop_sub_t *bm_url_sub;
  prop_sub_t *bm_type_sub;
  prop_sub_t *bm_icon_sub;
  prop_sub_t *bm_vfs_sub;

  rstr_t *bm_id;
  rstr_t *bm_title;
  rstr_t *bm_url;
  rstr_t *bm_type;
  rstr_t *bm_icon;

  int bm_vfs;
  int bm_vfs_id;

  service_t *bm_service;

  setting_t *bm_type_setting;
  setting_t *bm_vfs_setting;
  setting_t *bm_delete;

  prop_t *bm_info;


} bookmark_t;

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

  int np_direct_close;

  prop_sub_t *np_close_sub;
  prop_sub_t *np_eventsink_sub;

  prop_sub_t *np_direct_close_sub;

  prop_t *np_opened_from;

  // For bookmarking

  prop_sub_t *np_bookmarked_sub;
  prop_t *np_bookmarked;

  prop_sub_t *np_title_sub;
  rstr_t *np_title;

  prop_sub_t *np_icon_sub;
  rstr_t *np_icon;

} nav_page_t;


/**
 *
 */
typedef struct navigator {

  LIST_ENTRY(navigator) nav_link;
  
  struct nav_page_queue nav_pages;
  struct nav_page_queue nav_history;

  nav_page_t *nav_page_current;

  prop_t *nav_prop_root;
  prop_t *nav_prop_pages;
  prop_t *nav_prop_curpage;
  prop_t *nav_prop_can_go_back;
  prop_t *nav_prop_can_go_fwd;
  prop_t *nav_prop_can_go_home;

  prop_sub_t *nav_eventsink;
  prop_sub_t *nav_dtor_tracker;

} navigator_t;

static void nav_eventsink(void *opaque, event_t *e);

static void nav_dtor_tracker(void *opaque, prop_event_t event, ...);

static void nav_open0(navigator_t *nav, const char *url, const char *view,
		      prop_t *origin, prop_t *model, const char *how);

static void page_redirect(nav_page_t *np, const char *url);

/**
 *
 */
static int
nav_page_is_bookmarked(nav_page_t *np)
{
  bookmark_t *bm;
  LIST_FOREACH(bm, &bookmarks, bm_link)
    if(bm->bm_url != NULL && !strcmp(np->np_url, rstr_get(bm->bm_url)))
      return 1;
  return 0;
}


/**
 *
 */
static void
nav_update_bookmarked(void)
{
  navigator_t *nav;

  LIST_FOREACH(nav, &navigators, nav_link) {
    nav_page_t *np;
    TAILQ_FOREACH(np, &nav->nav_pages, np_global_link) {
      prop_set_int_ex(np->np_bookmarked, 
		      np->np_bookmarked_sub, nav_page_is_bookmarked(np));
    }
  }
}




/**
 *
 */
static navigator_t *
nav_create(prop_t *prop)
{
  navigator_t *nav = calloc(1, sizeof(navigator_t));

  LIST_INSERT_HEAD(&navigators, nav, nav_link);

  nav->nav_prop_root = prop;

  TAILQ_INIT(&nav->nav_pages);
  TAILQ_INIT(&nav->nav_history);

  nav->nav_prop_pages       = prop_create(nav->nav_prop_root, "pages");
  nav->nav_prop_curpage     = prop_create(nav->nav_prop_root, "currentpage");
  nav->nav_prop_can_go_back = prop_create(nav->nav_prop_root, "canGoBack");
  nav->nav_prop_can_go_fwd  = prop_create(nav->nav_prop_root, "canGoForward");
  nav->nav_prop_can_go_home = prop_create(nav->nav_prop_root, "canGoHome");
  prop_t *eventsink         = prop_create(nav->nav_prop_root, "eventsink");
  prop_set_int(nav->nav_prop_can_go_home, 1);

  nav->nav_eventsink =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_EVENT, nav_eventsink, nav,
		   PROP_TAG_COURIER, nav_courier,
		   PROP_TAG_ROOT, eventsink,
		   NULL);

  nav->nav_dtor_tracker =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, nav_dtor_tracker, nav,
		   PROP_TAG_COURIER, nav_courier,
		   PROP_TAG_ROOT, nav->nav_prop_root,
		   NULL);

  nav_open0(nav, NAV_HOME, NULL, NULL, NULL, NULL);

  static int initial_opened = 0;

  if(atomic_add(&initial_opened, 1) == 0 && gconf.initial_url != NULL) {

    hts_mutex_lock(&gconf.state_mutex);
    while(gconf.state_plugins_loaded == 0)
      hts_cond_wait(&gconf.state_cond, &gconf.state_mutex);
    hts_mutex_unlock(&gconf.state_mutex);

    event_t *e = event_create_openurl(gconf.initial_url, gconf.initial_view,
                                      NULL, NULL, NULL);
    prop_send_ext_event(eventsink, e);
    event_release(e);
  }

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
  nav_courier = prop_courier_create_thread(NULL, "navigator");
  bookmarks_init();
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
page_unsub(nav_page_t *np)
{
  prop_unsubscribe(np->np_close_sub);
  prop_unsubscribe(np->np_direct_close_sub);
  prop_unsubscribe(np->np_bookmarked_sub);
  prop_unsubscribe(np->np_title_sub);
  prop_unsubscribe(np->np_icon_sub);
  prop_unsubscribe(np->np_eventsink_sub);
}


/**
 *
 */
static void
nav_close(nav_page_t *np, int with_prop)
{
  navigator_t *nav = np->np_nav;

  page_unsub(np);

  if(nav->nav_page_current == np)
    nav->nav_page_current = NULL;

  if(np->np_inhistory)
    nav_remove_from_history(nav, np);

  TAILQ_REMOVE(&nav->nav_pages, np, np_global_link);

  if(with_prop) {
    prop_destroy(np->np_prop_root);
    nav_update_cango(nav);
  }
  prop_ref_dec(np->np_opened_from);
  rstr_release(np->np_title);
  free(np->np_url);
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
nav_page_bookmarked_set(void *opaque, int v)
{
  nav_page_t *np = opaque;

  if(v) {
    if(nav_page_is_bookmarked(np))
      return;

    const char *title = rstr_get(np->np_title) ?: "<no title>";

    notify_add(NULL, NOTIFY_INFO, NULL, 3, _("Added new bookmark: %s"), title);

    bookmark_add(title, np->np_url, "other", rstr_get(np->np_icon), NULL, 0);

  } else {
    bookmark_t *bm;
    LIST_FOREACH(bm, &bookmarks, bm_link) {
      if(!strcmp(rstr_get(bm->bm_url), np->np_url)) {
	notify_add(NULL, NOTIFY_INFO, NULL, 3, _("Removed bookmark: %s"),
		   rstr_get(bm->bm_title));
	prop_destroy(bm->bm_root);
      }
    }
  }
  bookmarks_save();
}


/**
 *
 */
static void
nav_page_title_set(void *opaque, rstr_t *str)
{
  nav_page_t *np = opaque;
  rstr_set(&np->np_title, str);
}


/**
 *
 */
static void
page_eventsink(void *opaque, event_t *e)
{
  nav_page_t *np = opaque;
  if(event_is_type(e, EVENT_REDIRECT)) {
    page_redirect(np, e->e_payload);
  }
}


/**
 *
 */
static void
nav_page_icon_set(void *opaque, rstr_t *str)
{
  nav_page_t *np = opaque;
  rstr_set(&np->np_icon, str);
}


/**
 *
 */
static void
nav_page_setup_prop(navigator_t *nav, nav_page_t *np, const char *view,
		    const char *how)
{
  np->np_prop_root = prop_create_root("page");

  kv_prop_bind_create(prop_create(np->np_prop_root, "persistent"),
		      np->np_url);

  if(np->np_opened_from)
    prop_link(np->np_opened_from, prop_create(np->np_prop_root, "openedFrom"));

  if(view != NULL)
    prop_set_string(prop_create(np->np_prop_root, "requestedView"), view);

  if(how != NULL)
    prop_set_string(prop_create(np->np_prop_root, "how"), how);

  // XXX Change this into event-style subscription
  np->np_close_sub = 
    prop_subscribe(0,
		   PROP_TAG_ROOT, np->np_prop_root,
		   PROP_TAG_NAME("page", "close"),
		   PROP_TAG_CALLBACK_INT, nav_page_close_set, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);

  prop_set_string(prop_create(np->np_prop_root, "url"), np->np_url);

  np->np_direct_close_sub = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_ROOT, np->np_prop_root,
		   PROP_TAG_NAME("page", "directClose"),
		   PROP_TAG_CALLBACK_INT, nav_page_direct_close_set, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);

  np->np_eventsink_sub = 
    prop_subscribe(0,
		   PROP_TAG_ROOT, np->np_prop_root,
		   PROP_TAG_NAME("page", "eventSink"),
		   PROP_TAG_CALLBACK_EVENT, page_eventsink, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);


  np->np_bookmarked = prop_create(np->np_prop_root, "bookmarked");

  prop_set_int(np->np_bookmarked, nav_page_is_bookmarked(np));

  np->np_bookmarked_sub = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
		   PROP_TAG_ROOT, np->np_bookmarked,
		   PROP_TAG_CALLBACK_INT, nav_page_bookmarked_set, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);

  np->np_title_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "model", "metadata", "title"),
		   PROP_TAG_ROOT, np->np_prop_root,
		   PROP_TAG_CALLBACK_RSTR, nav_page_title_set, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);

  np->np_icon_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "model", "metadata", "logo"),
		   PROP_TAG_ROOT, np->np_prop_root,
		   PROP_TAG_CALLBACK_RSTR, nav_page_icon_set, np,
		   PROP_TAG_COURIER, nav_courier,
		   NULL);
}


/**
 *
 */
static void
nav_open0(navigator_t *nav, const char *url, const char *view, prop_t *origin,
	  prop_t *model, const char *how)
{
  nav_page_t *np = calloc(1, sizeof(nav_page_t));

  TRACE(TRACE_INFO, "navigator", "Opening %s", url);
  np->np_nav = nav;
  np->np_url = strdup(url);
  np->np_opened_from = prop_ref_inc(model);
  np->np_direct_close = 0;
  TAILQ_INSERT_TAIL(&nav->nav_pages, np, np_global_link);

  nav_page_setup_prop(nav, np, view, how);

  nav_insert_page(nav, np, origin);

  if(backend_open(np->np_prop_root, url, 0))
    nav_open_errorf(np->np_prop_root, _("No handler for URL"));
}


/**
 *
 */
void
nav_open(const char *url, const char *view)
{
  event_dispatch(event_create_openurl(url, view, NULL, NULL, NULL));
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

  page_unsub(np);

  prop_destroy(np->np_prop_root);
  nav_page_setup_prop(nav, np, NULL, "continue");

  if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  nav_select(nav, np, NULL);
    
  if(backend_open(np->np_prop_root, np->np_url, 0))
    nav_open_errorf(np->np_prop_root, _("No handler for URL"));
}


/**
 *
 */
static void
page_redirect(nav_page_t *np, const char *url)
{
  navigator_t *nav = np->np_nav;

  TRACE(TRACE_DEBUG, "navigator", "Following redirect to %s", url);
  prop_t *p = np->np_prop_root;

  page_unsub(np);
  prop_destroy_childs(np->np_prop_root);
  mystrset(&np->np_url, url);

  nav_page_setup_prop(nav, np, NULL, NULL);

  if(prop_set_parent_ex(np->np_prop_root, nav->nav_prop_pages,
                        p, NULL)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  if(nav->nav_page_current == np)
    nav_select(nav, np, NULL);
  prop_destroy(p);

  if(backend_open(np->np_prop_root, url, 0))
    nav_open_errorf(np->np_prop_root, _("No handler for URL"));
}


/**
 *
 */
static void
nav_eventsink(void *opaque, event_t *e)
{
  navigator_t *nav = opaque;
  event_openurl_t *ou;

  if(event_is_action(e, ACTION_NAV_BACK)) {
    nav_back(nav);

  } else if(event_is_action(e, ACTION_NAV_FWD)) {
    nav_fwd(nav);

  } else if(event_is_action(e, ACTION_HOME)) {
    nav_open0(nav, NAV_HOME, NULL, NULL, NULL, NULL);

  } else if(event_is_action(e, ACTION_PLAYQUEUE)) {
    nav_open0(nav, "playqueue:", NULL, NULL, NULL, NULL);

  } else if(event_is_action(e, ACTION_RELOAD_DATA)) {
    nav_reload_current(nav);

  } else if(event_is_type(e, EVENT_OPENURL)) {
    ou = (event_openurl_t *)e;
    if(ou->url != NULL)
      nav_open0(nav, ou->url, ou->view, ou->origin, ou->model, ou->how);
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
  LIST_REMOVE(nav, nav_link);
  free(nav);
}


/**
 *
 */
int
nav_open_error(prop_t *root, const char *msg)
{
  prop_t *model = prop_create_r(root, "model");
  prop_set(model, "type", PROP_SET_STRING, "openerror");
  prop_set(model, "loading", PROP_SET_INT, 0);
  prop_set(model, "error", PROP_SET_STRING, msg);
  prop_set(root, "directClose", PROP_SET_INT, 1);
  prop_ref_dec(model);
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



/**
 *
 */
static void
bookmarks_save(void)
{
  htsmsg_t *m = htsmsg_create_list();
  bookmark_t *bm;

  LIST_FOREACH(bm, &bookmarks, bm_link) {
    if(bm->bm_title == NULL || bm->bm_url == NULL || bm->bm_type == NULL)
      continue;
    htsmsg_t *b = htsmsg_create_map();
    htsmsg_add_str(b, "id", rstr_get(bm->bm_id));
    htsmsg_add_str(b, "title", rstr_get(bm->bm_title));
    htsmsg_add_str(b, "svctype", rstr_get(bm->bm_type));
    htsmsg_add_str(b, "url", rstr_get(bm->bm_url));
    if(bm->bm_icon)
      htsmsg_add_str(b, "icon", rstr_get(bm->bm_icon));
    if(bm->bm_vfs)
      htsmsg_add_u32(b, "vfs", bm->bm_vfs);
    htsmsg_add_msg(m, NULL, b);
  }

  htsmsg_store_save(m, "bookmarks2");
  htsmsg_destroy(m);
}


/**
 *
 */
static void
bookmark_destroyed(void *opaque, prop_event_t event, ...)
{
  bookmark_t *bm = opaque;
  prop_sub_t *s;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_DESTROYED)
    return;

  s = va_arg(ap, prop_sub_t *);

  prop_unsubscribe(bm->bm_title_sub);
  prop_unsubscribe(bm->bm_url_sub);
  prop_unsubscribe(bm->bm_type_sub);
  prop_unsubscribe(bm->bm_icon_sub);
  prop_unsubscribe(bm->bm_vfs_sub);

  rstr_release(bm->bm_id);
  rstr_release(bm->bm_title);
  rstr_release(bm->bm_url);
  rstr_release(bm->bm_type);
  rstr_release(bm->bm_icon);

  service_destroy(bm->bm_service);
  setting_destroy(bm->bm_type_setting);
  setting_destroy(bm->bm_delete);

  LIST_REMOVE(bm, bm_link);
  prop_ref_dec(bm->bm_root);

  prop_destroy(bm->bm_info);
  free(bm);

  bookmarks_save();
  prop_unsubscribe(s);
  nav_update_bookmarked();
}

/**
 *
 */
static void
update_vfs_mapping(bookmark_t *bm)
{
  if(bm->bm_vfs && bm->bm_title && rstr_get(bm->bm_title)[0]) {
    if(bm->bm_vfs_id)
      return;

    if(bm->bm_url && rstr_get(bm->bm_url)[0]) {
      bm->bm_vfs_id = vfs_add_mapping(rstr_get(bm->bm_title),
				      rstr_get(bm->bm_url));
    } else {
      vfs_del_mapping(bm->bm_vfs_id);
      bm->bm_vfs_id = 0;
    }
  } else {
    if(!bm->bm_vfs_id)
      return;
    vfs_del_mapping(bm->bm_vfs_id);
    bm->bm_vfs_id = 0;
  }
}

/**
 *
 */
static void
bm_set_title(void *opaque, rstr_t *str)
{
  bookmark_t *bm = opaque;

  rstr_set(&bm->bm_title, str);
  service_set_title(bm->bm_service, str);
  update_vfs_mapping(bm);
  bookmarks_save();
}


/**
 *
 */
static void
bm_set_url(void *opaque, rstr_t *str)
{
  bookmark_t *bm = opaque;

  rstr_set(&bm->bm_url, str);
  service_set_url(bm->bm_service, str);
  update_vfs_mapping(bm);
  bookmarks_save();
  nav_update_bookmarked();
}


/**
 *
 */
static void
bm_set_type(void *opaque, rstr_t *str)
{
  bookmark_t *bm = opaque;
  rstr_set(&bm->bm_type, str);
  service_set_type(bm->bm_service, str);
  bookmarks_save();
}


/**
 *
 */
static void
bm_set_icon(void *opaque, rstr_t *str)
{
  bookmark_t *bm = opaque;

  rstr_set(&bm->bm_icon, str);
  service_set_icon(bm->bm_service, str);
  bookmarks_save();
}


/**
 *
 */
static void
bm_set_vfs(void *opaque, int v)
{
  bookmark_t *bm = opaque;
  bm->bm_vfs = v;
  update_vfs_mapping(bm);
  bookmarks_save();
}


/**
 *
 */
static void
change_type(void *opaque, const char *string)
{
  bookmark_t *bm = opaque;
  prop_set_string(prop_create(prop_create(bm->bm_root, "metadata"),
			      "svctype"), string);
}


/**
 *
 */
static prop_sub_t *
add_prop(prop_t *parent, const char *name, rstr_t *value,
	 bookmark_t *bm, prop_callback_rstr_t *cb)
{
  prop_t *p = prop_create(prop_create(parent, "metadata"), name);
  prop_set_rstring(p, value);

  return prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
			PROP_TAG_CALLBACK_RSTR, cb, bm,
			PROP_TAG_ROOT, p, 
			PROP_TAG_COURIER, nav_courier,
			NULL);
}


/**
 *
 */
static void
bm_delete(void *opaque, prop_event_t event, ...)
{
  bookmark_t *bm = opaque;
  prop_destroy(bm->bm_root);
}



/**
 *
 */
static void
bookmark_add(const char *title, const char *url, const char *type,
	     const char *icon, const char *id, int vfs)
{
  if(title == NULL || url == NULL)
    return;

  bookmark_t *bm = calloc(1, sizeof(bookmark_t));

  LIST_INSERT_HEAD(&bookmarks, bm, bm_link);

  prop_t *p = prop_create_root(NULL);
  bm->bm_root = prop_ref_inc(p);

  prop_set_string(prop_create(p, "type"), "settings");

  bm->bm_title = rstr_alloc(title);
  bm->bm_url   = rstr_alloc(url);
  bm->bm_type  = rstr_alloc(type);
  bm->bm_icon  = rstr_alloc(icon);

  bm->bm_title_sub = add_prop(p, "title",   bm->bm_title, bm, bm_set_title);
  bm->bm_url_sub   = add_prop(p, "url",     bm->bm_url,   bm, bm_set_url);
  bm->bm_type_sub  = add_prop(p, "svctype", bm->bm_type,  bm, bm_set_type);
  bm->bm_icon_sub  = add_prop(p, "icon",    bm->bm_icon,  bm, bm_set_icon);

  if(id == NULL)
    bm->bm_id = get_random_string();
  else
    bm->bm_id = rstr_alloc(id);

  bm->bm_service = service_create(rstr_get(bm->bm_id),
				  title, url, type, icon, 1, 1,
				  SVC_ORIGIN_BOOKMARK);

  prop_link(service_get_status_prop(bm->bm_service),
	    prop_create(p, "status"));

  prop_link(service_get_statustxt_prop(bm->bm_service),
	    prop_create(p, "statustxt"));


  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE,
		 PROP_TAG_CALLBACK, bookmark_destroyed, bm,
		 PROP_TAG_ROOT, p,
		 PROP_TAG_COURIER, nav_courier,
		 NULL);

  // Construct the settings page

  prop_t *m = prop_create(p, "model");
  prop_t *md = prop_create(p, "metadata");
  rstr_t *r = backend_prop_make(m, NULL);
  prop_set_rstring(prop_create(p, "url"), r);
  rstr_release(r);

  prop_set_string(prop_create(m, "type"), "settings");
  prop_set_string(prop_create(m, "subtype"), "bookmark");

  prop_link(prop_create(md, "title"),
	    prop_create(prop_create(m, "metadata"), "title"));

  settings_create_bound_string(m, _p("Title"), prop_create(md, "title"));
  settings_create_bound_string(m, _p("URL"), prop_create(md, "url"));

  bm->bm_type_setting =
    setting_create(SETTING_MULTIOPT, m, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Type")),
                   SETTING_VALUE(type),
                   SETTING_OPTION("other",  _p("Other")),
                   SETTING_OPTION("music",  _p("Music")),
                   SETTING_OPTION("video",  _p("Video")),
                   SETTING_OPTION("tv",     _p("TV")),
                   SETTING_OPTION("photos", _p("Photos")),
                   SETTING_CALLBACK(change_type, bm),
                   SETTING_COURIER(nav_courier),
                   NULL);

  bm->bm_vfs_setting =
    setting_create(SETTING_BOOL, m, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Published in Virtual File System")),
                   SETTING_VALUE(vfs),
                   SETTING_CALLBACK(bm_set_vfs, bm),
                   SETTING_COURIER(nav_courier),
                   NULL);

  prop_link(prop_create(md, "url"), prop_create(md, "shortdesc"));

  bm->bm_info = prop_create_root(NULL);
  settings_create_info(m, NULL,
		       service_get_statustxt_prop(bm->bm_service));

  bm->bm_delete =
    settings_create_action(m, _p("Delete"), bm_delete, bm, 0, nav_courier);

  if(prop_set_parent(p, bookmark_nodes))
    abort();
  nav_update_bookmarked();
}


/**
 * Control function for bookmark parent. Here we create / destroy
 * entries.
 */
static void
bookmarks_callback(void *opaque, prop_event_t event, ...)
{
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_REQ_NEW_CHILD:
    bookmark_add("New bookmark", "none:", "other", NULL, NULL, 0);
    break;

  case PROP_REQ_DELETE_VECTOR:
    prop_vec_destroy_entries(va_arg(ap, prop_vec_t *));
    break;
  }
}


/**
 *
 */
static void
bookmark_load(htsmsg_t *o)
{
  bookmark_add(htsmsg_get_str(o, "title"),
	       htsmsg_get_str(o, "url"),
	       htsmsg_get_str(o, "svctype"),
	       htsmsg_get_str(o, "icon"),
	       htsmsg_get_str(o, "id"),
               htsmsg_get_u32_or_default(o, "vfs", 0));
}

/**
 *
 */
static void
bookmarks_init(void)
{
  htsmsg_field_t *f;
  htsmsg_t *m;

  bookmark_root = settings_add_dir(NULL, _p("Bookmarks"),
				   "bookmark", NULL,
				   _p("Add and remove items on homepage"),
				   "settings:bookmarks");

  bookmark_nodes = prop_create(bookmark_root, "nodes");
  prop_set_int(prop_create(bookmark_root, "mayadd"), 1);

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, bookmarks_callback, NULL,
		 PROP_TAG_ROOT, bookmark_nodes,
		 PROP_TAG_COURIER, nav_courier,
		 NULL);
  
  if((m = htsmsg_store_load("bookmarks")) != NULL) {
    htsmsg_t *n = htsmsg_get_map(m, "nodes");
    if(n != NULL) {
      HTSMSG_FOREACH(f, n) {
	htsmsg_t *o;
	if((o = htsmsg_get_map_by_field(f)) == NULL)
	  continue;
	htsmsg_t *p;
	if((p = htsmsg_get_map(o, "model")) != NULL)
	  o = p;

	bookmark_load(o);
      }
    }
    htsmsg_destroy(m);
    bookmarks_save();
    htsmsg_store_remove("bookmarks");
  } else if((m = htsmsg_store_load("bookmarks2")) != NULL) {
    HTSMSG_FOREACH(f, m) {
      htsmsg_t *o;
      if((o = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      bookmark_load(o);
    }
    htsmsg_destroy(m);
  }
}
