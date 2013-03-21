/*
 *  JSAPI <-> Showtime Prop bridge
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

#include <string.h>
#include "js.h"

#include "prop/prop_i.h"

/**
 *
 */
static const char *
name_by_id(jsval id)
{
  return JSVAL_IS_STRING(id) ? JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;
}


/**
 *
 */
static JSBool
pb_delProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  const char *name = name_by_id(id);

  if(name)
    prop_destroy_by_name(JS_GetPrivate(cx, obj), name);

  return JS_TRUE;
}


/**
 *
 */
static JSBool
pb_setProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  const char *name = name_by_id(id);
  if(name != NULL)
    js_prop_set_from_jsval(cx, prop_create(JS_GetPrivate(cx, obj), name), *vp);
  return JS_TRUE;
}



/**
 *
 */
static void
pb_finalize(JSContext *cx, JSObject *obj)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  prop_ref_dec(p);
}


/**
 *
 */
static JSClass prop_bridge_class = {
  "PropBridgeClass",
  JSCLASS_HAS_PRIVATE,
  pb_setProperty, 
  pb_delProperty,
  JS_PropertyStub,
  pb_setProperty,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,
  pb_finalize,
};



/**
 *
 */
JSObject *
js_object_from_prop(JSContext *cx, prop_t *p)
{
  JSObject *obj = JS_NewObjectWithGivenProto(cx, &prop_bridge_class,
					     NULL, NULL);
  JS_SetPrivate(cx, obj, prop_ref_inc(p));
  return obj;
}

typedef struct {
  jsval value;
  int done;
} wfv_t;


/**
 *
 */
static void
vfw_setval(void *opaque, prop_event_t event, ...)
{
  wfv_t *wfv = opaque;
  va_list ap;
  rstr_t *r;
  const char *s;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    if(JSVAL_IS_NULL(wfv->value) || JSVAL_IS_VOID(wfv->value))
      break;
    return;

  case PROP_SET_RSTRING:
    if(!JSVAL_IS_STRING(wfv->value))
      return;

    r = va_arg(ap, rstr_t *);
    if(strcmp(JS_GetStringBytes(JSVAL_TO_STRING(wfv->value)), rstr_get(r)))
      return;
    break;

  case PROP_SET_CSTRING:
    if(!JSVAL_IS_STRING(wfv->value))
      return;

    s = va_arg(ap, const char *);
    if(strcmp(JS_GetStringBytes(JSVAL_TO_STRING(wfv->value)), s))
      return;
    break;

  default:
    return;
  }
  wfv->done = 1;
}


/**
 *
 */
JSBool
js_wait_for_value(JSContext *cx, prop_t *root, const char *subname,
		  jsval value, jsval *rval)
{
  prop_courier_t *pc = prop_courier_create_waitable();
  prop_sub_t *s;
  wfv_t wfv;
  wfv.value = value;
  wfv.done = 0;

  s = prop_subscribe(0,
		     PROP_TAG_ROOT, root,
		     PROP_TAG_COURIER, pc,
		     PROP_TAG_CALLBACK, vfw_setval, &wfv,
		     PROP_TAG_NAMESTR, subname,
		     NULL);

  if(s == NULL) {
    JS_ReportError(cx, "Unable to subscribe to %s", subname);
    return JS_FALSE;
  }
  *rval = JSVAL_TRUE;

  while(!wfv.done) {

    struct prop_notify_queue q;
    jsrefcount s = JS_SuspendRequest(cx);
    prop_courier_wait(pc, &q, 0);
    JS_ResumeRequest(cx, s);
    prop_notify_dispatch(&q);
  }

  prop_unsubscribe(s);
  prop_courier_destroy(pc);
  return JS_TRUE;
}


/**
 *
 */
typedef struct js_subscription {
  LIST_ENTRY(js_subscription) jss_link;
  prop_sub_t *jss_sub;
  jsval jss_fn;
  int jss_refcount;
  int *jss_subcnt;
  JSContext *jss_cx;
} js_subscription_t;



/**
 *
 */
static void
jss_release(js_subscription_t *jss)
{
  if(atomic_add(&jss->jss_refcount, -1) > 1)
    return;
  free(jss);
}

/**
 *
 */
