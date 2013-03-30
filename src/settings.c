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
#include "htsmsg/htsmsg_store.h"


#define SETTINGS_URL "settings:"
static prop_t *settings_root;
static prop_t *settings_nodes;


/**
 *
 */
struct setting {
  void *s_opaque;
  void *s_callback;
  prop_sub_t *s_sub;
  prop_t *s_root;
  prop_t *s_val;
  int s_min;
  int s_max;

  settings_saver_t *s_saver;
  void *s_saver_opaque;
  htsmsg_t *s_store;

  char *s_id;

  char *s_initial_value;
  char *s_first;
};

static void init_dev_settings(void);


/**
 *
 */
static void
set_title2(prop_t *root, prop_t *title)
{
  prop_link(title, prop_create(prop_create(root, "metadata"), "title"));
}


/**
 *
 */
static prop_t *
setting_get(prop_t *parent, int flags)
{
  prop_t *p;
  if(flags & SETTINGS_RAW_NODES)
    p = prop_create(parent, NULL);
  else
    p = prop_create(parent ? prop_create(parent, "nodes") : settings_nodes,
		    NULL);
  return p;
}

/**
 *
 */
static prop_t *
setting_add(prop_t *parent, prop_t *title, const char *type, int flags)
{
  prop_t *p = setting_get(parent, flags);
  if(title != NULL)
    set_title2(p, title);
  prop_set_string(prop_create(p, "type"), type);
  prop_set_int(prop_create(p, "enabled"), 1);
  return p;
}


/**
 *
 */
static prop_t *
setting_add_cstr(prop_t *parent, const char *title, const char *type, int flags)
{
  prop_t *p = setting_get(parent, flags);
  prop_set_string(prop_create(prop_create(p, "metadata"), "title"), title);
  prop_set_string(prop_create(p, "type"), type);
  return p;
}


/**
 *
 */
static void
settings_add_dir_sup(prop_t *root,
		     const char *url, const char *icon,
		     const char *subtype)
{
  rstr_t *url2 = backend_prop_make(root, url);
  prop_set_rstring(prop_create(root, "url"), url2);
  rstr_release(url2);

  prop_t *metadata = prop_create(root, "metadata");

  prop_set_string(prop_create(root, "subtype"), subtype);

  if(icon != NULL)
    prop_set_string(prop_create(metadata, "icon"), icon);
}



/**
 *
 */
prop_t *
settings_add_dir(prop_t *parent, prop_t *title, const char *subtype,
		 const char *icon, prop_t *shortdesc,
		 const char *url)
{
  prop_t *p = setting_add(parent, title, "settings", 0);
  prop_t *metadata = prop_create(p, "metadata");

  if(shortdesc != NULL)
    prop_link(shortdesc, prop_create(metadata, "shortdesc"));

  settings_add_dir_sup(p, url, icon, subtype);
  return p;
}


/**
 *
 */
prop_t *
settings_add_url(prop_t *parent, prop_t *title,
		 const char *subtype, const char *icon,
		 prop_t *shortdesc, const char *url)
{
  prop_t *p = setting_add(parent, title, "settings", 0);
  prop_t *metadata = prop_create(p, "metadata");

  if(shortdesc != NULL)
    prop_link(shortdesc, prop_create(metadata, "shortdesc"));

  prop_set_string(prop_create(p, "url"), url);
  prop_set_string(prop_create(p, "subtype"), subtype);

  if(icon != NULL)
    prop_set_string(prop_create(metadata, "icon"), icon);
  return p;
}


/**
 *
 */
prop_t *
settings_add_dir_cstr(prop_t *parent, const char *title, const char *subtype,
		      const char *icon, const char *shortdesc,
		      const char *url)
{
  prop_t *p = setting_add_cstr(parent, title, "settings", 0);
  prop_t *metadata = prop_create(p, "metadata");

  prop_set_string(prop_create(metadata, "shortdesc"), shortdesc);

  settings_add_dir_sup(p, url, icon, subtype);
  return p;
}


/**
 *
 */
