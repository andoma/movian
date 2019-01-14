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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>

#include "main.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"
#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "prop/prop_nodefilter.h"
#include "prop/prop_concat.h"
#include "prop/prop_linkselected.h"
#include "htsmsg/htsmsg_store.h"
#include "db/kvstore.h"
#include "misc/minmax.h"
#include "usage.h"

#if ENABLE_NETLOG
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define SETTINGS_URL "settings:"
static prop_t *settings_model;
static prop_t *settings_nodes;

static hts_mutex_t settings_mutex;

/**
 *
 */
struct setting {
  LIST_ENTRY(setting) s_group_link;
  void *s_opaque;
  void *s_callback;
  prop_sub_t *s_sub;
  prop_t *s_root;
  prop_t *s_val;
  prop_t *s_current_origin;

  prop_sub_t *s_inherited_value_sub;
  prop_sub_t *s_inherited_origin_sub;
  struct setting *s_parent;

  settings_saver_t *s_saver;
  void *s_saver_opaque;
  htsmsg_t *s_store;
  prop_t *s_ext_value;

  char *s_id;

  char *s_pending_value;

  char *s_store_name;
  prop_sub_t *s_sub_enabler;

  prop_t *s_current_value;

  rstr_t *s_default_str;
  int s_default_int;

  char s_origin[10];

  atomic_t s_refcount;

  int s_flags;
  char s_type;

  char s_store_type;

  char s_enable_writeback : 1;
  char s_on_group_list    : 1;
  char s_value_set        : 1;
};

#define SETTING_STORETYPE_NONE          0
#define SETTING_STORETYPE_SIMPLE        1
#define SETTING_STORETYPE_KVSTORE       2


static void init_dev_settings(void);


/**
 *
 */
static void
set_title2(prop_t *root, prop_t *title)
{
  prop_setv(root, "metadata", "title", NULL, PROP_SET_LINK, title);
}


/**
 *
 */
static prop_t *
setting_get(prop_t *parent, int flags)
{
  prop_t *p;


  if(flags & SETTINGS_FIRST) {
    parent = parent ? prop_create(parent, "nodes") : settings_nodes;
    p = prop_create_root(NULL);
    prop_t *before = prop_first_child(parent);
    if(prop_set_parent_ex(p, parent, before, NULL)) {
      prop_destroy(p);
      return NULL;
    }
    prop_ref_dec(before);
  } else if(flags & SETTINGS_RAW_NODES) {
    p = prop_create(parent, NULL);
  } else {
    p = prop_create(parent ? prop_create(parent, "nodes") : settings_nodes,
		    NULL);
  }
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
  prop_set(p, "type", PROP_SET_STRING, type);
  prop_set(p, "enabled", PROP_SET_INT, 1);
  return p;
}


/**
 *
 */
static prop_t *
setting_add_cstr(prop_t *parent, const char *title, const char *type, int flags)
{
  prop_t *p = setting_get(parent, flags);
  prop_setv(p, "metadata", "title", NULL, PROP_SET_STRING, title);
  prop_set(p, "type", PROP_SET_STRING, type);
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
  prop_set(root, "url", PROP_ADOPT_RSTRING, backend_prop_make(root, url));
  prop_set(root, "subtype", PROP_SET_STRING, subtype);

  if(icon != NULL)
    prop_setv(root, "metadata", "icon", NULL, PROP_SET_STRING, icon);
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

  if(shortdesc != NULL)
    prop_setv(p, "metadata", "shortdesc", NULL, PROP_SET_LINK, shortdesc);

  settings_add_dir_sup(p, url, icon, subtype);
  return p;
}


/**
 *
 */
void
settings_add_url(prop_t *parent, prop_t *title,
		 const char *subtype, const char *icon,
		 prop_t *shortdesc, const char *url,
		 int flags)
{
  prop_t *p = setting_add(parent, title, "settings", flags);

  if(shortdesc != NULL) {
    prop_setv(p, "metadata", "shortdesc", NULL, PROP_SET_LINK, shortdesc);
  }

  prop_set(p, "url",     PROP_SET_STRING, url);
  prop_set(p, "subtype", PROP_SET_STRING, subtype);

  if(icon != NULL)
    prop_setv(p, "metadata", "icon", NULL, PROP_SET_STRING, icon);
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

  if(shortdesc != NULL)
    prop_setv(p, "metadata", "shortdesc", NULL, PROP_SET_STRING, shortdesc);

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
  atomic_set(&s->s_refcount, 1);
  s->s_root = prop_ref_inc(setting_add(parent, title, type, flags));
  s->s_val = prop_create_r(s->s_root, valuename);
  return s;
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
void
settings_create_info(prop_t *parent, const char *image,
		     prop_t *description)
{
  prop_t *r = setting_add(parent, NULL, "info", 0);
  prop_set(r, "description", PROP_SET_LINK, description);
  if(image != NULL)
    prop_set(r, "image", PROP_SET_STRING, image);
}


/**
 *
 */
void
settings_create_separator(prop_t *parent, prop_t *caption)
{
  setting_add(parent, caption, "separator", 0);
}


/**
 *
 */
setting_t *
settings_create_action(prop_t *parent, prop_t *title, const char *subtype,
		       prop_callback_t *cb, void *opaque,
		       int flags, prop_courier_t *pc)
{
  setting_t *s = setting_create_leaf(parent, title, "action", "action", flags);
  prop_set(s->s_root, "subtype", PROP_SET_STRING, subtype);
  s->s_sub = prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
			    PROP_TAG_CALLBACK, cb, opaque,
			    PROP_TAG_NAMED_ROOT, s->s_root, "node",
                            PROP_TAG_NAME("node", "eventSink"),
			    PROP_TAG_COURIER, pc,
			    NULL);
  return s;
}



