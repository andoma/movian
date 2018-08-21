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
#include "config.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "main.h"
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
#include "prop/prop_linkselected.h"

TAILQ_HEAD(nav_page_queue, nav_page);
LIST_HEAD(navigator_list, navigator);
static prop_t *all_navigators;
static struct navigator_list navigators;

static HTS_MUTEX_DECL(nav_mutex);

#if ENABLE_BOOKMARKS

LIST_HEAD(bookmark_list, bookmark);
LIST_HEAD(bookmark_query_list, bookmark_query);

static prop_t *bookmark_nodes;
static struct bookmark_query_list bookmark_queries;

static void bookmarks_init(void);
static void bookmark_add(const char *title, const char *url, const char *type,
			 const char *icon, const char *id);
static void bookmarks_save(void);

static struct bookmark_list bookmarks;




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
  prop_sub_t *bm_del_req_sub;
  rstr_t *bm_id;
  rstr_t *bm_title;
  rstr_t *bm_url;
  rstr_t *bm_type;
  rstr_t *bm_icon;

  service_t *bm_service;

  setting_t *bm_type_setting;
  setting_t *bm_delete;

  prop_t *bm_info;

  struct bookmark_query_list bm_queries;

} bookmark_t;



/**
 *
 */
typedef struct bookmark_query {
  LIST_ENTRY(bookmark_query) bq_global_link;

  LIST_ENTRY(bookmark_query) bq_bookmark_link;
  bookmark_t *bq_bm;

  rstr_t *bq_key;

  prop_t *bq_link;

  prop_sub_t *bq_sub_key;


} bookmark_query_t;

#endif


/**
 *
 */
typedef struct nav_page {
  struct navigator *np_nav;

  TAILQ_ENTRY(nav_page) np_global_link;
  TAILQ_ENTRY(nav_page) np_history_link;

  prop_t *np_prop_root;
  char *np_url;
  char *np_parent_url;
  char *np_how;

  int np_direct_close;

  prop_sub_t *np_close_sub;
  prop_sub_t *np_eventsink_sub;

  prop_sub_t *np_direct_close_sub;

  prop_t *np_item_model_src;
  prop_t *np_item_model_dst;

  prop_t *np_parent_model_src;
  prop_t *np_parent_model_dst;

#if ENABLE_BOOKMARKS
  prop_sub_t *np_bookmarked_sub;
  prop_t *np_bookmarked;

  prop_sub_t *np_title_sub;
  rstr_t *np_title;

  prop_sub_t *np_icon_sub;
  rstr_t *np_icon;

  prop_t *np_bookmark_notify_prop;
#endif

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
		      prop_t *item_model, prop_t *parent_model, const char *how,
                      const char *parent_url);


static void nav_reload_page(nav_page_t *np);

static void page_redirect(nav_page_t *np, const char *url);

#if ENABLE_BOOKMARKS
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

#endif




/**
 *
 */
