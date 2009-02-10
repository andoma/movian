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

#include <libhts/htsatomic.h>
#include <libhts/htssettings.h>

#include "settings.h"
#include "bookmarks.h"


static prop_t *bookmarks;

static void
bookmark_save(void)
{
  htsmsg_t *m = prop_tree_to_htsmsg(prop_create(bookmarks, "nodes"));
  hts_settings_save(m, "bookmarks");
  htsmsg_destroy(m);
}

/**
 *
 */
static void
bookmark_destroyed(struct prop_sub *sub, prop_event_t event, ...)
{
  if(event == PROP_DESTROYED)
    bookmark_save();
}


/**
 *
 */
static void
bookmark_updated(struct prop_sub *sub, prop_event_t event, ...)
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

  prop_subscribe(p, NULL, bookmark_updated, NULL, NULL,
		 PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_AUTO_UNSUBSCRIBE);
}


/**
 *
 */
static void
bookmark_add(const char *title, const char *url, const char *icon,
	     int save)
{
  prop_t *p = prop_create(NULL, NULL);

  prop_subscribe(p, NULL, bookmark_destroyed, NULL, NULL,
		 PROP_SUB_TRACK_DESTROY | PROP_SUB_NO_INITIAL_UPDATE |
		 PROP_SUB_AUTO_UNSUBSCRIBE);

  prop_set_string(prop_create(p, "type"), "bookmark");

  bookmark_add_prop(p, "title", title);
  bookmark_add_prop(p, "url",   url);

  prop_set_parent(p, prop_create(bookmarks, "nodes"));

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
 *
 */
void
bookmark_create(const char *title, const char *url, const char *icon)
{
  bookmark_add(title, url, icon, 1);
}



/**
 * Control function for bookmark parent. Here we create / destroy
 * entries.
 */
static void
bookmarks_callback(struct prop_sub *sub, prop_event_t event, ...)
{
  prop_t *p;

  va_list ap;
  va_start(ap, event);

  p = va_arg(ap, prop_t *);

  switch(event) {
  default:
    break;

  case PROP_REQ_NEW_CHILD:
    bookmark_create("New bookmark", "none:", NULL);
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

  bookmarks = settings_add_dir(NULL, "bookmarks", "Bookmarks", "bookmark");

  prop_set_int(prop_create(bookmarks, "mayadd"), 1);

  prop_subscribe(prop_create(bookmarks, "nodes"), NULL,
		 bookmarks_callback, NULL, NULL, 0);
  
  if((m = hts_settings_load("bookmarks")) != NULL) {

    n = htsmsg_get_msg(m, "nodes");
    HTSMSG_FOREACH(f, n) {
      if((o = htsmsg_get_msg_by_field(f)) == NULL)
	continue;
      bookmark_load(o);
    }

    htsmsg_destroy(m);
  }
}
