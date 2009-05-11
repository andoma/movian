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

#include <htsmsg/htsmsg_store.h>

#include "settings.h"
#include "bookmarks.h"

static hts_mutex_t bookmark_mutex;
static prop_t *bookmarks;

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
  if(event == PROP_DESTROYED)
    bookmark_save();
}


/**
 *
 */
static void
bookmark_updated(void *opaque, prop_event_t event, ...)
{
  if(event == PROP_SET_STRING || event == PROP_SET_VOID)
    bookmark_save();
}



/**
 *
 */
static void
bookmark_add_prop(prop_t *parent, const char *name, const char *value)
{
  prop_t *p = prop_create(parent, name);
  if(value != NULL) prop_set_string(p, value); else prop_set_void(p);

  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_AUTO_UNSUBSCRIBE,
		 PROP_TAG_CALLBACK, bookmark_updated, NULL,
		 PROP_TAG_ROOT, p, 
		 PROP_TAG_MUTEX, &bookmark_mutex,
		 NULL);
}


/**
 *
 */
static void
bookmark_add(const char *title, const char *url, const char *icon,
	     int save)
{
  prop_t *p = prop_create(NULL, NULL);

  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE |
		 PROP_SUB_AUTO_UNSUBSCRIBE,

		 PROP_TAG_CALLBACK, bookmark_destroyed, NULL,
		 PROP_TAG_ROOT, p,
		 PROP_TAG_MUTEX, &bookmark_mutex,
		 NULL);

  prop_set_string(prop_create(p, "type"), "bookmark");

  bookmark_add_prop(p, "title", title);
  bookmark_add_prop(p, "url",   url);

  if(prop_set_parent(p, prop_create(bookmarks, "nodes")))
    abort();
}

/**
 *
 */
static void
bookmark_load(htsmsg_t *m)
{
  bookmark_add(htsmsg_get_str(m, "title"),
	       htsmsg_get_str(m, "url"),
	       htsmsg_get_str(m, "icon"), 0);
}


/**
 * Control function for bookmark parent. Here we create / destroy
 * entries.
 */
static void
bookmarks_callback(void *opaque, prop_event_t event, ...)
{
  prop_t *p;

  va_list ap;
  va_start(ap, event);

  p = va_arg(ap, prop_t *);

  switch(event) {
  default:
    break;

  case PROP_REQ_NEW_CHILD:
    bookmark_add("New bookmark", "none:", NULL, 1);
    break;

  case PROP_REQ_DELETE:
    prop_destroy(p);
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

  hts_mutex_init(&bookmark_mutex);

  bookmarks = settings_add_dir(NULL, "bookmarks", "Bookmarks", "bookmark");

  prop_set_int(prop_create(bookmarks, "mayadd"), 1);

  prop_subscribe(0,
		 PROP_TAG_CALLBACK, bookmarks_callback, NULL,
		 PROP_TAG_ROOT, prop_create(bookmarks, "nodes"),
		 PROP_TAG_MUTEX, &bookmark_mutex,
		 NULL);
  
  if((m = htsmsg_store_load("bookmarks")) != NULL) {

    n = htsmsg_get_map(m, "nodes");
    HTSMSG_FOREACH(f, n) {
      if((o = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      bookmark_load(o);
    }

    htsmsg_destroy(m);
  } else {

  }
}