static navigator_t *
nav_create(void)
{
  hts_mutex_lock(&nav_mutex);
  navigator_t *nav = calloc(1, sizeof(navigator_t));

  LIST_INSERT_HEAD(&navigators, nav, nav_link);

  TAILQ_INIT(&nav->nav_pages);
  TAILQ_INIT(&nav->nav_history);

  nav->nav_prop_root = prop_create(all_navigators, NULL);

  nav->nav_prop_pages       = prop_create(nav->nav_prop_root, "pages");
  nav->nav_prop_curpage     = prop_create(nav->nav_prop_root, "currentpage");
  nav->nav_prop_can_go_back = prop_create(nav->nav_prop_root, "canGoBack");
  nav->nav_prop_can_go_fwd  = prop_create(nav->nav_prop_root, "canGoForward");
  nav->nav_prop_can_go_home = prop_create(nav->nav_prop_root, "canGoHome");
  prop_t *eventsink         = prop_create(nav->nav_prop_root, "eventSink");
  prop_set_int(nav->nav_prop_can_go_home, 1);

  nav->nav_eventsink =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_EVENT, nav_eventsink, nav,
		   PROP_TAG_MUTEX, &nav_mutex,
		   PROP_TAG_ROOT, eventsink,
		   NULL);

  nav->nav_dtor_tracker =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, nav_dtor_tracker, nav,
		   PROP_TAG_MUTEX, &nav_mutex,
		   PROP_TAG_ROOT, nav->nav_prop_root,
		   NULL);

  nav_open0(nav, NAV_HOME, NULL, NULL, NULL, NULL, NULL);

  hts_mutex_unlock(&nav_mutex);

  static atomic_t initial_opened;

  if(atomic_add_and_fetch(&initial_opened, 1) == 1 &&
     gconf.initial_url != NULL) {

    hts_mutex_lock(&gconf.state_mutex);
    while(gconf.navigator_can_start == 0)
      hts_cond_wait(&gconf.state_cond, &gconf.state_mutex);
    hts_mutex_unlock(&gconf.state_mutex);

    event_t *e = event_create_openurl(
                                      .url  = gconf.initial_url,
                                      .view = gconf.initial_view,
                                      );
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
  prop_t *p = nav_create()->nav_prop_root;
  prop_select(p);
  return p;
}


/**
 *
 */
