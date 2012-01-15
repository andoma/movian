/*
 *  JSAPI <-> Navigator page object
 *  Copyright (C) 2010 Andreas Öman
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
#include "misc/string.h"
#include "misc/regex.h"
#include "prop/prop_nodefilter.h"
#include "event.h"
#include "metadata/metadata.h"
#include "htsmsg/htsmsg_json.h"

LIST_HEAD(js_event_handler_list, js_event_handler);
LIST_HEAD(js_item_list, js_item);

static struct js_route_list js_routes;
static struct js_searcher_list js_searchers;

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
typedef struct js_event_handler {
  LIST_ENTRY(js_event_handler) jeh_link;
  char *jeh_action;
  jsval jeh_function;
} js_event_handler_t;


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

  prop_t *jm_eventsink;

  prop_courier_t *jm_pc;
  prop_sub_t *jm_nodesub;

  prop_sub_t *jm_eventsub;

  jsval jm_paginator;
  
  JSContext *jm_cx;

  struct js_event_handler_list jm_event_handlers;

  struct js_item_list jm_items;

  int jm_subs;

} js_model_t;


static JSObject *make_model_object(JSContext *cx, js_model_t *jm,
				   jsval *root);

/**
 *
 */
static void
destroy_event_handlers(JSContext *cx, struct js_event_handler_list *list)
{
  js_event_handler_t *jeh;

  while((jeh = LIST_FIRST(list)) != NULL) {
    LIST_REMOVE(jeh, jeh_link);
    JS_RemoveRoot(cx, &jeh->jeh_function);
    free(jeh->jeh_action);
    free(jeh);
  }
}



/**
 *
 */
static void
js_event_dispatch_action(JSContext *cx, struct js_event_handler_list *list,
			 const char *action, JSObject *this)
{
  js_event_handler_t *jeh;
  jsval result;
  if(action == NULL)
    return;

  LIST_FOREACH(jeh, list, jeh_link) {
    if(!strcmp(jeh->jeh_action, action)) {
      JS_CallFunctionValue(cx, this, jeh->jeh_function, 0, NULL, &result);
      return;
    }
  }
}


/**
 *
 */
static void
js_event_dispatch(JSContext *cx, struct js_event_handler_list *list,
		  event_t *e, JSObject *this)
{
  if(event_is_type(e, EVENT_ACTION_VECTOR)) {
  event_action_vector_t *eav = (event_action_vector_t *)e;
  int i;
  for(i = 0; i < eav->num; i++)
    js_event_dispatch_action(cx, list, action_code2str(eav->actions[i]), this);
    
  } else if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
    js_event_dispatch_action(cx, list, e->e_payload, this);
  }
}


/**
 *
 */
static js_model_t *
js_model_create(jsval openfunc)
{
  js_model_t *jm = calloc(1, sizeof(js_model_t));
  jm->jm_refcount = 1;
  jm->jm_openfunc = openfunc;
  return jm;
}


/**
 *
 */
static void
js_model_destroy(js_model_t *jm)
{
  if(jm->jm_args)
    strvec_free(jm->jm_args);


  if(jm->jm_eventsub != NULL)
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
  if(jm->jm_eventsink) prop_ref_dec(jm->jm_eventsink);

  if(jm->jm_pc != NULL)
    prop_courier_destroy(jm->jm_pc);

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
  LIST_ENTRY(js_item) ji_link;
  prop_t *ji_root;
  struct js_event_handler_list ji_event_handlers;
  prop_sub_t *ji_eventsub;
  jsval ji_this;
  int ji_enable_set_property;
} js_item_t;


/**
 *
 */
static void
item_finalize(JSContext *cx, JSObject *obj)
{
  js_item_t *ji = JS_GetPrivate(cx, obj);
  assert(LIST_FIRST(&ji->ji_event_handlers) == NULL);
  LIST_REMOVE(ji, ji_link);
  prop_ref_dec(ji->ji_root);
  free(ji);
}


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


/**
 *
 */