/**
 *
 */
void
settings_create_bound_string(prop_t *parent, prop_t *title, prop_t *value)
{
  prop_t *p = setting_add(parent, title, "string", 0);
  prop_set(p, "value", PROP_SET_LINK, value);
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
static void
setting_release(setting_t *s)
{
  if(atomic_dec(&s->s_refcount))
    return;

  if(s->s_parent != NULL)
    setting_release(s->s_parent);

  prop_ref_dec(s->s_val);
  prop_ref_dec(s->s_current_origin);
  prop_ref_dec(s->s_root);
  prop_ref_dec(s->s_ext_value);
  prop_ref_dec(s->s_current_value);

  rstr_release(s->s_default_str);
  free(s->s_id);
  free(s->s_pending_value);
  free(s->s_store_name);
  free(s);
}

/**
 *
 */
void
setting_destroy(setting_t *s)
{
  if(s->s_on_group_list)
    LIST_REMOVE(s, s_group_link);
  s->s_callback = NULL;
  prop_unsubscribe(s->s_sub);
  prop_unsubscribe(s->s_inherited_value_sub);
  prop_unsubscribe(s->s_inherited_origin_sub);
  prop_destroy(s->s_root);
  setting_release(s);
}


/**
 *
 */
void
setting_group_destroy(struct setting_list *list)
{
  setting_t *s;
  while((s = LIST_FIRST(list)) != NULL)
    setting_destroy(s);
}

/**
 *
 */
void
setting_destroyp(setting_t **sp)
{
  if(*sp) {
    setting_destroy(*sp);
    *sp = NULL;
  }
}


/**
 *
 */
int
settings_get_type(const setting_t *s)
{
  return s->s_type;
}


/**
 *
 */
static void
settings_int_set_value(setting_t *s, int v)
{
  prop_callback_int_t *cb = s->s_callback;

  s->s_value_set = 1;

  if(cb != NULL)
    cb(s->s_opaque, v);

  if(s->s_ext_value)
    prop_set_int(s->s_ext_value, v);

  if(s->s_flags & SETTINGS_DEBUG)
    TRACE(TRACE_DEBUG, "Settings", "Value set to %d", v);

  if(!s->s_enable_writeback)
    return;

  switch(s->s_store_type) {
  case SETTING_STORETYPE_SIMPLE:
    htsmsg_store_set(s->s_store_name, s->s_id, HMF_S64, (int64_t)v);
    break;

  case SETTING_STORETYPE_KVSTORE:
    kv_url_opt_set(s->s_store_name, KVSTORE_DOMAIN_SETTING,
                   s->s_id, KVSTORE_SET_INT, v);
    break;
  }

  prop_set_string(s->s_current_origin, s->s_origin);
}


/**
 *
 */
static void
settings_int_callback(void *opaque, prop_event_t event, ...)
{
  setting_t *s = opaque;
  event_t *e;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_INT:
    settings_int_set_value(s, va_arg(ap, int));
    break;
  case PROP_SET_FLOAT:
    settings_int_set_value(s, va_arg(ap, double));
    break;
  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_action(e, ACTION_RESET))
      setting_reset(s);
    break;
  default:
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
settings_int_inherited_value(void *opaque, int v)
{
  setting_t *s = opaque;

  if(s->s_value_set)
    return;

  if(s->s_flags & SETTINGS_DEBUG)
    TRACE(TRACE_DEBUG, "Settings", "Value set to %d (inherited)", v);

  prop_callback_int_t *cb = s->s_callback;

  prop_set_int_ex(s->s_val, s->s_sub, v);

  if(cb) cb(s->s_opaque, v);

  if(s->s_ext_value)
    prop_set_int(s->s_ext_value, v);
}


/**
 *
 */
static void
settings_int_inherited_origin(void *opaque, rstr_t *origin)
{
  setting_t *s = opaque;

  if(!s->s_value_set)
    prop_set_rstring(s->s_current_origin, origin);
}


/**
 *
 */
static void
settings_string_set_value(setting_t *s, rstr_t *rstr)
{
  prop_callback_string_t *cb = s->s_callback;

  rstr_t *outval = rstr;

  s->s_value_set = 1;

  if((rstr == NULL || rstr_get(rstr)[0] == 0))
    outval = s->s_default_str;

  if(cb != NULL)
    cb(s->s_opaque, rstr_get(outval));

  if(s->s_ext_value)
    prop_set_rstring(s->s_ext_value, outval);

  if(!s->s_enable_writeback)
    return;

  switch(s->s_store_type) {
  case SETTING_STORETYPE_SIMPLE:
    htsmsg_store_set(s->s_store_name, s->s_id, HMF_STR, rstr_get(rstr));
    break;

  case SETTING_STORETYPE_KVSTORE:
    kv_url_opt_set(s->s_store_name, KVSTORE_DOMAIN_SETTING, s->s_id,
                   rstr ? KVSTORE_SET_STRING : KVSTORE_SET_VOID,
                   rstr_get(rstr));
    break;
  }
}


/**
 *
 */
static void
settings_string_callback(void *opaque, prop_event_t event, ...)
{
  setting_t *s = opaque;
  event_t *e;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_RSTRING:
    settings_string_set_value(s, va_arg(ap, rstr_t *));
    break;
  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_action(e, ACTION_RESET))
      setting_reset(s);
    break;
  default:
    break;
  }
  va_end(ap);
}