static setting_t *
setting_create_leaf(prop_t *parent, prop_t *title, const char *type,
		    const char *valuename, int flags)
{
  setting_t *s = calloc(1, sizeof(setting_t));
  s->s_root = prop_ref_inc(setting_add(parent, title, type, flags));
  s->s_val = prop_create_r(s->s_root, valuename);
  
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
  setting_t *s = setting_create_leaf(parent, title, "bool", "value", flags);
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_u32_or_default(store, id, initial);

  prop_set_int(s->s_val, !!initial);

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_int_callback(s, !!initial);
  
  sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
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
  setting_t *s = setting_create_leaf(parent, title, "integer", "value", flags);
  prop_sub_t *sub;

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;


  if(store != NULL)
    initial = htsmsg_get_s32_or_default(store, id, initial);

  prop_set_int_clipping_range(s->s_val, min, max);

  prop_set_int(prop_create(s->s_root, "min"), min);
  prop_set_int(prop_create(s->s_root, "max"), max);
  prop_set_int(prop_create(s->s_root, "step"), step);
  prop_set_string(prop_create(s->s_root, "unit"), unit);

  prop_set_int(s->s_val, initial);

  s->s_min = min;
  s->s_max = max;

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_int_callback(s, initial);

  sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
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
  rstr_t *name;
  va_list ap;
  va_start(ap, event);

  cb = s->s_callback;

  if(event != PROP_SELECT_CHILD)
    return;

  c = va_arg(ap, prop_t *);

  name = c ? prop_get_name(c) : NULL;
  va_end(ap);

  if(cb != NULL)
    cb(s->s_opaque, rstr_get(name));

  if(s->s_store != NULL && s->s_saver != NULL) {
    htsmsg_delete_field(s->s_store, s->s_id);
    if(name != NULL)
      htsmsg_add_str(s->s_store, s->s_id, rstr_get(name));
    s->s_saver(s->s_saver_opaque, s->s_store);
  }
  rstr_release(name);
}


/**
 *
 */
setting_t *
settings_create_multiopt(prop_t *parent, const char *id, prop_t *title,
			 int flags)
{
  setting_t *s = setting_create_leaf(parent, title, "multiopt", "options", 
				     flags);
  s->s_id = strdup(id);
  return s;
}



/**
 *
 */
static prop_t *
settings_multiopt_add(setting_t *s, const char *id, prop_t *o, int selected)
{
  if(selected) {
    if(s->s_sub != NULL)
      prop_select(o);
    else
      mystrset(&s->s_initial_value, id);
  }

  if(s->s_first == NULL)
    s->s_first = strdup(id);
  return o;
}


/**
 *
 */
prop_t *
settings_multiopt_add_opt(setting_t *s, const char *id, prop_t *title,
			  int selected)
{
  prop_t *o = prop_create(s->s_val, id);
  prop_link(title, prop_create(o, "title"));
  return settings_multiopt_add(s, id, o, selected);
}


/**
 *
 */
prop_t *
settings_multiopt_add_opt_cstr(setting_t *s, const char *id, const char *title,
			  int selected)
{
  prop_t *o = prop_create(s->s_val, id);
  prop_set_string(prop_create(o, "title"), title);
  return settings_multiopt_add(s, id, o, selected);
}


/**
 *
 */
void
settings_multiopt_initiate(setting_t *s,
			   prop_callback_string_t *cb, void *opaque,
			   prop_courier_t *pc, htsmsg_t *store,
			   settings_saver_t *saver, void *saver_opaque)
{
  const char *str = store ? htsmsg_get_str(store, s->s_id) : NULL;
  prop_t *o = str ? prop_find(s->s_val, str, NULL) : NULL;

  if(o == NULL && s->s_initial_value != NULL)
    o = prop_find(s->s_val, s->s_initial_value, NULL);

  if(o == NULL && s->s_first != NULL)
    o = prop_find(s->s_val, s->s_first, NULL);

  if(o != NULL) {
    prop_select(o);
    rstr_t *name = prop_get_name(o);
    cb(opaque, rstr_get(name));
    rstr_release(name);
    prop_ref_dec(o);
  }
  

  s->s_callback = cb;
  s->s_opaque = opaque;
  
  mystrset(&s->s_initial_value, NULL);


  s->s_sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
			    PROP_TAG_CALLBACK, callback_opt, s, 
			    PROP_TAG_ROOT, s->s_val, 
			    PROP_TAG_COURIER, pc,
			    NULL);

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
  setting_t *s = setting_create_leaf(parent, title, "string", "value", flags);

  s->s_id = strdup(id);
  s->s_callback = cb;
  s->s_opaque = opaque;

  if(store != NULL)
    initial = htsmsg_get_str(store, id) ?: initial;

  if(initial != NULL)
    prop_set_string(s->s_val, initial);

  if(flags & SETTINGS_PASSWORD)
    prop_set_int(prop_create(s->s_root, "password"), 1);

  if(flags & SETTINGS_INITIAL_UPDATE)
    settings_string_callback(s, initial);

  s->s_sub =
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
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
  prop_t *r = setting_add(parent, NULL, "info", 0);
  prop_link(description, prop_create(r, "description"));
  if(image != NULL)
    prop_set_string(prop_create(r, "image"), image);
}


/**
 *
 */
prop_t *
settings_create_separator(prop_t *parent, prop_t *caption)
{
  return setting_add(parent, caption, "separator", 0);
}


/**
 *
 */