void
nav_init(void)
{
#if ENABLE_BOOKMARKS
  bookmarks_init();
#endif
  prop_t *navs = prop_create(prop_get_global(), "navigators");

  all_navigators = prop_create(navs, "nodes");
  prop_linkselected_create(all_navigators, navs, "current", NULL);
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
page_unsub(nav_page_t *np)
{
  prop_unsubscribe(np->np_close_sub);
  prop_unsubscribe(np->np_direct_close_sub);
  prop_unsubscribe(np->np_eventsink_sub);
#if ENABLE_BOOKMARKS
  prop_unsubscribe(np->np_bookmarked_sub);
  prop_unsubscribe(np->np_title_sub);
  prop_unsubscribe(np->np_icon_sub);
#endif
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

  TAILQ_REMOVE(&nav->nav_history, np, np_history_link);
  TAILQ_REMOVE(&nav->nav_pages, np, np_global_link);

  prop_unlink(np->np_item_model_dst);
  prop_unlink(np->np_parent_model_dst);

  if(with_prop) {
    prop_destroy(np->np_prop_root);
    nav_update_cango(nav);
  }

  prop_ref_dec(np->np_item_model_src);
  prop_ref_dec(np->np_item_model_dst);

  prop_ref_dec(np->np_parent_model_src);
  prop_ref_dec(np->np_parent_model_dst);

#if ENABLE_BOOKMARKS
  prop_ref_dec(np->np_bookmark_notify_prop);
  rstr_release(np->np_title);
  rstr_release(np->np_icon);
#endif
  free(np->np_url);
  free(np->np_parent_url);
  free(np->np_how);
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
void
nav_fini(void)
{
  hts_mutex_lock(&nav_mutex);
  navigator_t *nav;

  LIST_FOREACH(nav, &navigators, nav_link) {
    nav_close_all(nav, 1);
  }
  hts_mutex_unlock(&nav_mutex);
}


/**
 *
 */
static void
nav_select(navigator_t *nav, nav_page_t *np, prop_t *item_model)
{
  prop_link(np->np_prop_root, nav->nav_prop_curpage);
  prop_select_ex(np->np_prop_root, item_model, NULL);
  nav->nav_page_current = np;
  nav_update_cango(nav);
}


/**
 *
 */
static void
nav_insert_page(navigator_t *nav, nav_page_t *np, prop_t *item_model)
{
  nav_page_t *np2;

  if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  if(nav->nav_page_current != NULL) {

    /* Destroy any previous "future" histories,
     * this happens if we back a few times and then jumps away
     * in another "direction"
     */

    while((np2 = TAILQ_NEXT(nav->nav_page_current, np_history_link)) != NULL)
      nav_close(np2, 1);
  }

  TAILQ_INSERT_TAIL(&nav->nav_history, np, np_history_link);

  nav_select(nav, np, item_model);

  /*
   * Kill off pages that's just previous to this page in history
   * with the same URI. This makes sure the user don't end up with
   * tons of duplicates if holding down some key
   */

  while((np2 = TAILQ_PREV(np, nav_page_queue, np_history_link)) != NULL &&
        !strcmp(np2->np_url, np->np_url)) {
    nav_close(np2, 1);
  }

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
  nav_page_t *scan;
  nav_page_t *np = opaque;
  navigator_t *nav = np->np_nav;

  np->np_direct_close = v;

  if(!v)
    return;

  // If this page is "to the right" in the future stack, close it

  scan = nav->nav_page_current;
  if(scan == NULL)
    return;

  scan = TAILQ_NEXT(scan, np_history_link);

  for(; scan != NULL; scan = TAILQ_NEXT(scan, np_history_link)) {
    if(scan == np) {
      nav_close(np, 1);
      return;
    }
  }
}

#if ENABLE_BOOKMARKS

/**
 *
 */
static void
nav_page_bookmarked_set(void *opaque, int v)
{
  nav_page_t *np = opaque;
  prop_t *p = NULL;
  if(v) {
    if(nav_page_is_bookmarked(np))
      return;

    const char *title = rstr_get(np->np_title) ?: "<no title>";

    p = notify_add(NULL, NOTIFY_INFO, NULL, -3,
                   _("Added %s to home page"), title);

    bookmark_add(title, np->np_url, "other", rstr_get(np->np_icon), NULL);

  } else {
    bookmark_t *bm;
    LIST_FOREACH(bm, &bookmarks, bm_link) {
      if(!strcmp(rstr_get(bm->bm_url), np->np_url)) {
        prop_ref_dec(p);
	p = notify_add(NULL, NOTIFY_INFO, NULL, -3,
                       _("Removed %s from homepage"), rstr_get(bm->bm_title));
	prop_destroy(bm->bm_root);
      }
    }
  }
  bookmarks_save();

  prop_destroy(np->np_bookmark_notify_prop);
  prop_ref_dec(np->np_bookmark_notify_prop);
  np->np_bookmark_notify_prop = p;
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
nav_page_icon_set(void *opaque, rstr_t *str)
{
  nav_page_t *np = opaque;
  rstr_set(&np->np_icon, str);
}

#endif


/**
 *
 */
static void
page_eventsink(void *opaque, event_t *e)
{
  nav_page_t *np = opaque;
  if(event_is_action(e, ACTION_RELOAD_DATA)) {
    nav_reload_page(np);
  } else if(event_is_type(e, EVENT_REDIRECT)) {
    const event_payload_t *ep = (const event_payload_t *)e;
    page_redirect(np, ep->payload);
  }
}



/**
 *
 */
static void
nav_page_setup_prop(nav_page_t *np, const char *view)
{
  np->np_prop_root = prop_create_root(NULL);

  kv_prop_bind_create(prop_create(np->np_prop_root, "persistent"),
		      np->np_url);

  prop_t *prev = prop_create(np->np_prop_root, "previous");
  
  if(np->np_parent_model_src) {
    np->np_parent_model_dst = prop_create_r(prev, "parentModel");
    prop_link(np->np_parent_model_src, np->np_parent_model_dst);
  }

  if(np->np_item_model_src) {
    np->np_item_model_dst = prop_create_r(prev, "itemModel");
    prop_link(np->np_item_model_src, np->np_item_model_dst);
  }

  if(view != NULL)
    prop_set(np->np_prop_root, "requestedView", PROP_SET_STRING, view);

  prop_set(np->np_prop_root, "how", PROP_SET_STRING, np->np_how);

  // XXX Change this into event-style subscription
  np->np_close_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAMED_ROOT, np->np_prop_root, "page",
		   PROP_TAG_NAME("page", "close"),
		   PROP_TAG_CALLBACK_INT, nav_page_close_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);

  prop_set(np->np_prop_root, "url",       PROP_SET_STRING, np->np_url);
  prop_set(np->np_prop_root, "parentUrl", PROP_SET_STRING, np->np_parent_url);

  np->np_direct_close_sub = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_NAMED_ROOT, np->np_prop_root, "page",
		   PROP_TAG_NAME("page", "directClose"),
		   PROP_TAG_CALLBACK_INT, nav_page_direct_close_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);

  np->np_eventsink_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAMED_ROOT, np->np_prop_root, "page",
		   PROP_TAG_NAME("page", "eventSink"),
		   PROP_TAG_CALLBACK_EVENT, page_eventsink, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);


#if ENABLE_BOOKMARKS
  np->np_bookmarked = prop_create(np->np_prop_root, "bookmarked");

  prop_set_int(np->np_bookmarked, nav_page_is_bookmarked(np));

  np->np_bookmarked_sub = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
		   PROP_TAG_ROOT, np->np_bookmarked,
		   PROP_TAG_CALLBACK_INT, nav_page_bookmarked_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);

  np->np_title_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "model", "metadata", "title"),
		   PROP_TAG_NAMED_ROOT, np->np_prop_root, "page",
		   PROP_TAG_CALLBACK_RSTR, nav_page_title_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);

  np->np_icon_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "model", "metadata", "logo"),
		   PROP_TAG_NAMED_ROOT, np->np_prop_root, "page",
		   PROP_TAG_CALLBACK_RSTR, nav_page_icon_set, np,
		   PROP_TAG_MUTEX, &nav_mutex,
		   NULL);