/**
 *
 */
static void
settings_str_inherited_value(void *opaque, rstr_t *v)
{
  setting_t *s = opaque;

  rstr_set(&s->s_default_str, v);
  prop_set(s->s_root, "defaultValue", PROP_SET_RSTRING, s->s_default_str);

  if(s->s_value_set)
    return;

  if(s->s_flags & SETTINGS_DEBUG)
    TRACE(TRACE_DEBUG, "Settings", "Value set to %s (inherited)", rstr_get(v));

  prop_set_void_ex(s->s_val, s->s_sub);

  if(s->s_callback != NULL) {
    prop_callback_string_t *cb = s->s_callback;
    cb(s->s_opaque, rstr_get(s->s_default_str));
  }

  if(s->s_ext_value)
    prop_set_rstring(s->s_ext_value, s->s_default_str);
}


/**
 *
 */
static void
settings_multiopt_callback_ng(void *opaque, prop_event_t event, ...)
{
  setting_t *s = opaque;
  prop_callback_string_t *cb;
  prop_t *c;
  va_list ap;
  va_start(ap, event);

  cb = s->s_callback;

  switch(event) {
  default:
    break;

  case PROP_SELECT_CHILD:
    if(s->s_pending_value) {
      free(s->s_pending_value);
      s->s_pending_value = NULL;
    }

    c = va_arg(ap, prop_t *);

    prop_ref_dec(s->s_current_value);
    s->s_current_value = prop_ref_inc(c);
    rstr_t *name = c ? prop_get_name(c) : NULL;

    if(s->s_ext_value)
      prop_set_rstring(s->s_ext_value, name);

    if(cb != NULL)
      cb(s->s_opaque, rstr_get(name));


    if(s->s_enable_writeback) {

      switch(s->s_store_type) {
      case SETTING_STORETYPE_SIMPLE:
        htsmsg_store_set(s->s_store_name, s->s_id, HMF_STR, rstr_get(name));
        break;

      case SETTING_STORETYPE_KVSTORE:
	kv_url_opt_set(s->s_store_name, KVSTORE_DOMAIN_SETTING,
                       s->s_id,
                       name ? KVSTORE_SET_STRING : KVSTORE_SET_VOID,
                       rstr_get(name));
      }
    }

    rstr_release(name);

    break;

  case PROP_DEL_CHILD:
    c = va_arg(ap, prop_t *);

    if(c == s->s_current_value) {
      c = prop_first_child(s->s_val);
      if(c != NULL) {
	prop_select(c);
	prop_ref_dec(c);
      }
    }
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
settings_generic_set_int(void *opaque, int value)
{
  int *p = opaque;
  *p = value;
}


/**
 *
 */
static void
settings_generic_set_int_from_str(void *opaque, const char *str)
{
  int *p = opaque;
  *p = atoi(str);
}


/**
 *
 */
setting_t *
setting_create(int type, prop_t *model, int flags, ...)
{
  setting_t *s = calloc(1, sizeof(setting_t));
  prop_courier_t *pc = NULL;
  void *mtx = NULL;
  void *lockmgr = NULL;
  int tag;
  const char *str, *str2;
  int initial_int = 0;
  const char *initial_str = NULL;
  int min = 0, max = 100, step = 1; // Just something
  const char **optlist;
  va_list ap;
  int i32;
  struct setting_list *list;
  prop_t *initial_value_prop = NULL;

  atomic_set(&s->s_refcount, 1);
  s->s_type = type;
  s->s_flags = flags;
  strcpy(s->s_origin, "local");
  va_start(ap, flags);

  if(model == NULL) {
    s->s_root = prop_ref_inc(prop_create_root(NULL));
  } else if(flags & SETTINGS_RAW_NODES) {
    s->s_root = prop_create_r(model, NULL);
  } else {
    prop_t *nodes = prop_create_r(model, "nodes");
    s->s_root = prop_create_r(nodes, NULL);
    prop_ref_dec(nodes);
  }

  switch(type) {
  case SETTING_INT:
  case SETTING_BOOL:
  case SETTING_STRING:
    s->s_val = prop_create_r(s->s_root, "value");
    break;

  case SETTING_MULTIOPT:
    s->s_val = prop_create_r(s->s_root, "options");
    break;


  case SETTING_ACTION:
    s->s_val = prop_create_r(s->s_root, "eventSink");
    break;

  case SETTING_SEPARATOR:
    break;

  default:
    abort();
  }

  prop_t *m = prop_create_r(s->s_root, "metadata");
  prop_t *title = prop_create_r(m, "title");
  prop_t *enabled = prop_create_r(s->s_root, "enabled");
  s->s_current_origin = prop_create_r(s->s_root, "origin");

  do {
    tag = va_arg(ap, int);
    switch(tag) {
    case SETTING_TAG_TITLE:
      prop_link(va_arg(ap, prop_t *), title);
      break;

    case SETTING_TAG_TITLE_CSTR:
      str = va_arg(ap, const char *);
      prop_set_string(title, str);
      break;

    case SETTING_TAG_CALLBACK:
      s->s_callback = va_arg(ap, void *);
      s->s_opaque   = va_arg(ap, void *);
      break;

    case SETTING_TAG_COURIER:
      pc = va_arg(ap, prop_courier_t *);
      break;

    case SETTING_TAG_MUTEX:
      mtx = va_arg(ap, void *);
      break;

    case SETTING_TAG_LOCKMGR:
      lockmgr = va_arg(ap, void *);
      break;

    case SETTING_TAG_STORE:
      s->s_store_type = SETTING_STORETYPE_SIMPLE;
      mystrset(&s->s_store_name, va_arg(ap, const char *));
      mystrset(&s->s_id, va_arg(ap, const char *));
      break;

    case SETTING_TAG_VALUE:
      switch(type) {
      case SETTING_INT:
      case SETTING_BOOL:
        initial_int = va_arg(ap, int);
        break;
      case SETTING_STRING:
      case SETTING_MULTIOPT:
        initial_str = va_arg(ap, const char *);
        break;
      }
      break;

    case SETTING_TAG_VALUE_PROP:
      initial_value_prop = va_arg(ap, prop_t *);
      break;

    case SETTING_TAG_RANGE:
      min  = va_arg(ap, int);
      max  = va_arg(ap, int);

      prop_set_int_clipping_range(s->s_val, min, max);
      break;

    case SETTING_TAG_STEP:
      step = va_arg(ap, int);
      break;

    case SETTING_TAG_UNIT_CSTR:
      str = va_arg(ap, const char *);
      prop_set(s->s_root, "unit", PROP_SET_STRING, str);
      break;

    case SETTING_TAG_OPTION:
      str = va_arg(ap, const char *);
      prop_t *opttitle = va_arg(ap, prop_t *);
      if(str != NULL)
        prop_setv(s->s_val, str, "title", NULL, PROP_SET_LINK, opttitle);
      break;

    case SETTING_TAG_OPTION_CSTR:
      str2 = va_arg(ap, const char *);
      str = va_arg(ap, const char *);
      prop_setv(s->s_val, str2, "title", NULL, PROP_SET_STRING, str);
      break;

    case SETTING_TAG_OPTION_LIST:
      optlist = va_arg(ap, const char **);
      if(optlist == NULL)
        break;

      while(*optlist) {
        prop_setv(s->s_val, optlist[0], "title", NULL,
                  PROP_SET_STRING, optlist[1]);
        optlist += 2;
      }
      break;

    case SETTING_TAG_WRITE_INT:
      s->s_opaque = va_arg(ap, int *);

      switch(type) {
      case SETTING_INT:
      case SETTING_BOOL:
        s->s_callback = &settings_generic_set_int;
        break;
      case SETTING_STRING:
      case SETTING_MULTIOPT:
        s->s_callback = &settings_generic_set_int_from_str;
        break;
      }
      break;

    case SETTING_TAG_ZERO_TEXT:
      prop_set(s->s_root, "zerotext", PROP_SET_LINK, va_arg(ap, prop_t *));
      break;

    case SETTING_TAG_WRITE_PROP:
      s->s_ext_value = prop_ref_inc(va_arg(ap, prop_t *));
      break;

    case SETTING_TAG_KVSTORE:
      s->s_store_type = SETTING_STORETYPE_KVSTORE;
      mystrset(&s->s_store_name, va_arg(ap, const char *));
      mystrset(&s->s_id, va_arg(ap, const char *));
      break;

    case SETTING_TAG_PROP_ENABLER:
      prop_link(va_arg(ap, prop_t *), enabled);
      prop_ref_dec(enabled);
      enabled = NULL;
      break;

    case SETTING_TAG_VALUE_ORIGIN:
      snprintf(s->s_origin, sizeof(s->s_origin), "%s",
               va_arg(ap, const char *));
      break;

    case SETTING_TAG_GROUP:
      list = va_arg(ap, struct setting_list *);
      LIST_INSERT_HEAD(list, s, s_group_link);
      s->s_on_group_list = 1;
      break;

    case SETTING_TAG_INHERIT:
      s->s_parent = va_arg(ap, setting_t *);
      atomic_inc(&s->s_parent->s_refcount);
      initial_int = INT32_MIN;
      break;

    case 0:
      break;

    default:
      fprintf(stderr, "%s: Unsupported tag value 0x%x\n",
              __FUNCTION__, tag);
      abort();
    }
  } while(tag);

  prop_set(s->s_root, "type", PROP_SET_STRING,
           (const char *[]) {
             [SETTING_INT]        = "integer",
               [SETTING_BOOL]     = "bool",
               [SETTING_STRING]   = "string",
               [SETTING_MULTIOPT] = "multiopt",
               [SETTING_ACTION]   = "action",
               [SETTING_SEPARATOR]= "separator",
               }[type]);

  rstr_t *curstr = NULL;

  switch(type) {

  case SETTING_INT:
    prop_set(s->s_root, "min",  PROP_SET_INT, min);
    prop_set(s->s_root, "max",  PROP_SET_INT, max);
    prop_set(s->s_root, "step", PROP_SET_INT, step);
    // FALLTHRU
  case SETTING_BOOL:

    s->s_default_int = initial_int;

    i32 = INT32_MIN;


    switch(s->s_store_type) {
    case SETTING_STORETYPE_SIMPLE:
      i32 = htsmsg_store_get_int(s->s_store_name, s->s_id, INT32_MIN);
      break;

    case SETTING_STORETYPE_KVSTORE:
      i32 = kv_url_opt_get_int(s->s_store_name, KVSTORE_DOMAIN_SETTING,
                               s->s_id, INT32_MIN);
      break;
    }

    if(i32 == INT32_MIN)
      i32 = initial_int;

    if(i32 != INT32_MIN) {
      // If this setting originated the value, then set it

      // Clamp value
      if(type == SETTING_INT) {


        int x = MIN(max, MAX(min, i32));

        if(x != i32)
          TRACE(TRACE_DEBUG, "Settings", "Value %d clamped to %d", i32, x);
        i32 = x;

      } else if(type == SETTING_BOOL) {
        i32 = !!i32;
      }

      s->s_value_set = 1;
      prop_set_string(s->s_current_origin, s->s_origin);
      prop_set_int(s->s_val, i32);
      if(flags & SETTINGS_INITIAL_UPDATE)
        settings_int_set_value(s, i32);
    }

    s->s_sub =
      prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
                     PROP_TAG_CALLBACK, settings_int_callback, s,
                     PROP_TAG_ROOT, s->s_val,
                     PROP_TAG_COURIER, pc,
                     PROP_TAG_MUTEX, mtx,
                     PROP_TAG_LOCKMGR, lockmgr,
                     NULL);

    if(s->s_parent != NULL) {
      s->s_inherited_value_sub =
        prop_subscribe(PROP_SUB_IGNORE_VOID,
                       PROP_TAG_CALLBACK_INT, settings_int_inherited_value, s,
                       PROP_TAG_ROOT, s->s_parent->s_val,
                       PROP_TAG_COURIER, pc,
                       PROP_TAG_MUTEX, mtx,
                       PROP_TAG_LOCKMGR, lockmgr,
                       NULL);

      s->s_inherited_origin_sub =
        prop_subscribe(PROP_SUB_IGNORE_VOID,
                       PROP_TAG_CALLBACK_RSTR, settings_int_inherited_origin, s,
                       PROP_TAG_NAMED_ROOT, s->s_parent->s_root, "setting",
                       PROP_TAG_NAME("setting", "origin"),
                       PROP_TAG_COURIER, pc,
                       PROP_TAG_MUTEX, mtx,
                       PROP_TAG_LOCKMGR, lockmgr,
                       NULL);

    } else if(initial_value_prop != NULL) {
      s->s_inherited_value_sub =
        prop_subscribe(PROP_SUB_IGNORE_VOID,
                       PROP_TAG_CALLBACK_INT, settings_int_inherited_value, s,
                       PROP_TAG_ROOT, initial_value_prop,
                       PROP_TAG_COURIER, pc,
                       PROP_TAG_MUTEX, mtx,
                       PROP_TAG_LOCKMGR, lockmgr,
                       NULL);
    }
    break;

  case SETTING_STRING:
    if(flags & SETTINGS_PASSWORD)
      prop_set(s->s_root, "password", PROP_SET_INT, 1);
    if(flags & SETTINGS_FILE)
      prop_set(s->s_root, "fileRequest", PROP_SET_INT, 1);
    if(flags & SETTINGS_DIR)
      prop_set(s->s_root, "dirRequest", PROP_SET_INT, 1);

    s->s_default_str = rstr_alloc(initial_str);
    prop_set(s->s_root, "defaultValue", PROP_SET_RSTRING, s->s_default_str);

    rstr_t *initial = NULL;

    switch(s->s_store_type) {
    case SETTING_STORETYPE_SIMPLE:
      initial = htsmsg_store_get_str(s->s_store_name, s->s_id);
      break;

    case SETTING_STORETYPE_KVSTORE:
      initial = kv_url_opt_get_rstr(s->s_store_name, KVSTORE_DOMAIN_SETTING,
                                    s->s_id);
      break;
    }

    if(initial != NULL && rstr_get(initial)[0] == 0) {
      rstr_release(initial);
      initial = NULL;
    }

    prop_set_rstring(s->s_val, initial);

    if(flags & SETTINGS_INITIAL_UPDATE && initial_value_prop == NULL)
      settings_string_set_value(s, initial);

    rstr_release(initial);

    s->s_sub =
      prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE | PROP_SUB_IGNORE_VOID,
                     PROP_TAG_CALLBACK, settings_string_callback, s,
                     PROP_TAG_ROOT, s->s_val,
                     PROP_TAG_COURIER, pc,
                     PROP_TAG_MUTEX, mtx,
                     PROP_TAG_LOCKMGR, lockmgr,
                     NULL);

    if(initial_value_prop != NULL) {
      s->s_inherited_value_sub =
        prop_subscribe(PROP_SUB_IGNORE_VOID,
                       PROP_TAG_CALLBACK_RSTR, settings_str_inherited_value, s,
                       PROP_TAG_ROOT, initial_value_prop,
                       PROP_TAG_COURIER, pc,
                       PROP_TAG_MUTEX, mtx,
                       PROP_TAG_LOCKMGR, lockmgr,
                       NULL);
    }
    break;


  case SETTING_MULTIOPT:


    switch(s->s_store_type) {
    case SETTING_STORETYPE_SIMPLE:
      curstr = htsmsg_store_get_str(s->s_store_name, s->s_id);
      break;

    case SETTING_STORETYPE_KVSTORE:
      curstr = kv_url_opt_get_rstr(s->s_store_name, KVSTORE_DOMAIN_SETTING,
                                   s->s_id);
      break;
    }

    prop_t *o = NULL;
    if(curstr != NULL) {
      o = prop_find(s->s_val, rstr_get(curstr), NULL);
      rstr_release(curstr);
    }

    if(o == NULL && initial_str != NULL)
      o = prop_find(s->s_val, initial_str, NULL);

    if(o == NULL) {
      mystrset(&s->s_pending_value, initial_str);
      o = prop_first_child(s->s_val);
    }

    if(o != NULL) {
      prop_select(o);

      if(flags & SETTINGS_INITIAL_UPDATE) {
        rstr_t *name = prop_get_name(o);
        settings_string_set_value(s, name);
        rstr_release(name);
      }
      s->s_current_value = o;
    }

    prop_linkselected_create(s->s_val, s->s_root, "current", "value");

    s->s_sub =
      prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
                     PROP_TAG_CALLBACK, settings_multiopt_callback_ng, s,
                     PROP_TAG_ROOT, s->s_val,
                     PROP_TAG_COURIER, pc,
                     PROP_TAG_MUTEX, mtx,
                     PROP_TAG_LOCKMGR, lockmgr,
                     NULL);
    break;

  case SETTING_ACTION:
    s->s_sub =
      prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
                     PROP_TAG_CALLBACK_EVENT, s->s_callback, s->s_opaque,
                     PROP_TAG_ROOT, s->s_val,
                     PROP_TAG_COURIER, pc,
                     PROP_TAG_MUTEX, mtx,
                     PROP_TAG_LOCKMGR, lockmgr,
                     NULL);
    break;

  case SETTING_SEPARATOR:
    break;
  }


  if(enabled != NULL) {
    prop_set_int(enabled, 1);
    prop_ref_dec(enabled);
  }

  s->s_enable_writeback = 1;
  prop_ref_dec(title);
  prop_ref_dec(m);
  return s;
}


