/*
 *  JSAPI <-> Navigator page object
 *  Copyright (C) 2010 Andreas Öman
 *  Copyright (C) 2012 Fábio Ferreira
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

#include <sys/types.h>
#include <assert.h>
#include <string.h>
#include "js.h"


#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "backend/search.h"
#include "navigator.h"
#include "misc/str.h"
#include "misc/regex.h"
#include "prop/prop_nodefilter.h"
#include "event.h"
#include "metadata/metadata.h"
#include "htsmsg/htsmsg_json.h"

TAILQ_HEAD(js_item_queue, js_item);

static hts_mutex_t js_page_mutex; // protects global lists

static struct js_route_list js_routes;
static struct js_searcher_list js_searchers;

static JSFunctionSpec item_proto_functions[];

/**
 *
 */
typedef struct js_route {
  js_plugin_t *jsr_jsp;
  LIST_ENTRY(js_route) jsr_global_link;
  LIST_ENTRY(js_route) jsr_plugin_link;
  char *jsr_pattern;
  hts_regex_t jsr_regex;
  jsval jsr_openfunc;
  int jsr_prio;
} js_route_t;


/**
 *
 */
typedef struct js_searcher {
  js_plugin_t *jss_jsp;
  LIST_ENTRY(js_searcher) jss_global_link;
  LIST_ENTRY(js_searcher) jss_plugin_link;
  jsval jss_openfunc;
  char *jss_title;
  char *jss_icon;

} js_searcher_t;


/**
 *
 */
typedef struct js_model {

  js_context_private_t jm_ctxpriv; // Must be first

  int jm_refcount;

  char **jm_args;

  jsval jm_openfunc;

  prop_t *jm_nodes;
  prop_t *jm_actions;
  prop_t *jm_root;

  prop_t *jm_loading;
  prop_t *jm_type;
  prop_t *jm_error;
  prop_t *jm_contents;
  prop_t *jm_entries;
  prop_t *jm_source;
  prop_t *jm_metadata;
  prop_t *jm_options;

  prop_t *jm_eventsink;

  prop_courier_t *jm_pc;
  prop_sub_t *jm_nodesub;

  prop_sub_t *jm_eventsub;

  jsval jm_paginator;
  jsval jm_reorderer;
  
  JSContext *jm_cx;

  struct js_event_handler_list jm_event_handlers;

  struct js_item_queue jm_items;

  int jm_subs;

  jsval jm_item_proto;

  char *jm_url;

  struct js_subscription_list jm_subscriptions;

  int jm_pending_want_more;

} js_model_t;


static JSObject *make_model_object(JSContext *cx, js_model_t *jm,
				   jsval *root);

static void install_nodesub(js_model_t *jm);

/**
 *
 */
void
js_page_init(void)
{
  hts_mutex_init(&js_page_mutex);
}

/**
 *
 */
static js_model_t *
js_model_create(JSContext *cx, jsval openfunc)
{
  js_model_t *jm = calloc(1, sizeof(js_model_t));
  jm->jm_refcount = 1;
  jm->jm_openfunc = openfunc;
  JS_AddNamedRoot(cx, &jm->jm_openfunc, "openfunc");
  TAILQ_INIT(&jm->jm_items);
  return jm;
}


/**
 *
 */
static void
js_model_destroy(js_model_t *jm)
{
  assert(TAILQ_FIRST(&jm->jm_items) == NULL);

  if(jm->jm_args)
    strvec_free(jm->jm_args);

  prop_unsubscribe(jm->jm_eventsub);

  if(jm->jm_root)      prop_ref_dec(jm->jm_root);
  if(jm->jm_loading)   prop_ref_dec(jm->jm_loading);
  if(jm->jm_nodes)     prop_ref_dec(jm->jm_nodes);
  if(jm->jm_actions)   prop_ref_dec(jm->jm_actions);
  if(jm->jm_type)      prop_ref_dec(jm->jm_type);
  if(jm->jm_error)     prop_ref_dec(jm->jm_error);
  if(jm->jm_contents)  prop_ref_dec(jm->jm_contents);
  if(jm->jm_entries)   prop_ref_dec(jm->jm_entries);
  if(jm->jm_source)    prop_ref_dec(jm->jm_source);
  if(jm->jm_metadata)  prop_ref_dec(jm->jm_metadata);
  if(jm->jm_options)   prop_ref_dec(jm->jm_options);
  if(jm->jm_eventsink) prop_ref_dec(jm->jm_eventsink);

  if(jm->jm_pc != NULL)
    prop_courier_destroy(jm->jm_pc);
  free(jm->jm_url);
  free(jm);
}


/**
 *
 */
static void
js_model_release(js_model_t *jm)
{
  if(atomic_add(&jm->jm_refcount, -1) == 1)
    js_model_destroy(jm);
}


/**
 *
 */