#endif
}



typedef struct nav_open_backend_aux {
  prop_t *p;
  char *url;
} nav_open_backend_aux_t;

/**
 *
 */
static void *
nav_open_thread(void *aux)
{
  nav_open_backend_aux_t *noba = aux;

  if(backend_open(noba->p, noba->url, 0))
    nav_open_errorf(noba->p, _("No handler for URL"));

  free(noba->url);
  prop_ref_dec(noba->p);
  free(noba);
  return NULL;
}

/**
 *
 */
static void
nav_open_backend(nav_page_t *np)
{
  nav_open_backend_aux_t *noba = malloc(sizeof(nav_open_backend_aux_t));
  noba->p = prop_ref_inc(np->np_prop_root);
  noba->url = strdup(np->np_url);

  hts_thread_create_detached("navopen", nav_open_thread, noba,
			     THREAD_PRIO_MODEL);
}

/**
 *
 */
static void
nav_open0(navigator_t *nav, const char *url, const char *view,
          prop_t *item_model, prop_t *parent_model,
          const char *how, const char *parent_url)
{
  nav_page_t *np = calloc(1, sizeof(nav_page_t));

  TRACE(TRACE_INFO, "navigator", "Opening %s", url);
  np->np_nav = nav;
  np->np_url = strdup(url);
  np->np_parent_url = parent_url ? strdup(parent_url) : NULL;
  np->np_item_model_src   = prop_ref_inc(item_model);
  np->np_parent_model_src = prop_ref_inc(parent_model);
  np->np_direct_close = 0;
  np->np_how = how ? strdup(how) : NULL;
  TAILQ_INSERT_TAIL(&nav->nav_pages, np, np_global_link);

  nav_page_setup_prop(np, view);

  nav_insert_page(nav, np, item_model);
  nav_open_backend(np);

}


/**
 *
 */