/**
 *
 */
prop_t *
setting_add_option(setting_t *s, const char *id,
                   const char *title, int sel)
{
  prop_t *opt = prop_create(s->s_val, id);
  prop_set(opt, "title", PROP_SET_STRING, title);

  if((s->s_pending_value && !strcmp(s->s_pending_value, id)) || sel) {
    free(s->s_pending_value);
    s->s_pending_value = NULL;
    prop_select(opt);
  }
  return opt;
}


/**
 *
 */
void
setting_set(setting_t *s, int type, ...)
{
  if(s->s_type != type)
    return;

  const char *str;
  int i32;
  va_list ap;
  va_start(ap, type);

  switch(type) {
  case SETTING_INT:
  case SETTING_BOOL:
    i32 = va_arg(ap, int);
    prop_set_int(s->s_val, i32);
    break;

  case SETTING_STRING:
    abort();
    break;

  case SETTING_MULTIOPT:
    str = va_arg(ap, const char *);
    prop_select_by_value(s->s_val, str);
    break;
  }
  va_end(ap);
}


/**
 *
 */
void
setting_reset(setting_t *s)
{
  s->s_value_set = 0;

  switch(s->s_store_type) {
  case SETTING_STORETYPE_SIMPLE:
    htsmsg_store_set(s->s_store_name, s->s_id, -1);
    break;

  case SETTING_STORETYPE_KVSTORE:
    kv_url_opt_set(s->s_store_name, KVSTORE_DOMAIN_SETTING, s->s_id,
                   KVSTORE_SET_VOID);
    break;
  }

  if(s->s_parent == NULL) {

    switch(s->s_type) {
    case SETTING_STRING:
      prop_set_void_ex(s->s_val, s->s_sub);
      if(s->s_callback != NULL) {
        prop_callback_string_t *cb = s->s_callback;
        cb(s->s_opaque, rstr_get(s->s_default_str));
      }

      if(s->s_ext_value)
        prop_set_rstring(s->s_ext_value, s->s_default_str);

      break;
    case SETTING_INT:
    case SETTING_BOOL:
      prop_set_int_ex(s->s_val, s->s_sub, s->s_default_int);
      if(s->s_callback != NULL) {
        prop_callback_int_t *cb = s->s_callback;
        cb(s->s_opaque, s->s_default_int);
      }
      if(s->s_ext_value != NULL)
        prop_set_int(s->s_ext_value, s->s_default_int);
      break;
    default:
      printf("Can't reset type %d\n", s->s_type);
      break;
    }

  } else {
    prop_sub_reemit(s->s_inherited_value_sub);
    prop_sub_reemit(s->s_inherited_origin_sub);
  }
}


