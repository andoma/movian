/*
 *  Settings framework
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "showtime.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"
#include "backend/backend.h"
#include "assert.h"

#define SETTINGS_URL "settings:"

static prop_t *settings_root;


/**
 *
 */
struct setting {
  void *s_opaque;
  void *s_callback;
  prop_sub_t *s_sub;
  prop_t *s_prop;
  prop_t *s_val;
  int s_min;
  int s_max;

  settings_saver_t *s_saver;
  void *s_saver_opaque;
  htsmsg_t *s_store;

  char *s_id;

};


/**
 *
 */
static void
settings_set_url(prop_t *p)
{
  char url[URL_MAX];
  prop_t **a;
  int i;
  const char *slash = "";

  a = prop_get_ancestors(p);
  i = 0;
  while(a[i] != NULL)
    i++;

  assert(i > 3);
  i-=4;

  snprintf(url, sizeof(url), SETTINGS_URL);

  while(i >= 0) {
    
    snprintf(url + strlen(url), sizeof(url) - strlen(url), 
	     "%s%s", slash, a[i]->hp_name);
    slash="/";
    i-=3;
  }

  prop_ancestors_unref(a);
  prop_set_string(prop_create(p, "url"), url);
}


/**
 *
 */
static prop_t *
settings_add(prop_t *parent, 
	     const char *id, const char *title, const char *type)
{
  prop_t *p, *src;

  p = prop_create(prop_create(parent ?: settings_root, "nodes"), id);
  settings_set_url(p);

  src = prop_create(p, "source");

  prop_set_string(prop_create(src, "id"), id);
  prop_set_string(prop_create(src, "title"), title);
  prop_set_string(prop_create(src, "type"), type);
  return src;
}


/**
 *
 */
prop_t *
settings_add_dir(prop_t *parent, const char *id, const char *title,
		 const char *subtype)
{
  prop_t *r = settings_add(parent, id, title, "settings");
  prop_set_string(prop_create(r, "subtype"), subtype);
  return r;
}


/**
 *
 */
static void
settings_int_callback(void *opaque, int v)
{
  setting_t *s = opaque;
  prop_callback_int_t *cb = s->s_callback;

  if(cb) cb(s->s_opaque, v);

  if(s->s_store && s->s_saver) {
    htsmsg_delete_field(s->s_store, s->s_id);
    htsmsg_add_s32(s->s_store, s->s_id, v);
    s->s_saver(s->s_saver_opaque, s->s_store);
  }
}

/**
 *
 */
setting_t *
settings_create_bool(prop_t *parent, const char *id, const char *title,
		     int initial, htsmsg_t *store,
		     prop_callback_int_t *cb, void *opaque,
		     int flags, prop_courier_t *pc,
		     settings_saver_t *saver, void *saver_opaque)
{
  prop_t *r = settings_add(parent, id, title, "bool");
  prop_t *v = prop_create(r, "value");
  setting_t *s = calloc(1, sizeof(setting_t));
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_u32_or_default(store, id, initial);

  prop_set_int(v, !!initial);

  s->s_prop = r;
  
  sub = prop_subscribe(flags & SETTINGS_INITIAL_UPDATE ?
		       0 : PROP_SUB_NO_INITIAL_UPDATE,
		       PROP_TAG_CALLBACK_INT, settings_int_callback, s,
		       PROP_TAG_ROOT, v,
		       PROP_TAG_COURIER, pc,
		       NULL);
  s->s_sub = sub;
  s->s_store = store;
  s->s_saver = saver;
  s->s_saver_opaque = saver_opaque;
  return s;
}


/**
 *
 */
void
settings_set_bool(setting_t *s, int v)
{
  prop_set_int(prop_create(s->s_prop, "value"), v);
}


/**
 *
 */
void
settings_toggle_bool(setting_t *s)
{
  prop_toggle_int(prop_create(s->s_prop, "value"));
}


/**
 *
 */