void
nav_open(const char *url, const char *view)
{
  event_dispatch(event_create_openurl(.url  = url,
                                      .view = view));
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

    const int doclose = np->np_direct_close || gconf.enable_nav_always_close;

    nav_select(nav, prev, NULL);

    if(doclose)
      nav_close(np, 1);
  } else {
    event_t *e = event_create_action(ACTION_SYSTEM_HOME);
    prop_t *eventsink = prop_create_r(nav->nav_prop_root, "eventSink");
    prop_send_ext_event(eventsink, e);
    prop_ref_dec(eventsink);
    event_release(e);
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
nav_reload_page(nav_page_t *np)
{
  navigator_t *nav = np->np_nav;
  TRACE(TRACE_INFO, "navigator", "Reloading %s", np->np_url);

  page_unsub(np);

  prop_destroy(np->np_prop_root);
  mystrset(&np->np_how, "continue");
  nav_page_setup_prop(np, NULL);

  if(prop_set_parent(np->np_prop_root, nav->nav_prop_pages)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  nav_select(nav, np, NULL);
  nav_open_backend(np);
  printf("Done\n");
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

#if ENABLE_PLUGINS
  plugins_reload_dev_plugin();
#endif
  nav_reload_page(np);
}


/**
 *
 */
static void
page_redirect(nav_page_t *np, const char *url)
{
  navigator_t *nav = np->np_nav;

  TRACE(TRACE_DEBUG, "navigator", "Following redirect to %s", url);

  if(nav->nav_page_current == np)
    prop_unlink(nav->nav_prop_curpage);

  prop_t *p = np->np_prop_root;

  page_unsub(np);
  prop_destroy_childs(np->np_prop_root);
  mystrset(&np->np_url, url);

  nav_page_setup_prop(np, NULL);

  if(prop_set_parent_ex(np->np_prop_root, nav->nav_prop_pages,
                        p, NULL)) {
    /* nav->nav_prop_pages is a zombie, this is an error */
    abort();
  }

  if(nav->nav_page_current == np)
    nav_select(nav, np, NULL);
  prop_destroy(p);

  nav_open_backend(np);
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
    nav_open0(nav, NAV_HOME, NULL, NULL, NULL, NULL, NULL);

  } else if(event_is_action(e, ACTION_PLAYQUEUE)) {
    nav_open0(nav, "playqueue:", NULL, NULL, NULL, NULL, NULL);

  } else if(event_is_action(e, ACTION_RELOAD_DATA)) {
    nav_reload_current(nav);

  } else if(event_is_type(e, EVENT_OPENURL)) {
    ou = (event_openurl_t *)e;
    if(ou->url != NULL)
      nav_open0(nav, ou->url, ou->view,
                ou->item_model, ou->parent_model,
                ou->how, ou->parent_url);
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
void
nav_redirect(prop_t *root, const char *url)
{
  event_t *e = event_create_str(EVENT_REDIRECT, url);
  prop_t *p = prop_create_r(root, "eventSink");
  prop_send_ext_event(p, e);
  prop_ref_dec(p);
  event_release(e);
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


#if ENABLE_BOOKMARKS

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
    htsmsg_add_msg(m, NULL, b);
  }

  htsmsg_store_save(m, "bookmarks2");
  htsmsg_release(m);
}

/**
 *
 */
static void
bookmark_unlink_queries(bookmark_t *bm)
{
  bookmark_query_t *bq;

  while((bq = LIST_FIRST(&bm->bm_queries)) != NULL) {
    LIST_REMOVE(bq, bq_bookmark_link);
    bq->bq_bm = NULL;
    prop_unlink(bq->bq_link);
  }
}


/**
 *
 */
static void
bookmark_link_queries(bookmark_t *bm)
{
  bookmark_query_t *bq;
  LIST_FOREACH(bq, &bookmark_queries, bq_global_link) {
    if(rstr_eq(bm->bm_url, bq->bq_key) && bq->bq_bm == NULL) {
      bq->bq_bm = bm;
      LIST_INSERT_HEAD(&bm->bm_queries, bq, bq_bookmark_link);
      prop_link(bm->bm_root, bq->bq_link);
    }
  }
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

  bookmark_unlink_queries(bm);

  s = va_arg(ap, prop_sub_t *);

  prop_unsubscribe(bm->bm_title_sub);
  prop_unsubscribe(bm->bm_url_sub);
  prop_unsubscribe(bm->bm_type_sub);
  prop_unsubscribe(bm->bm_icon_sub);
  prop_unsubscribe(bm->bm_del_req_sub);

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
bookmark_delete_request(void *opaque, prop_event_t event)
{
  bookmark_t *bm = opaque;

  if(event != PROP_REQ_DELETE)
    return;
  prop_destroy(bm->bm_root);
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
  bookmarks_save();
}


/**
 *
 */
static void
bm_set_url(void *opaque, rstr_t *str)
{
  bookmark_t *bm = opaque;

  bookmark_unlink_queries(bm);
  rstr_set(&bm->bm_url, str);
  service_set_url(bm->bm_service, str);
  bookmarks_save();
  nav_update_bookmarked();
  bookmark_link_queries(bm);
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
change_type(void *opaque, const char *string)
{
  bookmark_t *bm = opaque;
  prop_setv(bm->bm_root, "metadata", "svctype", NULL, PROP_SET_STRING, string);
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
                        PROP_TAG_MUTEX, &nav_mutex,
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
	     const char *icon, const char *id)
{
  if(title == NULL || url == NULL)
    return;

  bookmark_t *bm = calloc(1, sizeof(bookmark_t));

  LIST_INSERT_HEAD(&bookmarks, bm, bm_link);

  prop_t *p = prop_create_root(NULL);
  bm->bm_root = prop_ref_inc(p);

  prop_set(p, "type", PROP_SET_STRING, "settings");

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
                 PROP_TAG_MUTEX, &nav_mutex,
		 NULL);

  prop_set(bm->bm_service->s_root, "deleteText",
           PROP_SET_LINK, _p("Remove bookmark"));

  bm->bm_del_req_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_CALLBACK, bookmark_delete_request, bm,
                   PROP_TAG_ROOT, bm->bm_service->s_root,
                   PROP_TAG_MUTEX, &nav_mutex,
                   NULL);

  // Construct the settings page


  prop_t *m = prop_create(p, "model");

  prop_set(p, "url", PROP_ADOPT_RSTRING, backend_prop_make(m, NULL));

  prop_t *md = prop_create(p, "metadata");

  prop_set(m, "type", PROP_SET_STRING, "settings");
  prop_set(m, "subtype", PROP_SET_STRING, "bookmark");

  prop_link(prop_create(md, "title"),
	    prop_create(prop_create(m, "metadata"), "title"));

  settings_create_bound_string(m, _p("Title"), prop_create(md, "title"));
  settings_create_bound_string(m, _p("URL"), prop_create(md, "url"));
  settings_create_bound_string(m, _p("Icon"), prop_create(md, "icon"));

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
                   SETTING_MUTEX(&nav_mutex),
                   NULL);

  prop_link(prop_create(md, "url"), prop_create(md, "shortdesc"));

  bm->bm_info = prop_create_root(NULL);
  settings_create_info(m, NULL,
		       service_get_statustxt_prop(bm->bm_service));

  bm->bm_delete =
    setting_create(SETTING_ACTION, m, 0,
                   SETTING_TITLE(_p("Delete")),
                   SETTING_CALLBACK(bm_delete, bm),
                   SETTING_MUTEX(&nav_mutex),
                   NULL);


  if(prop_set_parent(p, bookmark_nodes))
    abort();
  nav_update_bookmarked();
  bookmark_link_queries(bm);
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
    bookmark_add("New bookmark", "none:", "other", NULL, NULL);
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
	       htsmsg_get_str(o, "id"));
}