/**
 *
 */
void
setting_group_reset(struct setting_list *list)
{
  setting_t *s;
  LIST_FOREACH(s, list, s_group_link)
    setting_reset(s);
}


/**
 *
 */
void
setting_push_to_ancestor(setting_t *s, const char *ancestor)
{
  setting_t *a;

  for(a = s; a != NULL; a = a->s_parent) {
    if(!strcmp(ancestor, a->s_origin))
      break;
  }
  if(a != NULL)
    prop_copy(a->s_val, s->s_val);
}


/**
 *
 */
void
setting_group_push_to_ancestor(struct setting_list *list, const char *ancestor)
{
  setting_t *s;
  LIST_FOREACH(s, list, s_group_link)
    setting_push_to_ancestor(s, ancestor);
}


/**
 *
 */
static void
set_system_name(void *opaque, const char *str)
{
  snprintf(gconf.system_name, sizeof(gconf.system_name), "%s", str);
  prop_setv(prop_get_global(), "app", "systemname", NULL,
            PROP_SET_STRING, str);
#if STOS && ENABLE_AVAHI
  extern void avahi_update_hostname(void);
  avahi_update_hostname();
#endif

}


/**
 *
 */
void
settings_init(void)
{
  prop_t *n, *d;
  prop_t *s1;

  hts_mutex_init(&settings_mutex);

  prop_t *settings_root = prop_create(prop_get_global(), "settings");
  settings_model = prop_create(settings_root, "model");

  prop_set(settings_model, "type", PROP_SET_STRING, "settings");
  set_title2(settings_model, _p("Global settings"));

  settings_nodes = prop_create_root(NULL);
  s1 = prop_create_root(NULL);

  struct prop_nf *pnf;

  pnf = prop_nf_create(s1, settings_nodes, NULL, PROP_NF_AUTODESTROY);
  prop_nf_sort(pnf, "node.metadata.title", 0, 0, NULL, 1);

  gconf.settings_apps = prop_create(settings_root, "apps");
  gconf.settings_sd = prop_create(settings_root, "sd");

  prop_concat_t *pc;

  pc = prop_concat_create(prop_create(settings_model, "nodes"));

  prop_concat_add_source(pc, s1, NULL);

  // About

  n = prop_create_root(NULL);
  settings_add_url(n,
		   _p("About"), "about", NULL, NULL, "page:about",
		   SETTINGS_RAW_NODES);

  d = prop_create_root(NULL);
  prop_set(d, "type", PROP_SET_STRING, "separator");
  prop_concat_add_source(pc, n, d);


  // Applications and plugins

  n = prop_create(gconf.settings_apps, "nodes");

  d = prop_create_root(NULL);
  set_title2(d, _p("Applications and installed plugins"));
  prop_set(d, "type", PROP_SET_STRING, "separator");
  prop_concat_add_source(pc, n, d);

  // Discovered sources

  d = prop_create_root(NULL);
  set_title2(d, _p("Discovered media sources"));
  prop_set(d, "type", PROP_SET_STRING, "separator");

  n = prop_create(gconf.settings_sd, "nodes");
  prop_concat_add_source(pc, n, d);


  gconf.settings_network =
    settings_add_dir(NULL, _p("Network settings"), "network", NULL,
                     _p("Network services, etc"),
                     "settings:network");

  // Add configurable system name

  setting_create(SETTING_STRING, gconf.settings_network,
		 SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("System name")),
		 SETTING_VALUE(APPNAME),
                 SETTING_CALLBACK(set_system_name, NULL),
                 SETTING_STORE("netinfo", "sysname"),
                 NULL);


  // Look and feel settings

  prop_t *lnf =
    settings_add_dir(NULL, _p("Look and feel"),
		     "display", NULL,
		     _p("Fonts and user interface styling"),
		     "settings:lookandfeel");

  gconf.settings_look_and_feel =
    prop_concat_create(prop_create(lnf, "nodes"));

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
  usage_page_open(sync, "Settings");
  prop_set(page, "model", PROP_SET_LINK, settings_model);
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
  if(title)
    prop_setv(d, "metadata", "title", NULL, PROP_SET_LINK, title);
  prop_set(d, "type", PROP_SET_STRING, "separator");
  return d;

}