setting_t *
settings_create_int(prop_t *parent, const char *id, const char *title,
		    int initial, htsmsg_t *store,
		    int min, int max, int step,
		    prop_callback_int_t *cb, void *opaque,
		    int flags, const char *unit,
		    prop_courier_t *pc,
		    settings_saver_t *saver, void *saver_opaque)
{
  prop_t *r = settings_add(parent, id, title, "integer");
  prop_t *v = prop_create(r, "value");
  setting_t *s = calloc(1, sizeof(setting_t));
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;


  if(store != NULL)
    initial = htsmsg_get_s32_or_default(store, id, initial);

  prop_set_int_clipping_range(v, min, max);

  prop_set_int(prop_create(r, "min"), min);
  prop_set_int(prop_create(r, "max"), max);
  prop_set_int(prop_create(r, "step"), step);
  prop_set_string(prop_create(r, "unit"), unit);

  prop_set_int(v, initial);

  s->s_prop = r;
  s->s_val = v;
  s->s_min = min;
  s->s_max = max;
  sub = prop_subscribe(flags & SETTINGS_INITIAL_UPDATE ?
		       0 : PROP_SUB_NO_INITIAL_UPDATE,
		       PROP_TAG_CALLBACK_INT, settings_int_callback, s,
		       PROP_TAG_ROOT, v,
		       PROP_TAG_COURIER, pc,
		       NULL);
  s->s_sub = sub;
  s->s_store = store;
  s->s_saver = saver;
  s->s_saver_opaque = saver_opaque;
  return s;
}


/**
 *
 */
void
settings_set_int(setting_t *s, int v)
{
  prop_set_int(s->s_val, v);
}


/**
 *
 */
void 
settings_add_int(setting_t *s, int delta)
{
  prop_add_int(s->s_val, delta);
}


/**
 *
 */
static void 
callback_opt(void *opaque, prop_event_t event, ...)
{
  setting_t *s = opaque;
  prop_callback_string_t *cb;
  prop_t *c;

  va_list ap;
  va_start(ap, event);

  cb = s->s_callback;

  if(event != PROP_SELECT_CHILD)
    return;

  c = va_arg(ap, prop_t *);

  if(cb) cb(s->s_opaque, c ? c->hp_name : NULL);

  if(s->s_store && s->s_saver) {
    htsmsg_delete_field(s->s_store, s->s_id);
    if(c != NULL)
      htsmsg_add_str(s->s_store, s->s_id, c->hp_name);
    s->s_saver(s->s_saver_opaque, s->s_store);
  }
}


/**
 *
 */
setting_t *
settings_create_multiopt(prop_t *parent, const char *id, const char *title,
			 prop_callback_string_t *cb, void *opaque)
{
  prop_t *r = settings_add(parent, id, title, "multiopt");
  prop_t *o = prop_create(r, "options");
  setting_t *s = malloc(sizeof(setting_t));
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;
  s->s_prop = r;
  
  sub = prop_subscribe(0,
		       PROP_TAG_CALLBACK, callback_opt, s, 
		       PROP_TAG_ROOT, o, 
		       NULL);

  s->s_sub = sub;
  return s;
}



/**
 *
 */
void
settings_multiopt_add_opt(setting_t *parent, const char *id, const char *title,
			  int selected)
{
  prop_t *r = parent->s_prop;
  prop_t *opts = prop_create_ex(r, "options", parent->s_sub, 0);
  prop_t *o = prop_create(opts, id);

  prop_set_string(prop_create(o, "title"), title);


  if(selected)
    prop_select_ex(o, 0, parent->s_sub);
}


/**
 *
 */
void
settings_multiopt_initiate(setting_t *s, htsmsg_t *store,
			   settings_saver_t *saver, void *saver_opaque)
{
  const char *str = htsmsg_get_str(store, s->s_id);

  if(str != NULL) {
    prop_t *r = s->s_prop;
    prop_t *opts = prop_create_ex(r, "options", s->s_sub, 0);
    prop_t *o = prop_get_by_names(opts, str, NULL);

    if(o != NULL)
      prop_select_ex(o, 0, s->s_sub);
  }

  s->s_store = store;
  s->s_saver = saver;
  s->s_saver_opaque = saver_opaque;
}