static void
bookmark_query_set_key(void *aux, rstr_t *str)
{
  bookmark_query_t *bq = aux;
  bookmark_t *bm;
  rstr_set(&bq->bq_key, str);

  if(bq->bq_bm != NULL)
    LIST_REMOVE(bq, bq_bookmark_link);

  LIST_FOREACH(bm, &bookmarks, bm_link) {
    if(rstr_eq(bm->bm_url, bq->bq_key))
      break;
  }

  bq->bq_bm = bm;
  if(bm != NULL) {
    LIST_INSERT_HEAD(&bm->bm_queries, bq, bq_bookmark_link);
    prop_link(bm->bm_root, bq->bq_link);
  } else {
    prop_unlink(bq->bq_link);
  }
}


/**
 *
 */
static void
bookmark_query_add(prop_t *p)
{
  bookmark_query_t *bq = calloc(1, sizeof(bookmark_query_t));

  bq->bq_link = prop_create_r(p, "value");
  LIST_INSERT_HEAD(&bookmark_queries, bq, bq_global_link);

  prop_tag_set(p, &bookmark_queries, bq);

  bq->bq_sub_key =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_RSTR, bookmark_query_set_key, bq,
                   PROP_TAG_MUTEX, &nav_mutex,
                   PROP_TAG_NAMED_ROOT, p, "node",
                   PROP_TAG_NAME("node", "key"),
                   NULL);
}


