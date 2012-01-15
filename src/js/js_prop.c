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

    struct prop_notify_queue exp, nor;
    jsrefcount s = JS_SuspendRequest(cx);
    prop_courier_wait(pc, &nor, &exp, 0);
    JS_ResumeRequest(cx, s);
    prop_notify_dispatch(&exp);
    prop_notify_dispatch(&nor);
  }

  prop_unsubscribe(s);
  prop_courier_destroy(pc);
  return JS_TRUE;
}