/**
 *
 */
static void
add_dev_bool(const char *title, const char *id, int *val)
{
  setting_create(SETTING_BOOL, gconf.settings_dev, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE_CSTR(title),
                 SETTING_VALUE(*val),
                 SETTING_WRITE_BOOL(val),
                 SETTING_STORE("dev", id),
                 NULL);
#if 0
  if(htsmsg_get_u32_or_default(s, id, 0))
    TRACE(TRACE_DEBUG, "DEV", "Developer setting '%s' is enabled",
          title, id);
#endif
}


#if ENABLE_NETLOG
/**
 *
 */
static void
set_netlog(void *opaque, const char *str)
{
  if(str == NULL) {
    gconf.log_server_ipv4 = 0;
    return;
  }
  char *msg = mystrdupa(str);

  char *p = strchr(msg, ':');
  if(p != NULL) {
    *p++ = 0;
    gconf.log_server_port = atoi(p);
  } else {
    gconf.log_server_port = 4000;
  }

  struct in_addr addr;

  if(inet_pton(AF_INET, msg, &addr) != 1) {
    gconf.log_server_ipv4 = 0;
  } else {
    gconf.log_server_ipv4 = addr.s_addr;
  }
}
#endif

/**
 *
 */
static void
init_dev_settings(void)
{
  gconf.settings_dev = settings_add_dir(prop_create_root(NULL),
				  _p("Developer settings"), NULL, NULL,
				  _p("Settings useful for developers"),
				  "settings:dev");

  prop_t *r = setting_add(gconf.settings_dev, NULL, "info", 0);
  prop_set_string(prop_create(r, "description"),
		  "Settings for developers. If you don't know what this is, don't touch it");

#if ENABLE_UPGRADE
  add_dev_bool("Enable binreplace",
	       "binreplace", &gconf.enable_bin_replace);

  add_dev_bool("Enable omnigrade",
	       "omnigrade", &gconf.enable_omnigrade);
#endif

#ifndef NDEBUG
  add_dev_bool("Disable analytics",
	       "disableanalytics", &gconf.disable_analytics);
#endif

  add_dev_bool("Always close pages when pressing back",
	       "navalwaysclose", &gconf.enable_nav_always_close);

  add_dev_bool("Disable HTTP connection reuse",
	       "nohttpreuse", &gconf.disable_http_reuse);

  add_dev_bool("Enable indexer option",
	       "enable_indexer", &gconf.enable_indexer);

  if(gconf.arch_dev_opts)
    gconf.arch_dev_opts(&add_dev_bool);

#if ENABLE_NETLOG
  setting_create(SETTING_STRING, gconf.settings_dev, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE_CSTR("Network log destination"),
                 SETTING_CALLBACK(set_netlog, NULL),
                 SETTING_STORE("dev", "netlogdest"),
                 NULL);
#endif

  // ---------- debug filtering

  setting_add_cstr(gconf.settings_dev,
                   "Debug log filtering", "separator", 0);

  add_dev_bool("Debug all HTTP requests",
	       "httpdebug", &gconf.enable_http_debug);

  add_dev_bool("Debug various ecmascript engine things",
	       "ecmascriptdebug", &gconf.enable_ecmascript_debug);

  add_dev_bool("Log AV-diff stats",
	       "detailedavdiff", &gconf.enable_detailed_avdiff);
#ifdef PS3
  add_dev_bool("Log memory usage",
	       "memdebug", &gconf.enable_mem_debug);
#endif

  add_dev_bool("Debug HLS",
	       "hlsdebug", &gconf.enable_hls_debug);

  add_dev_bool("Debug FTP Client",
	       "ftpdebug", &gconf.enable_ftp_client_debug);

  add_dev_bool("Debug FTP Server",
	       "ftpserverdebug", &gconf.enable_ftp_server_debug);
#if CONFIG_LIBCEC
  add_dev_bool("Debug CEC",
	       "cecdebug", &gconf.enable_cec_debug);
#endif
  add_dev_bool("Debug directory listings",
	       "fascannerdebug", &gconf.enable_fa_scanner_debug);

  add_dev_bool("Debug library indexer",
	       "indexerdebug", &gconf.enable_indexer_debug);

  add_dev_bool("Debug SMB/CIFS (Windows File Sharing)",
	       "smbdebug", &gconf.enable_smb_debug);

  add_dev_bool("Debug read/writes to URL key/value store",
	       "kvstoredebug", &gconf.enable_kvstore_debug);

  add_dev_bool("Debug icecast streaming",
	       "icecastdebug", &gconf.enable_icecast_debug);

  add_dev_bool("Debug image loading and decoding",
	       "imagedebug", &gconf.enable_image_debug);

  add_dev_bool("Debug metadata lookups",
	       "metadatadebug", &gconf.enable_metadata_debug);

  add_dev_bool("Debug settings store/load from disk",
	       "settingsdebug", &gconf.enable_settings_debug);

  add_dev_bool("Debug threads",
	       "threadsdebug", &gconf.enable_thread_debug);

  add_dev_bool("Debug UPNP",
	       "upnp", &gconf.enable_upnp_debug);
#if ENABLE_BITTORRENT
  add_dev_bool("Debug Bittorrent general events",
	       "bt", &gconf.enable_torrent_debug);

  add_dev_bool("Debug Bittorrent tracker communication",
	       "bttracker", &gconf.enable_torrent_tracker_debug);

  add_dev_bool("Debug Bittorrent peer connections",
	       "btpeercon", &gconf.enable_torrent_peer_connection_debug);

  add_dev_bool("Debug Bittorrent peer downloading",
	       "btpeerdl", &gconf.enable_torrent_peer_download_debug);

  add_dev_bool("Debug Bittorrent peer uploading",
	       "btpeerul", &gconf.enable_torrent_peer_upload_debug);

  add_dev_bool("Debug Bittorrent disk I/O",
	       "btdiskio", &gconf.enable_torrent_diskio_debug);
#endif
  add_dev_bool("Debug input events",
	       "inputevents", &gconf.enable_input_event_debug);

#ifdef __ANDROID__
  add_dev_bool("Debug touch events",
               "touchevents", &gconf.enable_touch_debug);

  add_dev_bool("Debug MediaCodec",
               "androidMediaCodec", &gconf.enable_MediaCodec_debug);
#endif
}