/**
 *
 */
static void
bookmark_query_del(prop_t *p)
{
  bookmark_query_t *bq = prop_tag_clear(p, &bookmark_queries);
  prop_ref_dec(bq->bq_link);
  if(bq->bq_bm != NULL)
    LIST_REMOVE(bq, bq_bookmark_link);
  LIST_REMOVE(bq, bq_global_link);
  rstr_release(bq->bq_key);
  prop_unsubscribe(bq->bq_sub_key);
  free(bq);
}


/**
 *
 */
static void
bookmark_queries_callback(void *opaque, prop_event_t event, ...)
{
  va_list ap;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    bookmark_query_add(va_arg(ap, prop_t *));
    break;

  case PROP_DEL_CHILD:
    bookmark_query_del(va_arg(ap, prop_t *));
    break;
  }
}


/**
 *
 */
static void
bookmark_eventsink(void *opaque, event_t *e)
{
  if(event_is_type(e, EVENT_PROPREF)) {
    event_prop_t *ep = (event_prop_t *)e;
    rstr_t *url   = prop_get_string(ep->p, "url", NULL);
    rstr_t *title = prop_get_string(ep->p, "metadata", "title", NULL);
    rstr_t *icon  = prop_get_string(ep->p, "metadata", "icon", NULL);
    bookmark_t *bm;
    LIST_FOREACH(bm, &bookmarks, bm_link) {
      if(rstr_eq(bm->bm_url, url)) {
	notify_add(NULL, NOTIFY_INFO, NULL, 3, _("Removed bookmark: %s"),
		   rstr_get(bm->bm_title));
	prop_destroy(bm->bm_root);
        break;
      }
    }

    if(bm == NULL) {
      bookmark_add(rstr_get(title), rstr_get(url), "other",
                   rstr_get(icon), rstr_get(url));
      bookmarks_save();
      notify_add(NULL, NOTIFY_INFO, NULL, 3, _("Added new bookmark: %s"),
                 rstr_get(title));
    }

    rstr_release(url);
    rstr_release(title);
    rstr_release(icon);

  }
}

/**
 *
 */
static void
bookmarks_init(void)
{
  htsmsg_field_t *f;
  htsmsg_t *m;

  prop_t *root = settings_add_dir(NULL, _p("Bookmarks"),
                                  "bookmark", NULL,
                                  _p("Add and remove items on homepage"),
                                  "settings:bookmarks");

  bookmark_nodes = prop_create(root, "nodes");
  prop_set(root, "mayadd", PROP_SET_INT, 1);

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, bookmarks_callback, NULL,
		 PROP_TAG_ROOT, bookmark_nodes,
                 PROP_TAG_MUTEX, &nav_mutex,
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
    htsmsg_release(m);
    bookmarks_save();
    htsmsg_store_remove("bookmarks");
  } else if((m = htsmsg_store_load("bookmarks2")) != NULL) {
    HTSMSG_FOREACH(f, m) {
      htsmsg_t *o;
      if((o = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      bookmark_load(o);
    }
    htsmsg_release(m);
  }


  prop_subscribe(0,
                 PROP_TAG_CALLBACK, bookmark_queries_callback, NULL,
                 PROP_TAG_NAME("global", "bookmarks", "queries"),
                 PROP_TAG_MUTEX, &nav_mutex,
		 NULL);


  prop_subscribe(0,
                 PROP_TAG_CALLBACK_EVENT, bookmark_eventsink, NULL,
                 PROP_TAG_NAME("global", "bookmarks", "eventSink"),
                 PROP_TAG_MUTEX, &nav_mutex,
                 NULL);

}
#endif
