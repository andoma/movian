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


/**
 *
 */
static void
service_finalize(JSContext *cx, JSObject *obj)
{
  service_destroy(JS_GetPrivate(cx, obj));
}


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

  service_set_enabled(JS_GetPrivate(cx, obj), on);
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
  service_t *s;

  if (!JS_ConvertArguments(cx, argc, argv, "sssb/s",
			   &title, &url, &type, &enabled, &icon))
    return JS_FALSE;

  s = service_create(title, url, type, icon, 0, !!enabled);
  robj = JS_NewObjectWithGivenProto(cx, &service_class, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  JS_SetPrivate(cx, robj, s);

  JS_DefineProperty(cx, robj, "enabled", BOOLEAN_TO_JSVAL(enabled),
		    NULL, setEnabled, JSPROP_PERMANENT);

  return JS_TRUE;
}

