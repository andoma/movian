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
#include <assert.h>

#include "showtime.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"
#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"

#define SETTINGS_URL "settings:"

static prop_t *settings_root;
static prop_t *settings_nodes;

prop_t *settings_apps, *settings_sd;

/**
 *
 */
struct setting {
  void *s_opaque;
  void *s_callback;
  prop_sub_t *s_sub;
  prop_t *s_root;
  prop_t *s_model;
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
set_title(prop_t *model, prop_t *title)
{
  prop_link(title, prop_create(prop_create(model, "metadata"), "title"));
}


/**
 *
 */
static prop_t *
setting_add(prop_t *parent, prop_t *title, const char *type)
{
  prop_t *p, *model;
  p = prop_create(parent ? prop_create(parent, "nodes") : settings_nodes, NULL);
  model = prop_create(p, "model");
  if(title != NULL)
    set_title(model, title);
  prop_set_string(prop_create(model, "type"), type);
  return p;
}


/**
 *
 */
static prop_t *
setting_add_cstr(prop_t *parent, const char *title, const char *type)
{
  prop_t *p, *model;
  p = prop_create(parent ? prop_create(parent, "nodes") : settings_nodes, NULL);
  model = prop_create(p, "model");
  prop_set_string(prop_create(prop_create(model, "metadata"), "title"), title);
  prop_set_string(prop_create(model, "type"), type);
  return p;
}


/**
 *
 */
prop_t *
settings_add_dir(prop_t *parent, prop_t *title, const char *subtype,
		 const char *icon, prop_t *shortdesc)
{
  char url[100];
  prop_t *p = setting_add(parent ? prop_create(parent, "model") : NULL,
			  title, "settings");
  prop_t *model = prop_create(p, "model");
  prop_t *metadata = prop_create(model, "metadata");

  prop_set_string(prop_create(model, "subtype"), subtype);

  backend_prop_make(model, url, sizeof(url));
  prop_set_string(prop_create(p, "url"), url);
  if(icon != NULL)
    prop_set_string(prop_create(metadata, "icon"), icon);

  if(shortdesc != NULL)
    prop_link(shortdesc, prop_create(metadata, "shortdesc"));
  return p;
}


/**
 *
 */
prop_t *
settings_add_dir_cstr(prop_t *parent, const char *title, const char *subtype,
		 const char *icon, const char *shortdesc)
{
  char url[100];
  prop_t *p = setting_add_cstr(parent ? prop_create(parent, "model") : NULL,
			       title, "settings");
  prop_t *model = prop_create(p, "model");
  prop_t *metadata = prop_create(model, "metadata");

  backend_prop_make(model, url, sizeof(url));
  prop_set_string(prop_create(p, "url"), url);
  if(icon != NULL)
    prop_set_string(prop_create(metadata, "icon"), icon);

  prop_set_string(prop_create(metadata, "shortdesc"), shortdesc);
  return p;
}


/**
 *
 */
static setting_t *
setting_create_leaf(prop_t *parent, prop_t *title, const char *type,
		    const char *valuename)
{
  setting_t *s = calloc(1, sizeof(setting_t));
  s->s_root = setting_add(prop_create(parent, "model"), title, type);
  s->s_model = prop_create(s->s_root, "model");
  s->s_val = prop_ref_inc(prop_create(s->s_model, valuename));
  return s;
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
settings_create_bool(prop_t *parent, const char *id, prop_t *title,
		     int initial, htsmsg_t *store,
		     prop_callback_int_t *cb, void *opaque,
		     int flags, prop_courier_t *pc,
		     settings_saver_t *saver, void *saver_opaque)
{
  setting_t *s = setting_create_leaf(parent, title, "bool", "value");
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_u32_or_default(store, id, initial);

  prop_set_int(s->s_val, !!initial);

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_int_callback(s, !!initial);
  
  sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		       PROP_TAG_CALLBACK_INT, settings_int_callback, s,
		       PROP_TAG_ROOT, s->s_val,
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
  prop_set_int(s->s_val, v);
}


/**
 *
 */
void
settings_toggle_bool(setting_t *s)
{
  prop_toggle_int(s->s_val);
}


/**
 *
 */
setting_t *
settings_create_int(prop_t *parent, const char *id, prop_t *title,
		    int initial, htsmsg_t *store,
		    int min, int max, int step,
		    prop_callback_int_t *cb, void *opaque,
		    int flags, const char *unit,
		    prop_courier_t *pc,
		    settings_saver_t *saver, void *saver_opaque)
{
  setting_t *s = setting_create_leaf(parent, title, "integer", "value");
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;


  if(store != NULL)
    initial = htsmsg_get_s32_or_default(store, id, initial);

  prop_set_int_clipping_range(s->s_val, min, max);

  prop_set_int(prop_create(s->s_model, "min"), min);
  prop_set_int(prop_create(s->s_model, "max"), max);
  prop_set_int(prop_create(s->s_model, "step"), step);
  prop_set_string(prop_create(s->s_model, "unit"), unit);

  prop_set_int(s->s_val, initial);

  s->s_min = min;
  s->s_max = max;

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_int_callback(s, initial);

  sub = prop_subscribe(0,
		       PROP_TAG_CALLBACK_INT, settings_int_callback, s,
		       PROP_TAG_ROOT, s->s_val,
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

  if(cb) cb(s->s_opaque, c ? prop_get_name(c) : NULL);

  if(s->s_store && s->s_saver) {
    htsmsg_delete_field(s->s_store, s->s_id);
    if(c != NULL)
      htsmsg_add_str(s->s_store, s->s_id, prop_get_name(c));
    s->s_saver(s->s_saver_opaque, s->s_store);
  }
}


/**
 *
 */
setting_t *
settings_create_multiopt(prop_t *parent, const char *id, prop_t *title,
			 prop_callback_string_t *cb, void *opaque)
{
  setting_t *s = setting_create_leaf(parent, title, "multiopt", "options");

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;
  
  s->s_sub = prop_subscribe(0,
			    PROP_TAG_CALLBACK, callback_opt, s, 
			    PROP_TAG_ROOT, s->s_val, 
			    NULL);
  return s;
}



/**
 *
 */
void
settings_multiopt_add_opt(setting_t *s, const char *id, prop_t *title,
			  int selected)
{
  prop_t *o = prop_create(s->s_val, id);
  prop_link(title, prop_create(o, "title"));

  if(selected)
    prop_select_ex(o, NULL, s->s_sub);
}


/**
 *
 */
void
settings_multiopt_add_opt_cstr(setting_t *s, const char *id, const char *title,
			  int selected)
{
  prop_t *o = prop_create(s->s_val, id);
  prop_set_string(prop_create(o, "title"), title);

  if(selected)
    prop_select_ex(o, NULL, s->s_sub);
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
    prop_t *o = prop_find(s->s_val, str, NULL);
    if(o != NULL)
      prop_select(o);
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
settings_create_string(prop_t *parent, const char *id, prop_t *title,
		       const char *initial, htsmsg_t *store,
		       prop_callback_string_t *cb, void *opaque,
		       int flags, prop_courier_t *pc,
		       settings_saver_t *saver, void *saver_opaque)
{
  setting_t *s = setting_create_leaf(parent, title, "string", "value");

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_str(store, id) ?: initial;

  if(initial != NULL)
    prop_set_string(s->s_val, initial);

  if(flags & SETTINGS_PASSWORD)
    prop_set_int(prop_create(s->s_model, "password"), 1);

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_string_callback(s, initial);

  s->s_sub =
    prop_subscribe(0,
		   PROP_TAG_CALLBACK_STRING, settings_string_callback, s,
		   PROP_TAG_ROOT, s->s_val,
		   PROP_TAG_COURIER, pc,
		   NULL);
  
  s->s_store = store;
  s->s_saver = saver;
  s->s_saver_opaque = saver_opaque;
  return s;
}


/**
 *
 */
void
settings_create_info(prop_t *parent, const char *image,
		     prop_t *description)
{
  prop_t *r = prop_create(setting_add(prop_create(parent, "model"),
				      NULL, "info"), "model");
  prop_link(description, prop_create(r, "description"));
  if(image != NULL)
    prop_set_string(prop_create(r, "image"), image);
}


/**
 *
 */
prop_t *
settings_create_divider(prop_t *parent, prop_t *caption)
{
  return setting_add(prop_create(parent, "model"), caption, "divider");
}


/**
 *
 */
setting_t *
settings_create_action(prop_t *parent, const char *id, prop_t *title,
		       prop_callback_t *cb, void *opaque,
		       prop_courier_t *pc)
{
  setting_t *s = setting_create_leaf(parent, title, "action", "action");
  s->s_sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
			    PROP_TAG_CALLBACK, cb, opaque,
			    PROP_TAG_ROOT, s->s_val,
			    PROP_TAG_COURIER, pc,
			    NULL);
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
  prop_destroy(s->s_root);
  prop_ref_dec(s->s_val);
  free(s);
}


/**
 *
 */
prop_t *
settings_get_value(setting_t *s)
{
  return s->s_val;
}


/**
 *
 */
prop_t *
settings_get_node(setting_t *s)
{
  return s->s_root;
}

/**
 *
 */
void
settings_init(void)
{
  prop_t *n, *d, *model;
  prop_t *s1;

  settings_root = prop_create(prop_get_global(), "settings");
  prop_set_string(prop_create(settings_root, "type"), "settings");
  set_title(settings_root, _p("Global settings"));




  settings_nodes = prop_create_root(NULL);
  s1 = prop_create_root(NULL);

  prop_nf_create(s1,
		 settings_nodes, NULL, "node.model.metadata.title",
		 PROP_NF_AUTODESTROY);


  settings_apps = prop_create_root(NULL);
  settings_sd = prop_create_root(NULL);

  prop_concat_t *pc;

  pc = prop_concat_create(prop_create(settings_root, "nodes"), 0);

  prop_concat_add_source(pc, s1, NULL);

  // Applications and plugins

  n = prop_create(prop_create(settings_apps, "model"), "nodes");

  d = prop_create_root(NULL);
  model = prop_create(d, "model");
  set_title(model, _p("Applications and installed plugins"));
  prop_set_string(prop_create(model, "type"), "divider");
  prop_concat_add_source(pc, n, d);


  d = prop_create_root(NULL);
  model = prop_create(d, "model");
  set_title(model, _p("Discovered media sources"));
  prop_set_string(prop_create(model, "type"), "divider");

  n = prop_create(prop_create(settings_sd, "model"), "nodes");
  prop_concat_add_source(pc, n, d);
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
be_settings_open(prop_t *page, const char *url0)
{
  prop_link(settings_root, prop_create(page, "model"));
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

/**
 *
 */
void
settings_generic_save_settings(void *opaque, htsmsg_t *msg)
{
  htsmsg_store_save(msg, opaque);
}


/**
 *
 */
void
settings_generic_set_bool(void *opaque, int value)
{
  int *p = opaque;
  *p = value;
}