/**
 *
 */
static void
settings_string_callback(void *opaque, const char *str)
{
  setting_t *s = opaque;
  prop_callback_string_t *cb = s->s_callback;

  if(cb) cb(s->s_opaque, str);

  if(s->s_store && s->s_saver) {
    htsmsg_delete_field(s->s_store, s->s_id);
    if(str != NULL)
      htsmsg_add_str(s->s_store, s->s_id, str);
    s->s_saver(s->s_saver_opaque, s->s_store);
  }
}



/**
 *
 */
setting_t *
settings_create_string(prop_t *parent, const char *id, const char *title,
		       const char *initial, htsmsg_t *store,
		       prop_callback_string_t *cb, void *opaque,
		       int flags, prop_courier_t *pc,
		       settings_saver_t *saver, void *saver_opaque)
{
  prop_t *r = settings_add(parent, id, title, "string");
  prop_t *v = prop_create(r, "value");
  setting_t *s = calloc(1, sizeof(setting_t));
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_str(store, id);

  if(initial != NULL)
    prop_set_string(v, initial);

  s->s_prop = r;
  
  if(flags & SETTINGS_PASSWORD)
    prop_set_int(prop_create(r, "password"), 1);

  sub = prop_subscribe(flags & SETTINGS_INITIAL_UPDATE ?
		       0 : PROP_SUB_NO_INITIAL_UPDATE,
		       PROP_TAG_CALLBACK_STRING, settings_string_callback, s,
		       PROP_TAG_ROOT, v,
		       PROP_TAG_COURIER, pc,
		       NULL);
  s->s_sub = sub;
  
  s->s_store = store;
  s->s_saver = saver;
  s->s_saver_opaque = saver_opaque;
  return s;
}


/**
 *
 */
void
setting_destroy(setting_t *s)
{
  free(s->s_id);
  prop_unsubscribe(s->s_sub);
  prop_destroy(s->s_prop);
  free(s);
}


/**
 *
 */
prop_t *
settings_get_value(setting_t *s)
{
  return prop_create(s->s_prop, "value");
}

/**
 *
 */
void
settings_init(void)
{
  settings_root = prop_create(prop_get_global(), "settings");
  prop_set_string(prop_create(settings_root, "type"), "settings");
  prop_set_string(prop_create(settings_root, "title"), "Global settings");
}


/**
 *
 */
static int
be_settings_canhandle(const char *url)
{
  return !strncmp(url, SETTINGS_URL, strlen(SETTINGS_URL));
}





/**
 *
 */
static int
be_settings_open(struct navigator *nav,
		 const char *url0, const char *type0, prop_t *psource,
		 nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *p = NULL;
  const char *url = url0 + strlen(SETTINGS_URL);
  char buf[100];
  int l;

  if(!*url) {
    n = nav_page_create(nav, url0, sizeof(nav_page_t), 0);
    prop_link(settings_root, prop_create(n->np_prop_root, "source"));
    *npp = n;
    return 0;
   }

  while(*url) {
    l = 0;
    while(*url && *url != '/' && l < sizeof(buf) - 2) {
      buf[l++] = *url++;
    }
    buf[l] = 0;
    if(*url == '/')
      url++;
    

    p = p ? prop_create(p, "source") : settings_root;
    p = prop_create(p, "nodes");
    p = prop_create(p, buf);
  }
  
  n = nav_page_create(nav, url0, sizeof(nav_page_t), 0);
  prop_link(prop_create(p, "source"), prop_create(n->np_prop_root, "source"));
  *npp = n;
  return 0;
}


/**
 *
 */
static backend_t be_settings = {
  .be_canhandle = be_settings_canhandle,
  .be_open = be_settings_open,
};

BE_REGISTER(settings);