static JSClass item_class = {
  "item", JSCLASS_HAS_PRIVATE,
  item_set_property,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
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
    destroy_event_handlers(ji->ji_model->jm_cx, &ji->ji_event_handlers);
    prop_unsubscribe(ji->ji_eventsub);
    ji->ji_eventsub = NULL;
    ji->ji_model->jm_subs--;
    JS_RemoveRoot(ji->ji_model->jm_cx, &ji->ji_this);
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
  js_event_handler_t *jeh;

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  if(ji->ji_eventsub == NULL) {
    ji->ji_eventsub = 
      prop_subscribe(PROP_SUB_TRACK_DESTROY,
		     PROP_TAG_CALLBACK, js_item_eventsub, ji,
		     PROP_TAG_ROOT, ji->ji_root,
		     PROP_TAG_COURIER, ji->ji_model->jm_pc,
		     NULL);
    ji->ji_model->jm_subs++;
    ji->ji_this = OBJECT_TO_JSVAL(obj);
    JS_AddNamedRoot(cx, &ji->ji_this, "item_this");
  }

  jeh = malloc(sizeof(js_event_handler_t));
  jeh->jeh_action = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[0])));
  jeh->jeh_function = argv[1];
  JS_AddNamedRoot(cx, &jeh->jeh_function, "eventhandler");
  LIST_INSERT_HEAD(&ji->ji_event_handlers, jeh, jeh_link);

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
static JSFunctionSpec item_functions[] = {
  JS_FS("onEvent",            js_item_onEvent,      2, 0, 0),
  JS_FS("destroy",            js_item_destroy,      0, 0, 0),
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
  prop_t *item = prop_create_root(NULL);

  if(url != NULL)
    prop_set_string(prop_create(item, "url"), url);

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
      return JS_TRUE;
    }
  }

  prop_set_int(prop_create(item, "enabled"), enabled);

  prop_t *p = prop_ref_inc(item);

  if(prop_set_parent(item, parent)) {
    prop_destroy(item);
    prop_ref_dec(p);
  } else {
    JSObject *robj = JS_NewObjectWithGivenProto(cx, &item_class, NULL, NULL);
    *rval =  OBJECT_TO_JSVAL(robj);
    js_item_t *ji = calloc(1, sizeof(js_item_t));
    ji->ji_model = model;
    ji->ji_root =  p;
    LIST_INSERT_HEAD(&model->jm_items, ji, ji_link);
    JS_SetPrivate(cx, robj, ji);
    ji->ji_enable_set_property = 1; 
    JS_DefineFunctions(cx, robj, item_functions);
  }
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

  pnf = prop_nf_create(prop_create(model, "nodes"),
		       jm->jm_nodes,
		       prop_create(model, "filter"),
		       NULL, PROP_NF_AUTODESTROY);

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
  char url[URL_MAX];
  prop_t *item, *metadata;
  js_model_t *jm;
  JSObject *robj;

  if(!JS_ConvertArguments(cx, argc, argv, "s/o", &type, &metaobj))
    return JS_FALSE;

  item = prop_create_root(NULL);

  backend_prop_make(item, url, sizeof(url));
 
  metadata = prop_create(item, "metadata");

  if(metaobj)
    js_prop_from_object(cx, metaobj, metadata);

  prop_set_string(prop_create(item, "url"), url);

  jm = js_model_create(JSVAL_VOID);

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
    destroy_event_handlers(jm->jm_cx, &jm->jm_event_handlers);
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
  js_event_handler_t *jeh;

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

  jeh = malloc(sizeof(js_event_handler_t));
  jeh->jeh_action = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[0])));
  jeh->jeh_function = argv[1];
  JS_AddNamedRoot(cx, &jeh->jeh_function, "eventhandler");
  LIST_INSERT_HEAD(&jm->jm_event_handlers, jeh, jeh_link);
  
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
static JSFunctionSpec page_functions[] = {
    JS_FS("onEvent",            js_page_onEvent, 2, 0, 0),
    JS_FS("error",              js_page_error,   1, 0, 0),
    JS_FS("dump",               js_page_dump,    0, 0, 0),
    JS_FS("waitForValue",       js_page_wfv,     2, 0, 0),
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

  if(!jm->jm_paginator)
    return 0;

  JS_CallFunctionValue(cx, NULL, jm->jm_paginator, 0, NULL, &result);

  return JSVAL_IS_BOOLEAN(result) && JSVAL_TO_BOOLEAN(result);
}


