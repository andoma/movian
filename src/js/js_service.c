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
JSBool 
js_createService(JSContext *cx, JSObject *obj, uintN argc, 
		 jsval *argv, jsval *rval)
{
  const char *title;
  const char *url;
  const char *type;
  const char *icon = NULL;
  JSObject *robj;
  service_t *s;

  if (!JS_ConvertArguments(cx, argc, argv, "sss/s",
			   &title, &url, &type, &icon))
    return JS_FALSE;

  s = service_create(title, url, type, icon, 0);
  robj = JS_NewObjectWithGivenProto(cx, &service_class, NULL, NULL);
  JS_SetPrivate(cx, robj, s);
  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}

