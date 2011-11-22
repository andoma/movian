/*
 *  Bookmarks
 *  Copyright (C) 2009 Andreas Öman
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

#include "htsmsg/htsmsg_store.h"

#include "showtime.h"
#include "service.h"
#include "settings.h"
#include "bookmarks.h"
#include "misc/strtab.h"

static prop_t *bookmarks;

typedef struct bookmark {
  prop_sub_t *bm_title_sub;
  prop_sub_t *bm_url_sub;
  prop_sub_t *bm_type_sub;

  service_t *bm_service;

} bookmark_t;


/**
 *
 */
static void
bookmark_save(void)
{
  htsmsg_t *m = prop_tree_to_htsmsg(prop_create(bookmarks, "nodes"));
  htsmsg_store_save(m, "bookmarks");
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

  (void)va_arg(ap, prop_t *);
  s = va_arg(ap, prop_sub_t *);

  prop_unsubscribe(bm->bm_title_sub);
  prop_unsubscribe(bm->bm_url_sub);
  prop_unsubscribe(bm->bm_type_sub);

  service_destroy(bm->bm_service);

  free(bm);

  bookmark_save();
  prop_unsubscribe(s);
}


/**
 *
 */
static void
set_title(void *opaque, const char *str)
{
  bookmark_t *bm = opaque;

  service_set_title(bm->bm_service, str);
  bookmark_save();
}


/**
 *
 */
static void
set_url(void *opaque, const char *str)
{
  bookmark_t *bm = opaque;

  service_set_url(bm->bm_service, str);
  bookmark_save();
}




/**
 *
 */
static void
set_type(void *opaque, const char *str)
{
  bookmark_t *bm = opaque;
  service_set_type(bm->bm_service, str);
  bookmark_save();
}


/**
 *
 */
static prop_sub_t *
bookmark_add_prop(prop_t *parent, const char *name, const char *value,
		  bookmark_t *bm, prop_callback_string_t *cb)
{
  prop_t *p = prop_create(parent, name);
  prop_set_string(p, value);

  return prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
			PROP_TAG_CALLBACK_STRING, cb, bm,
			PROP_TAG_ROOT, p, 
			NULL);
}


/**
 *
 */
static void
bookmark_add(const char *title, const char *url, const char *type)
{
  bookmark_t *bm = calloc(1, sizeof(bookmark_t));
  prop_t *p = prop_create_root(NULL);
  prop_t *src = prop_create(p, "model");
 
  prop_set_string(prop_create(src, "type"), "bookmark");

  bm->bm_title_sub = bookmark_add_prop(src, "title",    title,   bm, set_title);
  bm->bm_url_sub   = bookmark_add_prop(src, "url",      url,     bm, set_url);
  bm->bm_type_sub  = bookmark_add_prop(src, "svctype",  type,    bm, set_type);

  bm->bm_service = service_create(title, url, type, NULL, 1, 1);

  prop_link(service_get_status_prop(bm->bm_service),
	    prop_create(src, "status"));

  prop_link(service_get_statustxt_prop(bm->bm_service),
	    prop_create(src, "statustxt"));


  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE,
		 PROP_TAG_CALLBACK, bookmark_destroyed, bm,
		 PROP_TAG_ROOT, p,
		 NULL);
  if(prop_set_parent(p, prop_create(bookmarks, "nodes")))
    abort();

}


/**
 *
 */
static void
bookmark_load(htsmsg_t *m)
{
  if((m = htsmsg_get_map(m, "model")) == NULL)
    return;

  bookmark_add(htsmsg_get_str(m, "title"),
	       htsmsg_get_str(m, "url"),
	       htsmsg_get_str(m, "svctype"));
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
    bookmark_add("New bookmark", "none:", "other");
    break;

  case PROP_REQ_DELETE_VECTOR:
    prop_vec_destroy_entries(va_arg(ap, prop_vec_t *));
    break;
  }
}


/**
 *
 */
void
bookmarks_init(void)
{
  htsmsg_field_t *f;
  htsmsg_t *m, *n, *o;


  bookmarks = prop_create(settings_add_dir(NULL, _p("Bookmarks"),
					   "bookmark", NULL, NULL),
			  "model");

  prop_set_int(prop_create(bookmarks, "mayadd"), 1);

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, bookmarks_callback, NULL,
		 PROP_TAG_ROOT, prop_create(bookmarks, "nodes"),
		 NULL);
  
  if((m = htsmsg_store_load("bookmarks")) != NULL) {

    n = htsmsg_get_map(m, "nodes");
    HTSMSG_FOREACH(f, n) {
      if((o = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      bookmark_load(o);
    }
    htsmsg_destroy(m);
  }
}