static JSBool 
js_setEntries(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_entries, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setType(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_type, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setContents(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_contents, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setSource(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_prop_set_from_jsval(cx, jm->jm_source, *vp);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setLoading(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_model_t *jm = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  prop_set_int(jm->jm_loading, on);
  return JS_TRUE;
}


/**
 *
 */
typedef struct js_item {
  js_model_t *ji_model;
  TAILQ_ENTRY(js_item) ji_link;
  prop_t *ji_root;
  struct js_event_handler_list ji_event_handlers;
  prop_sub_t *ji_eventsub;
  jsval ji_this;
  int ji_enable_set_property;
  rstr_t *ji_url;
  metadata_lazy_video_t *ji_mlv;
} js_item_t;


/**
 *
 */
static void
item_finalize(JSContext *cx, JSObject *obj)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  assert(LIST_FIRST(&ji->ji_event_handlers) == NULL);
  TAILQ_REMOVE(&ji->ji_model->jm_items, ji, ji_link);
  js_model_release(ji->ji_model);
  prop_ref_dec(ji->ji_root);
  rstr_release(ji->ji_url);
  if(ji->ji_mlv != NULL)
    mlv_unbind(ji->ji_mlv);
  free(ji);
}

#if 0
/**
 *
 */
static JSBool
item_set_property(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);

  if(!ji->ji_enable_set_property)
    return JS_TRUE;

  const char *name = JSVAL_IS_STRING(id) ? 
    JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;

  if(name != NULL)
    js_prop_set_from_jsval(cx, prop_create(ji->ji_root, name), *vp);

  return JS_TRUE;
}
#endif

/**
 *
 */
static JSClass item_class = {
  "item", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, item_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static void
js_item_eventsub(void *opaque, prop_event_t event, ...)
{
  js_item_t *ji = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    js_event_destroy_handlers(ji->ji_model->jm_cx, &ji->ji_event_handlers);
    prop_unsubscribe(ji->ji_eventsub);
    ji->ji_eventsub = NULL;
    ji->ji_model->jm_subs--;
    JS_RemoveRoot(ji->ji_model->jm_cx, &ji->ji_this);
    prop_tag_clear(ji->ji_root, ji->ji_model);
    break;

  case PROP_EXT_EVENT:
    js_event_dispatch(ji->ji_model->jm_cx, &ji->ji_event_handlers,
		      va_arg(ap, event_t *), JSVAL_TO_OBJECT(ji->ji_this));
    break;
  }
  va_end(ap);
}


/**
 *
 */
static JSBool 
js_item_onEvent(JSContext *cx, JSObject *obj,
		uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_event_handler_create(cx, &ji->ji_event_handlers,
			  JSVAL_IS_STRING(argv[0]) ?
			  JS_GetStringBytes(JS_ValueToString(cx, argv[0])) :
			  NULL,
			  argv[1]);
  
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_destroy(JSContext *cx, JSObject *obj,
		uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  prop_destroy(ji->ji_root);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_moveBefore(JSContext *cx, JSObject *obj,
		   uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  js_item_t *before;

  if(argc >= 1 && JSVAL_IS_OBJECT(argv[0]) &&
     !JSVAL_IS_NULL(argv[0]) && 
     JS_GetClass(cx, JSVAL_TO_OBJECT(argv[0])) == &item_class) {
    before = JS_GetPrivate(cx, JSVAL_TO_OBJECT(argv[0]));
  } else {
    before = NULL;
  }

  TAILQ_REMOVE(&ji->ji_model->jm_items, ji, ji_link);
  if(before)
    TAILQ_INSERT_BEFORE(before, ji, ji_link);
  else
    TAILQ_INSERT_TAIL(&ji->ji_model->jm_items, ji, ji_link);

  prop_move(ji->ji_root, before ? before->ji_root : NULL);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_item_dump(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  prop_print_tree(ji->ji_root, 1);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_enable(JSContext *cx, JSObject *obj,
	       uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  prop_set(ji->ji_root, "enabled", PROP_SET_INT, 1);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_disable(JSContext *cx, JSObject *obj,
		uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  prop_set(ji->ji_root, "enabled", PROP_SET_INT, 0);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_item_addOptURL(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  const char *title;
  const char *url;

  if (!JS_ConvertArguments(cx, argc, argv, "ss", &title, &url))
    return JS_FALSE;
  
  prop_t *p = prop_create_root(NULL);
  prop_set_string(prop_create(p, "type"), "location");
  prop_set_string(prop_create(p, "url"), url);
  prop_set_string(prop_create(prop_create(p, "metadata"), "title"), title);
  prop_set_int(prop_create(p, "enabled"), 1);

  prop_t *opts = prop_create_r(ji->ji_root, "options");
  if(prop_set_parent(p, opts))
    prop_destroy(p);
  prop_ref_dec(opts);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_addOptAction(JSContext *cx, JSObject *obj,
		     uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  const char *title;
  const char *action;

  if (!JS_ConvertArguments(cx, argc, argv, "ss", &title, &action))
    return JS_FALSE;
  
  prop_t *p = prop_create_root(NULL);
  prop_set_string(prop_create(p, "type"), "action");
  prop_set_string(prop_create(prop_create(p, "metadata"), "title"), title);
  prop_set_int(prop_create(p, "enabled"), 1);
  prop_set_string(prop_create(p, "action"), action);

  prop_t *opts = prop_create_r(ji->ji_root, "options");
  if(prop_set_parent(p, opts))
    prop_destroy(p);
  prop_ref_dec(opts);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_item_addOptSeparator(JSContext *cx, JSObject *obj,
		     uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  const char *title;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &title))
    return JS_FALSE;
  
  prop_t *p = prop_create_root(NULL);
  prop_set_string(prop_create(prop_create(p, "metadata"), "title"), title);
  prop_set_string(prop_create(p, "type"), "separator");
  prop_set_int(prop_create(p, "enabled"), 1);

  prop_t *opts = prop_create_r(ji->ji_root, "options");
  if(prop_set_parent(p, opts))
    prop_destroy(p);
  prop_ref_dec(opts);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_item_bindVideoMetadata(JSContext *cx, JSObject *obj,
			  uintN argc, jsval *argv, jsval *rval)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  JSObject *o = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "o", &o))
    return JS_FALSE;
  
  rstr_t *title = js_prop_rstr(cx, o, "filename");
  int year      = js_prop_int_or_default(cx, o, "year", 0);

  if(title != NULL) {
    // Raw filename case
    title = metadata_remove_postfix_rstr(title);
    year = -1;
  } else {
    title = js_prop_rstr(cx, o, "title");
  }

  int season    = js_prop_int_or_default(cx, o, "season", -1);
  int episode   = js_prop_int_or_default(cx, o, "episode", -1);
  rstr_t *imdb  = js_prop_rstr(cx, o, "imdb");
  int duration  = js_prop_int_or_default(cx, o, "duration", 0);

  if(ji->ji_mlv != NULL)
    mlv_unbind(ji->ji_mlv);

  ji->ji_mlv =
    metadata_bind_video_info(ji->ji_url, title, imdb, duration,
			     ji->ji_root, NULL, 0, 0, year, season, episode);
  rstr_release(imdb);
  rstr_release(title);
  
  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
static JSFunctionSpec item_proto_functions[] = {
  JS_FS("onEvent",            js_item_onEvent,         2, 0, 0),
  JS_FS("destroy",            js_item_destroy,         0, 0, 0),
  JS_FS("addOptURL",          js_item_addOptURL,       2, 0, 0),
  JS_FS("addOptAction",       js_item_addOptAction,    2, 0, 0),
  JS_FS("addOptSeparator",    js_item_addOptSeparator, 1, 0, 0),
  JS_FS("dump",               js_item_dump,            0, 0, 0),
  JS_FS("enable",             js_item_enable,          0, 0, 0),
  JS_FS("disable",            js_item_disable,         0, 0, 0),
  JS_FS("moveBefore",         js_item_moveBefore,      1, 0, 0),
  JS_FS("bindVideoMetadata",  js_item_bindVideoMetadata, 1, 0, 0),
  JS_FS_END
};


/**
 *
 */
static JSBool 
js_appendItem0(JSContext *cx, js_model_t *model, prop_t *parent,
	       const char *url, const char *type, JSObject *metaobj,
	       jsval *data, jsval *rval, int enabled,
	       const char *metabind)
{
  install_nodesub(model);

  prop_t *item = prop_create_root(NULL);

  rstr_t *rurl = url ? rstr_alloc(url) : NULL;

  if(url != NULL)
    prop_set(item, "url", PROP_SET_RSTRING, rurl);

  if(data != NULL)
    js_prop_set_from_jsval(cx, prop_create(item, "data"), *data);

  *rval = JSVAL_VOID;

  if(metabind != NULL)
    metadb_bind_url_to_prop(NULL, metabind, item);

  if(type != NULL) {
    prop_set_string(prop_create(item, "type"), type);

    if(metaobj)
      js_prop_from_object(cx, metaobj, prop_create(item, "metadata"));

  } else if(url != NULL) {

    if(backend_resolve_item(url, item)) {
      prop_destroy(item);
      rstr_release(rurl);
      return JS_TRUE;
    }
  }

  prop_set_int(prop_create(item, "enabled"), enabled);

  prop_t *p = prop_ref_inc(item);

  if(prop_set_parent(item, parent)) {
    prop_destroy(item);
    prop_ref_dec(p);
  } else {
    JSObject *robj =
      JS_NewObjectWithGivenProto(cx, &item_class,
				 JSVAL_TO_OBJECT(model->jm_item_proto), NULL);

    *rval =  OBJECT_TO_JSVAL(robj);
    js_item_t *ji = calloc(1, sizeof(js_item_t));
    atomic_add(&model->jm_refcount, 1);
    ji->ji_url = rstr_dup(rurl);
    ji->ji_model = model;
    ji->ji_root =  p;
    TAILQ_INSERT_TAIL(&model->jm_items, ji, ji_link);
    JS_SetPrivate(cx, robj, ji);
    ji->ji_enable_set_property = 1; 

    ji->ji_eventsub = 
      prop_subscribe(PROP_SUB_TRACK_DESTROY,
		     PROP_TAG_CALLBACK, js_item_eventsub, ji,
		     PROP_TAG_ROOT, ji->ji_root,
		     PROP_TAG_COURIER, model->jm_pc,
		     NULL);
    model->jm_subs++;
    ji->ji_this = OBJECT_TO_JSVAL(robj);
    JS_AddNamedRoot(cx, &ji->ji_this, "item_this");
    prop_tag_set(ji->ji_root, model, ji);
  }
  rstr_release(rurl);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_appendItem(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  const char *url;
  const char *type = NULL;
  JSObject *metaobj = NULL;
  js_model_t *model = JS_GetPrivate(cx, obj);
  const char *canonical_url = NULL;
  htsmsg_t *m = NULL;
  JSBool r;

  if(!JS_ConvertArguments(cx, argc, argv, "s/so", &url, &type, &metaobj))
    return JS_FALSE;

  if(!strncmp(url, "videoparams:", strlen("videoparams:"))) {
    m = htsmsg_json_deserialize(url + strlen("videoparams:"));
    if(m != NULL) {
      canonical_url = htsmsg_get_str(m, "canonicalUrl");

      if(canonical_url == NULL) {
	htsmsg_t *sources;
	if((sources = htsmsg_get_list(m, "sources")) == NULL) {
	  htsmsg_field_t *f;
	  HTSMSG_FOREACH(f, sources) {
	    htsmsg_t *src = &f->hmf_msg;
	    canonical_url = htsmsg_get_str(src, "url");
	    if(canonical_url != NULL)
	      break;
	  }
	}
      }
    }
  } else {
    canonical_url = url;
  }
  r = js_appendItem0(cx, model, model->jm_nodes, url, type, metaobj, NULL,
		     rval, 1, canonical_url);
  htsmsg_destroy(m);
  return r;
}


/**
 *
 */
static JSBool 
js_appendPassiveItem(JSContext *cx, JSObject *obj, uintN argc,
		     jsval *argv, jsval *rval)
{
  const char *type;
  jsval data = 0;
  JSObject *metaobj = NULL;
  js_model_t *model = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "s/vo", &type, &data, &metaobj))
    return JS_FALSE;

  return js_appendItem0(cx, model, model->jm_nodes, NULL, type, metaobj, &data,
			rval, 1, NULL);
}


/**
 *
 */
static JSBool 
js_appendAction(JSContext *cx, JSObject *obj, uintN argc,
		jsval *argv, jsval *rval)
{
  const char *type;
  jsval data = 0;
  JSBool enabled;
  JSObject *metaobj = NULL;
  js_model_t *model = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "svb/o",
			  &type, &data, &enabled, &metaobj))
    return JS_FALSE;

  return js_appendItem0(cx, model, model->jm_actions, NULL, type, metaobj,
			&data, rval, enabled, NULL);
}


/**
 *
 */
static void
init_model_props(js_model_t *jm, prop_t *model)
{
  struct prop_nf *pnf;

  jm->jm_nodes   = prop_ref_inc(prop_create(model, "items"));
  jm->jm_actions = prop_ref_inc(prop_create(model, "actions"));
  jm->jm_type    = prop_ref_inc(prop_create(model, "type"));
  jm->jm_error   = prop_ref_inc(prop_create(model, "error"));
  jm->jm_contents= prop_ref_inc(prop_create(model, "contents"));
  jm->jm_entries = prop_ref_inc(prop_create(model, "entries"));
  jm->jm_metadata= prop_ref_inc(prop_create(model, "metadata"));
  jm->jm_options = prop_ref_inc(prop_create(model, "options"));

  pnf = prop_nf_create(prop_create(model, "nodes"),
		       jm->jm_nodes,
		       prop_create(model, "filter"),
		       PROP_NF_AUTODESTROY);

  prop_set_int(prop_create(model, "canFilter"), 1);

  prop_nf_release(pnf);
}


/**
 *
 */
static JSBool 
js_appendModel(JSContext *cx, JSObject *obj, uintN argc,
	       jsval *argv, jsval *rval)
{
  js_model_t *parent = JS_GetPrivate(cx, obj);
  const char *type;
  JSObject *metaobj = NULL;
  prop_t *item, *metadata;
  js_model_t *jm;
  JSObject *robj;

  if(!JS_ConvertArguments(cx, argc, argv, "s/o", &type, &metaobj))
    return JS_FALSE;

  item = prop_create_root(NULL);

  rstr_t *url = backend_prop_make(item, NULL);
 
  metadata = prop_create(item, "metadata");

  if(metaobj)
    js_prop_from_object(cx, metaobj, metadata);

  prop_set_rstring(prop_create(item, "url"), url);
  rstr_release(url);

  jm = js_model_create(cx, JSVAL_VOID);

  init_model_props(jm, item);
  prop_set_string(jm->jm_type, type);

  if(prop_set_parent(item, parent->jm_nodes))
    prop_destroy(item);

  robj = make_model_object(cx, jm, argv+argc);

  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}



/**
 *
 */
static void
js_model_eventsub(void *opaque, prop_event_t event, ...)
{
  js_model_t *jm = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    js_event_destroy_handlers(jm->jm_cx, &jm->jm_event_handlers);
    prop_unsubscribe(jm->jm_eventsub);
    jm->jm_eventsub = NULL;
    jm->jm_subs--;
    break;

  case PROP_EXT_EVENT:
    js_event_dispatch(jm->jm_cx, &jm->jm_event_handlers, va_arg(ap, event_t *),
		      NULL);
    break;
  }
  va_end(ap);
}


/**
 *
 */
static JSBool 
js_page_onEvent(JSContext *cx, JSObject *obj,
		uintN argc, jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  if(jm->jm_eventsink == NULL) {
    JS_ReportError(cx, "onEvent() on non-page object");
    return JS_FALSE;
  }
  
  if(jm->jm_eventsub == NULL) {
    jm->jm_eventsub = 
      prop_subscribe(PROP_SUB_TRACK_DESTROY,
		     PROP_TAG_CALLBACK, js_model_eventsub, jm,
		     PROP_TAG_ROOT, jm->jm_eventsink,
		     PROP_TAG_COURIER, jm->jm_pc,
		     NULL);
    jm->jm_subs++;
  }

  js_event_handler_create(cx, &jm->jm_event_handlers,
			  JS_GetStringBytes(JS_ValueToString(cx, argv[0])),
			  argv[1]);
  
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_page_error(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  prop_set_int(jm->jm_loading, 0);
  prop_set_string(jm->jm_type, "openerror");
  prop_set_string(jm->jm_error, str);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_page_dump(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);

  prop_print_tree(jm->jm_root, 1);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_page_redirect(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  const char *url;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  event_t *e = event_create_str(EVENT_REDIRECT, url);
  prop_send_ext_event(jm->jm_eventsink, e);
  event_release(e);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_page_items(JSContext *cx, JSObject *obj, uintN argc,
	      jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_item_t *ji;

  int pos = 0;
  TAILQ_FOREACH(ji, &jm->jm_items, ji_link)
    pos++;

  JSObject *robj = JS_NewArrayObject(cx, pos, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  pos = 0;
  TAILQ_FOREACH(ji, &jm->jm_items, ji_link)
    JS_SetElement(cx, robj, pos++, &ji->ji_this);

  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_page_wfv(JSContext *cx, JSObject *obj, uintN argc,
	    jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  const char *str;
  jsval value;
  if (!JS_ConvertArguments(cx, argc, argv, "sv", &str, &value))
    return JS_FALSE;

  return js_wait_for_value(cx, jm->jm_root, str, value, rval);
}




/**
 *
 */
static JSFunctionSpec model_functions[] = {
    JS_FS("appendItem",         js_appendItem,        1, 0, 0),
    JS_FS("appendPassiveItem",  js_appendPassiveItem, 1, 0, 0),
    JS_FS("appendAction",       js_appendAction,      3, 0, 0),
    JS_FS("appendModel",        js_appendModel,       2, 0, 1),
    JS_FS_END
};


/**
 *
 */
static JSBool 
js_page_subscribe(JSContext *cx, JSObject *obj, uintN argc, 
		  jsval *argv, jsval *rval)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  return js_subscribe(cx, argc, argv, rval, jm->jm_root, "page",
		      &jm->jm_subscriptions, jm->jm_pc,
		      &jm->jm_subs);
}

/**
 *
 */
static JSFunctionSpec page_functions[] = {
    JS_FS("onEvent",            js_page_onEvent, 2, 0, 0),
    JS_FS("error",              js_page_error,   1, 0, 0),
    JS_FS("redirect",           js_page_redirect,1, 0, 0),
    JS_FS("dump",               js_page_dump,    0, 0, 0),
    JS_FS("waitForValue",       js_page_wfv,     2, 0, 0),
    JS_FS("subscribe",          js_page_subscribe, 2, 0, 0),
    JS_FS("getItems",           js_page_items,   0, 0, 0),
    JS_FS_END
};


/**
 *
 */
static void
model_finalize(JSContext *cx, JSObject *obj)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);
  js_model_release(jm);
}


static JSClass model_class = {
  "model", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, model_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static int
js_model_fill(JSContext *cx, js_model_t *jm)
{
  jsval result;

  JS_CallFunctionValue(cx, NULL, jm->jm_paginator, 0, NULL, &result);

  return JSVAL_IS_BOOLEAN(result) && JSVAL_TO_BOOLEAN(result);
}

/**
 *
 */
static void
js_reorder(JSContext *cx, js_model_t *jm, js_item_t *ji, js_item_t *before)
{
  void *mark;
  jsval *argv, result;

  if(!jm->jm_reorderer)
    return;

  argv = JS_PushArguments(cx, &mark, "vv", ji->ji_this,
			  before ? before->ji_this : JSVAL_NULL);
  
  JS_CallFunctionValue(cx, NULL, jm->jm_reorderer, 2, argv, &result);
  JS_PopArguments(cx, mark);
}


/**
 *
 */
static void
js_model_nodesub(void *opaque, prop_event_t event, ...)
{
  js_model_t *jm = opaque;
  va_list ap;
  prop_t *p1, *p2;
  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    if(jm->jm_paginator)
      JS_RemoveRoot(jm->jm_cx, &jm->jm_paginator);
    if(jm->jm_reorderer)
       JS_RemoveRoot(jm->jm_cx, &jm->jm_reorderer);

    prop_unsubscribe(jm->jm_nodesub);
    jm->jm_nodesub = NULL;
    jm->jm_subs--;
    break;

  case PROP_REQ_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    js_reorder(jm->jm_cx, jm, prop_tag_get(p1, jm), 
	       p2 ? prop_tag_get(p2, jm) : NULL);
    break;

  case PROP_WANT_MORE_CHILDS:
    if(jm->jm_paginator) {
      if(js_model_fill(jm->jm_cx, jm))
	prop_have_more_childs(jm->jm_nodes);
    } else {
      jm->jm_pending_want_more = 1;
    }
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
install_nodesub(js_model_t *jm)
{
  if(jm->jm_nodesub)
    return;

  jm->jm_subs++;
  jm->jm_nodesub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, js_model_nodesub, jm,
		   PROP_TAG_ROOT, jm->jm_nodes,
		   PROP_TAG_COURIER, jm->jm_pc,
		   NULL);
}


/**
 *
 */
static JSBool 
js_setPaginator(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);

  if(!JSVAL_IS_OBJECT(*vp) || 
     !JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(*vp))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  install_nodesub(jm);
  if(!jm->jm_paginator)
    JS_AddNamedRoot(cx, &jm->jm_paginator, "paginator");
  jm->jm_paginator = *vp;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setReorderer(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  js_model_t *jm = JS_GetPrivate(cx, obj);

  if(!JSVAL_IS_OBJECT(*vp) || 
     !JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(*vp))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  install_nodesub(jm);
  if(!jm->jm_reorderer)
    JS_AddNamedRoot(cx, &jm->jm_reorderer, "reorderer");
  jm->jm_reorderer = *vp;
  return JS_TRUE;
}


/**
 *
 */
static JSObject *
make_model_object(JSContext *cx, js_model_t *jm, jsval *root)
{
  JSObject *obj = JS_NewObjectWithGivenProto(cx, &model_class, NULL, NULL);
  *root = OBJECT_TO_JSVAL(obj);

  JS_SetPrivate(cx, obj, jm);
  atomic_add(&jm->jm_refcount, 1);

  if(jm->jm_pc != NULL)
    JS_DefineFunctions(cx, obj, model_functions);

  if(jm->jm_entries != NULL)
    JS_DefineProperty(cx, obj, "entries", JSVAL_VOID,
		      NULL, js_setEntries, JSPROP_PERMANENT);

  if(jm->jm_type != NULL)
    JS_DefineProperty(cx, obj, "type", JSVAL_VOID,
		      NULL, js_setType, JSPROP_PERMANENT);

  if(jm->jm_contents != NULL)
    JS_DefineProperty(cx, obj, "contents", JSVAL_VOID,
		      NULL, js_setContents, JSPROP_PERMANENT);

  if(jm->jm_loading != NULL)
    JS_DefineProperty(cx, obj, "loading", BOOLEAN_TO_JSVAL(1),
		      NULL, js_setLoading, JSPROP_PERMANENT);

  if(jm->jm_source != NULL)
    JS_DefineProperty(cx, obj, "source", JSVAL_VOID,
		      NULL, js_setSource, JSPROP_PERMANENT);

  if(jm->jm_metadata != NULL) {
    JSObject *metaobj = js_object_from_prop(cx, jm->jm_metadata);
    JS_DefineProperty(cx, obj, "metadata", OBJECT_TO_JSVAL(metaobj),
		      NULL, NULL, JSPROP_PERMANENT);
  }

  if(jm->jm_pc != NULL) {
    JS_DefineProperty(cx, obj, "paginator", JSVAL_VOID,
		      NULL, js_setPaginator, JSPROP_PERMANENT);

    JS_DefineProperty(cx, obj, "reorderer", JSVAL_VOID,
		      NULL, js_setReorderer, JSPROP_PERMANENT);
  }
  return obj;
}


/**
 *
 */
static void
js_open_invoke(JSContext *cx, js_model_t *jm)
{
  jsval *argv, result;
  void *mark;
  char argfmt[10];
  int i = 0, argc;
  jsval pageobj = JSVAL_NULL;

  JS_AddRoot(cx, &pageobj);

  JSObject *obj = make_model_object(cx, jm, &pageobj);

  if(jm->jm_pc != NULL)
    JS_DefineFunctions(cx, obj, page_functions);

  if(jm->jm_url != NULL)
    js_createPageOptions(cx, obj, jm->jm_url, jm->jm_options);

  if(jm->jm_args != NULL) {
    argfmt[0] = 'o';
    while(i < 8 && jm->jm_args[i]) {
      argfmt[i+1] = 's';
      i++;
    }
    argfmt[i+1] = 0;
    argc = i+1;
    argv = JS_PushArguments(cx, &mark, argfmt, obj,
			    i > 0 ? jm->jm_args[0] : "",
			    i > 1 ? jm->jm_args[1] : "",
			    i > 2 ? jm->jm_args[2] : "",
			    i > 3 ? jm->jm_args[3] : "",
			    i > 4 ? jm->jm_args[4] : "",
			    i > 5 ? jm->jm_args[5] : "",
			    i > 6 ? jm->jm_args[6] : "",
			    i > 7 ? jm->jm_args[7] : "");

  } else {
    argv = JS_PushArguments(cx, &mark, "o", obj);
    argc = 2;
  }
  if(argv != NULL) {
    JS_CallFunctionValue(cx, NULL, jm->jm_openfunc, argc, argv, &result);
    JS_PopArguments(cx, mark);
  }
  JS_RemoveRoot(cx, &pageobj);
}



/**
 *
 */
static void
js_open_error(JSContext *cx, const char *msg, JSErrorReport *r)
{
  js_model_t *jm = JS_GetContextPrivate(cx);

  int level;

  if(r->flags & JSREPORT_WARNING)
    level = TRACE_INFO;
  else {
    level = TRACE_ERROR;
    if(jm->jm_root != NULL)
      nav_open_error(jm->jm_root, msg);
  }

  TRACE(level, "JS", "%s:%u %s",  r->filename, r->lineno, msg);
}


/**
 *
 */
static void
js_open(js_model_t *jm)
{
  JSContext *cx = js_newctx(js_open_error);
  JS_SetContextPrivate(cx, jm);

  JS_BeginRequest(cx);

  jm->jm_item_proto = OBJECT_TO_JSVAL(JS_NewObject(cx, NULL, NULL, NULL));
  JS_AddNamedRoot(cx, &jm->jm_item_proto, "itemproto");
  JS_DefineFunctions(cx, JSVAL_TO_OBJECT(jm->jm_item_proto),
		     item_proto_functions);

  js_open_invoke(cx, jm);
  jm->jm_openfunc = JSVAL_VOID;
  JS_RemoveRoot(cx, &jm->jm_openfunc);

  jm->jm_cx = cx;

  while(jm->jm_subs) {
    struct prop_notify_queue exp, nor;
    jsrefcount s = JS_SuspendRequest(cx);
    prop_courier_wait(jm->jm_pc, &nor, &exp, 0);
    JS_ResumeRequest(cx, s);
    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);

    if(jm->jm_pending_want_more && jm->jm_paginator) {
      jm->jm_pending_want_more = 0;
      if(js_model_fill(jm->jm_cx, jm))
	prop_have_more_childs(jm->jm_nodes);
    }
  }

  JS_RemoveRoot(cx, &jm->jm_item_proto);

  js_model_release(jm);

  JS_DestroyContext(cx);
}


/**
 *
 */
static void *
js_open_trampoline(void *arg)
{
  js_model_t *jm = arg;
  js_open(jm);
  return NULL;
}


/**
 *
 */
static void
model_launch(js_model_t *jm)
{
  jm->jm_pc = prop_courier_create_waitable();
  prop_set_int(jm->jm_loading, 1);
  hts_thread_create_detached("jsmodel", js_open_trampoline, jm,
			     THREAD_PRIO_NORMAL);
}

/**
 *
 */
int
js_backend_open(prop_t *page, const char *url, int sync)
{
  js_route_t *jsr;
  hts_regmatch_t matches[8];
  int i;
  js_model_t *jm;
  prop_t *model;

  hts_mutex_lock(&js_page_mutex);

  LIST_FOREACH(jsr, &js_routes, jsr_global_link)
    if(jsr->jsr_jsp->jsp_enable_uri_routing &&
       !hts_regexec(&jsr->jsr_regex, url, 8, matches, 0))
      break;

  if(jsr == NULL) {
    hts_mutex_unlock(&js_page_mutex);
    return 1;
  }

  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);
  jm = js_model_create(cx, jsr->jsr_openfunc);
  JS_EndRequest(cx);
  JS_DestroyContext(cx);

  for(i = 1; i < 8; i++)
    if(matches[i].rm_so != -1)
      strvec_addpn(&jm->jm_args, url + matches[i].rm_so, 
		   matches[i].rm_eo - matches[i].rm_so);
  
  model = prop_create(page, "model");

  init_model_props(jm, model);

  jm->jm_source    = prop_ref_inc(prop_create(page, "source"));
  jm->jm_eventsink = prop_ref_inc(prop_create(page, "eventSink"));
  jm->jm_loading   = prop_ref_inc(prop_create(model, "loading"));
  jm->jm_root      = prop_ref_inc(page);
  jm->jm_url       = strdup(url);

  hts_mutex_unlock(&js_page_mutex);
  if(sync) {
    js_open(jm);
  } else {
    model_launch(jm);
  }
  return 0;
}


/**
 *
 */
void
js_backend_search(struct prop *model, const char *query)
{
  js_searcher_t *jss;
  prop_t *parent = prop_create(model, "nodes");
  js_model_t *jm;

  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  hts_mutex_lock(&js_page_mutex);

  LIST_FOREACH(jss, &js_searchers, jss_global_link) {
    if(!jss->jss_jsp->jsp_enable_search)
      continue;

    jm = js_model_create(cx, jss->jss_openfunc);
    strvec_addp(&jm->jm_args, query);

    search_class_create(parent, &jm->jm_nodes, &jm->jm_entries, 
			jss->jss_title, jss->jss_icon);

    model_launch(jm);
  }
  hts_mutex_unlock(&js_page_mutex);
  JS_EndRequest(cx);
  JS_DestroyContext(cx);
}


/**
 *
 */
static int
jsr_cmp(const js_route_t *a, const js_route_t *b)
{
  return b->jsr_prio - a->jsr_prio;
}


/**
 *
 */
JSBool 
js_addURI(JSContext *cx, JSObject *obj, uintN argc, 
	  jsval *argv, jsval *rval)
{
  const char *str;
  js_route_t *jsr;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  str = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  if(str[0] != '^') {
    int l = strlen(str);
    char *s = alloca(l + 2);
    s[0] = '^';
    memcpy(s+1, str, l+1);
    str = s;
  }

  LIST_FOREACH(jsr, &js_routes, jsr_global_link)
    if(!strcmp(str, jsr->jsr_pattern)) {
      JS_ReportError(cx, "URL already routed");
      return JS_FALSE;
    }
  
  jsr = calloc(1, sizeof(js_route_t));
  jsr->jsr_jsp = jsp;
  if(hts_regcomp(&jsr->jsr_regex, str)) {
    free(jsr);
    JS_ReportError(cx, "Invalid regular expression");
    return JS_FALSE;
  }

  TRACE(TRACE_DEBUG, "JS", "Add route for %s", str);
  
  jsr->jsr_pattern = strdup(str);
  jsr->jsr_prio = strcspn(str, "()[].*?+$") ?: INT32_MAX;


  hts_mutex_lock(&js_page_mutex);
  LIST_INSERT_SORTED(&js_routes, jsr, jsr_global_link, jsr_cmp);
  LIST_INSERT_HEAD(&jsp->jsp_routes, jsr, jsr_plugin_link);
  jsr->jsr_openfunc = argv[1];
  JS_AddNamedRoot(cx, &jsr->jsr_openfunc, "routeduri");
  hts_mutex_unlock(&js_page_mutex);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
JSBool 
js_addSearcher(JSContext *cx, JSObject *obj, uintN argc, 
	       jsval *argv, jsval *rval)
{
  js_searcher_t *jss;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[2]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }
  
  jss = calloc(1, sizeof(js_searcher_t));
  jss->jss_jsp = jsp;

  hts_mutex_lock(&js_page_mutex);
  LIST_INSERT_HEAD(&js_searchers, jss, jss_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_searchers, jss, jss_plugin_link);

  jss->jss_title = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[0])));
  if(JSVAL_IS_STRING(argv[1]))
    jss->jss_icon  = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[1])));

  jss->jss_openfunc = argv[2];
  JS_AddNamedRoot(cx, &jss->jss_openfunc, "searcher");
  hts_mutex_unlock(&js_page_mutex);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static void
js_route_delete(JSContext *cx, js_route_t *jsr)
{
  JS_RemoveRoot(cx, &jsr->jsr_openfunc);

  LIST_REMOVE(jsr, jsr_global_link);
  LIST_REMOVE(jsr, jsr_plugin_link);

  hts_regfree(&jsr->jsr_regex);

  free(jsr->jsr_pattern);
  free(jsr);
}


/**
 *
 */
static void
js_searcher_delete(JSContext *cx, js_searcher_t *jss)
{
  JS_RemoveRoot(cx, &jss->jss_openfunc);

  LIST_REMOVE(jss, jss_global_link);
  LIST_REMOVE(jss, jss_plugin_link);
  free(jss->jss_title);
  free(jss->jss_icon);
  free(jss);
}


/**
 *
 */
void
js_page_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_route_t *jsr;
  js_searcher_t *jss;

  while((jsr = LIST_FIRST(&jsp->jsp_routes)) != NULL)
    js_route_delete(cx, jsr);

  while((jss = LIST_FIRST(&jsp->jsp_searchers)) != NULL)
    js_searcher_delete(cx, jss);
}