setting_t *
settings_create_action(prop_t *parent, prop_t *title,
		       prop_callback_t *cb, void *opaque,
		       int flags, prop_courier_t *pc)
{
  setting_t *s = setting_create_leaf(parent, title, "action", "action", flags);
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
prop_t *
settings_create_bound_string(prop_t *parent, prop_t *title, prop_t *value)
{
  prop_t *p = setting_add(parent, title, "string", 0);
  prop_link(value, prop_create(p, "value"));
  return p;
}


/**
 *
 */
void
setting_detach(setting_t *s)
{
  prop_unparent(s->s_root);
}

/**
 *
 */
void
setting_destroy(setting_t *s)
{
  s->s_callback = NULL;
  free(s->s_id);
  free(s->s_initial_value);
  free(s->s_first);
  prop_unsubscribe(s->s_sub);
  prop_destroy(s->s_root);
  prop_ref_dec(s->s_val);
  prop_ref_dec(s->s_root);
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
  prop_t *n, *d;
  prop_t *s1;

  settings_root = prop_create(prop_get_global(), "settings");
  prop_set_string(prop_create(settings_root, "type"), "settings");
  set_title2(settings_root, _p("Global settings"));

  settings_nodes = prop_create_root(NULL);
  s1 = prop_create_root(NULL);

  struct prop_nf *pnf;

  pnf = prop_nf_create(s1, settings_nodes, NULL, PROP_NF_AUTODESTROY);
  prop_nf_sort(pnf, "node.metadata.title", 0, 0, NULL, 1);

  gconf.settings_apps = prop_create_root(NULL);
  gconf.settings_sd = prop_create_root(NULL);

  prop_concat_t *pc;

  pc = prop_concat_create(prop_create(settings_root, "nodes"), 0);

  prop_concat_add_source(pc, s1, NULL);

  // Applications and plugins

  n = prop_create(gconf.settings_apps, "nodes");

  d = prop_create_root(NULL);
  set_title2(d, _p("Applications and installed plugins"));
  prop_set_string(prop_create(d, "type"), "separator");
  prop_concat_add_source(pc, n, d);

  d = prop_create_root(NULL);
  set_title2(d, _p("Discovered media sources"));
  prop_set_string(prop_create(d, "type"), "separator");

  n = prop_create(gconf.settings_sd, "nodes");
  prop_concat_add_source(pc, n, d);

  // General settings

  gconf.settings_general =
    settings_add_dir(NULL, _p("General"), NULL, NULL,
		     _p("System related settings"),
		     "settings:general");

  // Look and feel settings

  prop_t *lnf =
    settings_add_dir(NULL, _p("Look and feel"),
		     "display", NULL,
		     _p("Fonts and user interface styling"),
		     "settings:lookandfeel");

  gconf.settings_look_and_feel =
    prop_concat_create(prop_create(lnf, "nodes"), 0);

  // Developer settings, only available via its URI

  init_dev_settings();
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
be_settings_open(prop_t *page, const char *url0, int sync)
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
prop_t *
makesep(prop_t *title)
{
  prop_t *d = prop_create_root(NULL);
  prop_link(title, prop_create(prop_create(d, "metadata"), "title"));
  prop_set_string(prop_create(d, "type"), "separator");
  return d;

}

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
settings_generic_set_int(void *opaque, int value)
{
  int *p = opaque;
  *p = value;
}

static htsmsg_t *devstore;

/**
 *
 */
static void
add_dev_bool(const char *title, const char *id, int *val)
{
  prop_t *t = prop_create_root(NULL);
  prop_set_string(t, title);

  settings_create_bool(gconf.settings_dev, id, t, 0,
		       devstore, settings_generic_set_bool,
		       val,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings, 
		       (void *)"dev");
}


/**
 *
 */
static void
init_dev_settings(void)
{

  if((devstore = htsmsg_store_load("dev")) == NULL)
    devstore = htsmsg_create_map();

  gconf.settings_dev = settings_add_dir(prop_create_root(NULL),
				  _p("Developer settings"), NULL, NULL,
				  _p("Settings useful for developers"),
				  "settings:dev");

  prop_t *r = setting_add(gconf.settings_dev, NULL, "info", 0);
  prop_set_string(prop_create(r, "description"),
		  "Settings for developers. If you don't know what this is, don't touch it");

  add_dev_bool("Various experimental features (Use at own risk)",
	       "experimental", &gconf.enable_experimental);

  add_dev_bool("Enable binreplace",
	       "binreplace", &gconf.enable_bin_replace);

  add_dev_bool("Enable omnigrade",
	       "omnigrade", &gconf.enable_omnigrade);

  add_dev_bool("Debug all HTTP requests",
	       "httpdebug", &gconf.enable_http_debug);

  add_dev_bool("Disable HTTP connection reuse",
	       "nohttpreuse", &gconf.disable_http_reuse);

  add_dev_bool("Log AV-diff stats",
	       "detailedavdiff", &gconf.enable_detailed_avdiff);
}
