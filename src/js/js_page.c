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

#include <string.h>
#include "js.h"


/**
 *
 */
static JSBool 
js_setTitle(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  prop_t *p;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  p = JS_GetPrivate(cx, obj);

  prop_set_string(prop_create(prop_create(prop_create(p, "model"),
					  "metadata"),
			      "title"),
		  str);

  *rval = JSVAL_VOID;  /* return undefined */
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setType(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  prop_t *p;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  p = JS_GetPrivate(cx, obj);

  prop_set_string(prop_create(prop_create(p, "model"),
			      "type"),
		  str);
  
  *rval = JSVAL_VOID;  /* return undefined */
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec page_functions[] = {
    JS_FS("setTitle",           js_setTitle,    1, 0, 0),
    JS_FS("setType",            js_setType,     1, 0, 0),
    JS_FS_END
};

/**
 *
 */
static void
finalize(JSContext *cx, JSObject *obj)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  prop_ref_dec(p);
}


static JSClass page_class = {
  "page", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};





JSObject *
js_page_object(JSContext *cx, prop_t *p)
{
  JSObject *obj = JS_NewObjectWithGivenProto(cx, &page_class,
					     NULL, NULL);

  prop_ref_inc(p);
  JS_SetPrivate(cx, obj, p);

  JS_DefineFunctions(cx, obj, page_functions);

  return obj;
}