static void
js_sub_destroy(JSContext *cx, js_subscription_t *jss)
{
  if(jss->jss_sub != NULL) {
    jss->jss_fn = JSVAL_VOID;
    JS_RemoveRoot(cx, &jss->jss_fn);
    LIST_REMOVE(jss, jss_link);
    prop_unsubscribe(jss->jss_sub);
    jss->jss_sub = NULL;
  }
  jss_release(jss);
}


/**
 *
 */
static void
js_sub_cb(void *opaque, prop_event_t event, ...)
{
  va_list ap;
  js_subscription_t *jss = opaque;
  JSContext *cx = jss->jss_cx;

  va_start(ap, event);
  jsval v = JSVAL_NULL;
  int i32;
  const char *str;

  switch(event) {
  case PROP_SET_INT:
    i32 = va_arg(ap, int);
    v = INT_TO_JSVAL(i32);
    break;
  case PROP_SET_FLOAT:
    v = DOUBLE_TO_JSVAL(JS_NewDouble(cx, va_arg(ap, double)));
    break;
  case PROP_SET_RSTRING:
    str = rstr_get(va_arg(ap, rstr_t *));
    v = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str));
    break;
  case PROP_SET_CSTRING:
    str = va_arg(ap, const char *);
    v = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str));
    break;

  case PROP_DESTROYED:
    if(jss->jss_subcnt != NULL) {
      (*jss->jss_subcnt)--;
      js_sub_destroy(cx, jss);
    }
    return;
  default:
    break;
  }

  va_end(ap);

  if(JSVAL_IS_OBJECT(jss->jss_fn)) {
    void *mark;
    jsval *argv, result;

    argv = JS_PushArguments(cx, &mark, "v", v);
    JS_CallFunctionValue(cx, NULL, jss->jss_fn, 1, argv, &result);
    JS_PopArguments(cx, mark);
  }

}


/**
 *
 */
static JSBool 
jss_destroy(JSContext *cx, JSObject *obj, uintN argc, 
	    jsval *argv, jsval *rval)
{
  js_subscription_t *jss = JS_GetPrivate(cx, obj);
  js_sub_destroy(cx, jss);
  return JS_TRUE;
}


/**
 *
 */
static void
jss_finalize(JSContext *cx, JSObject *obj)
{
  js_subscription_t *jss = JS_GetPrivate(cx, obj);
  jss_release(jss);
}


/**
 *
 */
static JSFunctionSpec subscription_functions[] = {
    JS_FS("destroy", jss_destroy, 0, 0, 0),
    JS_FS_END
};


static JSClass page_options_class = {
  "subscription", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub, JS_PropertyStub,
  JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, jss_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
JSBool 
js_subscribe(JSContext *cx, uintN argc, 
	     jsval *argv, jsval *rval, prop_t *root, const char *pname,
	     struct js_subscription_list *list, prop_courier_t *pc,
	     int *subsptr)
{
  const char *name;
  JSObject *func;

  if(!JS_ConvertArguments(cx, argc, argv, "so", &name, &func))
    return JS_FALSE;

  js_subscription_t *jss = calloc(1, sizeof(js_subscription_t));
  jss->jss_refcount = 2;
  jss->jss_fn = OBJECT_TO_JSVAL(func);
  JS_AddNamedRoot(cx, &jss->jss_fn, "subscription");

  // Abuse subsptr to tell if we're a page or global sub
  jss->jss_cx = subsptr ? cx : js_global_cx;
  jss->jss_subcnt = subsptr;
  if(subsptr)
    (*subsptr)++;

  jss->jss_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
		   PROP_TAG_CALLBACK, js_sub_cb, jss,
		   PROP_TAG_NAMESTR, name,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, root, pname,
		   NULL);

  LIST_INSERT_HEAD(list, jss, jss_link);

  JSObject *robj = JS_NewObject(cx, &page_options_class, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  JS_SetPrivate(cx, robj, jss);

  JS_DefineFunctions(cx, robj, subscription_functions);
 
  return JS_TRUE;
}


/**
 *
 */
JSBool 
js_subscribe_global(JSContext *cx, JSObject *obj, uintN argc, 
		    jsval *argv, jsval *rval)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  return js_subscribe(cx, argc, argv, rval, NULL, NULL,
		      &jsp->jsp_subscriptions, js_global_pc, NULL);
}


/**
 *
 */
void
js_subscription_flush_from_list(JSContext *cx, struct js_subscription_list *l)
{
  js_subscription_t *jss;
  while((jss = LIST_FIRST(l)) != NULL)
    js_sub_destroy(cx, jss);
}
