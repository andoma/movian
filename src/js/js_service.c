/*
 *  JSAPI <-> Service objects
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

#include "service.h"

typedef struct js_service {
  service_t *jss_s;
  LIST_ENTRY(js_service) jss_link;
  int jss_ref;
} js_service_t;


/**
 *
 */
static void 
js_service_release(js_service_t *jss)
{
  jss->jss_ref--;
  if(jss->jss_ref == 0)
    free(jss);
}


/**
 *
 */
static void
js_service_destroy(js_service_t *jss)
{
  service_destroy(jss->jss_s);
  LIST_REMOVE(jss, jss_link);
  js_service_release(jss);
}


/**
 *
 */
static void
service_finalize(JSContext *cx, JSObject *obj)
{
  js_service_release(JS_GetPrivate(cx, obj));
}


/**
 *
 */


static JSClass service_class = {
  "service", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, service_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};

/**
 *
 */
static JSBool 
setEnabled(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;
  
  js_service_t *jss = JS_GetPrivate(cx, obj);
  service_set_enabled(jss->jss_s, on);
  return JS_TRUE;
}

static JSBool 
destroy(JSContext *cx, JSObject *obj,
	uintN argc, jsval *argv, jsval *rval)
{
  js_service_destroy(JS_GetPrivate(cx, obj));
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
JSBool 
js_createService(JSContext *cx, JSObject *obj, uintN argc, 
		 jsval *argv, jsval *rval)
{
  const char *title;
  const char *url;
  const char *type;
  const char *icon = NULL;
  JSObject *robj;
  JSBool enabled;

  if (!JS_ConvertArguments(cx, argc, argv, "sssb/s",
			   &title, &url, &type, &enabled, &icon))
    return JS_FALSE;

  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  js_service_t *jss = malloc(sizeof(js_service_t));
  jss->jss_ref = 2;
  jss->jss_s = service_create(title, url, type, icon, 0, enabled);
  LIST_INSERT_HEAD(&jsp->jsp_services, jss, jss_link);

  robj = JS_NewObjectWithGivenProto(cx, &service_class, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  JS_SetPrivate(cx, robj, jss);

  JS_DefineProperty(cx, robj, "enabled", BOOLEAN_TO_JSVAL(enabled),
		    NULL, setEnabled, JSPROP_PERMANENT);

  JS_DefineFunction(cx, robj, "destroy", destroy, 0, 0);
  return JS_TRUE;
}


/**
 *
 */
void
js_service_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_service_t *jss;
  while((jss = LIST_FIRST(&jsp->jsp_services)) != NULL)
    js_service_destroy(jss);
}