/**
 *
 */
static void
js_model_nodesub(void *opaque, prop_event_t event, ...)
{
  js_model_t *jm = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    JS_RemoveRoot(jm->jm_cx, &jm->jm_paginator);
    prop_unsubscribe(jm->jm_nodesub);
    jm->jm_nodesub = NULL;
    jm->jm_subs--;
    break;

  case PROP_WANT_MORE_CHILDS:
    if(js_model_fill(jm->jm_cx, jm))
      prop_have_more_childs(jm->jm_nodes);
    break;
  }
  va_end(ap);
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

  jm->jm_subs++;
  jm->jm_nodesub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, js_model_nodesub, jm,
		   PROP_TAG_ROOT, jm->jm_nodes,
		   PROP_TAG_COURIER, jm->jm_pc,
		   NULL);

  jm->jm_paginator = *vp;
  JS_AddNamedRoot(cx, &jm->jm_paginator, "paginator");
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

  JS_DefineProperty(cx, obj, "paginator", JSVAL_VOID,
		    NULL, js_setPaginator, JSPROP_PERMANENT);
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

  JS_DefineFunctions(cx, obj, page_functions);

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
static void *
js_open_trampoline(void *arg)
{
  js_model_t *jm = arg;
  
  JSContext *cx = js_newctx(js_open_error);
  JS_SetContextPrivate(cx, jm);

  JS_BeginRequest(cx);

  js_open_invoke(cx, jm);

  jm->jm_cx = cx;

  while(jm->jm_subs) {
    struct prop_notify_queue exp, nor;
    jsrefcount s = JS_SuspendRequest(cx);
    prop_courier_wait(jm->jm_pc, &nor, &exp, 0);
    JS_ResumeRequest(cx, s);
    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);

  }
  js_model_release(jm);

  JS_DestroyContext(cx);
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
js_backend_open(prop_t *page, const char *url)
{
  js_route_t *jsr;
  hts_regmatch_t matches[8];
  int i;
  js_model_t *jm;
  prop_t *model;

  LIST_FOREACH(jsr, &js_routes, jsr_global_link)
    if(jsr->jsr_jsp->jsp_enable_uri_routing &&
       !hts_regexec(&jsr->jsr_regex, url, 8, matches, 0))
      break;

  if(jsr == NULL)
    return 1;
 
  jm = js_model_create(jsr->jsr_openfunc);

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
  
  model_launch(jm);
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

  LIST_FOREACH(jss, &js_searchers, jss_global_link) {
    if(!jss->jss_jsp->jsp_enable_search)
      continue;

    jm = js_model_create(jss->jss_openfunc);
    strvec_addp(&jm->jm_args, query);

    search_class_create(parent, &jm->jm_nodes, &jm->jm_entries, 
			jss->jss_title, jss->jss_icon);

    model_launch(jm);
  }
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
  
  jsr->jsr_pattern = strdup(str);
  jsr->jsr_prio = strcspn(str, "()[].*?+$") ?: INT32_MAX;
  
  LIST_INSERT_SORTED(&js_routes, jsr, jsr_global_link, jsr_cmp);
  LIST_INSERT_HEAD(&jsp->jsp_routes, jsr, jsr_plugin_link);

  TRACE(TRACE_DEBUG, "JS", "Add route for %s", str);

  jsr->jsr_openfunc = argv[1];
  JS_AddNamedRoot(cx, &jsr->jsr_openfunc, "routeduri");

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

  LIST_INSERT_HEAD(&js_searchers, jss, jss_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_searchers, jss, jss_plugin_link);

  jss->jss_title = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[0])));
  if(JSVAL_IS_STRING(argv[1]))
    jss->jss_icon  = strdup(JS_GetStringBytes(JS_ValueToString(cx, argv[1])));

  jss->jss_openfunc = argv[2];
  JS_AddNamedRoot(cx, &jss->jss_openfunc, "searcher");

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