static prop_t *
addgroup(prop_concat_t *pc, prop_t *title)
{
  prop_t *p = prop_create_root(NULL);
  prop_concat_add_source(pc, prop_create(p, "nodes"), makesep(title));
  return p;
}



prop_t *
setting_get_dir(const char *key)
{
  prop_t *r = NULL;
  const char *k2;
  hts_mutex_lock(&settings_mutex);
  if(!strcmp(key, "settings:tv")) {
    static prop_t *tvsettings;
    if(tvsettings == NULL) {
      tvsettings = settings_add_dir(NULL, _p("TV Control"),
                                    "display", NULL,
                                    _p("Configure communications with your TV"),
                                    "settings:tv");
    }
    r = tvsettings;
  } else if((k2 = mystrbegins(key, "general:")) != NULL) {

    static prop_t *general;

    static prop_t *misc;
    static prop_t *resets;
    static prop_t *filebrowse;
    static prop_t *plugins;
    static prop_t *runcontrol;
    static prop_t *upgrade;

    if(general == NULL) {
      general = settings_add_dir(NULL, _p("General"), NULL, NULL,
                                 _p("System related settings"),
                                 "settings:general");
      prop_concat_t *pc = prop_concat_create(prop_create(general, "nodes"));


      misc       = addgroup(pc, NULL);
      upgrade    = addgroup(pc, _p("Software upgrade"));
      filebrowse = addgroup(pc, _p("File browsing"));
      runcontrol = addgroup(pc, _p("Starting and stopping"));
      plugins    = addgroup(pc, _p("Plugin repositories"));
      resets     = addgroup(pc, _p("Reset"));
    }

    if(!strcmp(k2, "resets")) {
      r = resets;
    } else if(!strcmp(k2, "runcontrol")) {
      r = runcontrol;
    } else if(!strcmp(k2, "upgrade")) {
      r = upgrade;
    } else if(!strcmp(k2, "filebrowse")) {
      r = filebrowse;
    } else if(!strcmp(k2, "plugins")) {
      r = plugins;
    } else if(!strcmp(k2, "misc")) {
      r = misc;
    } else {

      printf("setting key lookup \"%s\" not found\n", k2);
      abort();
    }
  }



  hts_mutex_unlock(&settings_mutex);
  return r;
}
