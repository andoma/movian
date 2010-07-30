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
js_setTitle(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  const char *str = JS_GetStringBytes(JS_ValueToString(cx, *vp));

  prop_set_string(prop_create(prop_create(prop_create(p, "model"),
					  "metadata"),
			      "title"),
		  str);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setType(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  const char *str = JS_GetStringBytes(JS_ValueToString(cx, *vp));

  prop_set_string(prop_create(prop_create(p, "model"),
			      "type"),
		  str);

  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_setLoading(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  JSBool on;


  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  prop_set_int(prop_create(prop_create(p, "model"),
			      "loading"), on);
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
  const char *type;
  JSObject *metaobj = NULL;

  prop_t *parent;
  prop_t *item;

  if(!JS_ConvertArguments(cx, argc, argv, "ss/o", &url, &type, &metaobj))
    return JS_FALSE;

  item = prop_create(NULL, NULL);

  if(metaobj != NULL) {
    JSIdArray *ida;
    prop_t *metadata;
    int i;
    if((ida = JS_Enumerate(cx, metaobj)) == NULL) {
      prop_destroy(item);
      return JS_FALSE;
    }

    metadata = prop_create(item, "metadata");

    for(i = 0; i < ida->length; i++) {
      jsval name, value;
      prop_t *val;
      if(!JS_IdToValue(cx, ida->vector[i], &name))
	continue;

      if(!JSVAL_IS_STRING(name))
	continue;

      if(!JS_GetProperty(cx, metaobj, JS_GetStringBytes(JSVAL_TO_STRING(name)),
			 &value))
	continue;

      val = prop_create(metadata, JS_GetStringBytes(JSVAL_TO_STRING(name)));
      if(JSVAL_IS_INT(value)) {
	prop_set_int(val, JSVAL_TO_INT(value));
      } else if(JSVAL_IS_DOUBLE(value)) {
	double d;
	if(JS_ValueToNumber(cx, value, &d))
	  prop_set_float(val, d);
      } else {
	prop_set_string(val, JS_GetStringBytes(JS_ValueToString(cx, value)));
      }
    }
    JS_DestroyIdArray(cx, ida);
  }

  prop_set_string(prop_create(item, "url"), url);
  prop_set_string(prop_create(item, "type"), type);
  parent = JS_GetPrivate(cx, obj);

  parent = prop_create(parent, "model");
  parent = prop_create(parent, "nodes");

  if(prop_set_parent(item, parent))
    prop_destroy(item);

  *rval = JSVAL_VOID;
  return JS_TRUE;
  

}


/**
 *
 */
static JSFunctionSpec page_functions[] = {
    JS_FS("appendItem",         js_appendItem,  3, 0, 0),
    JS_FS_END
};


/**
 *
 */
static JSPropertySpec page_properties[] = {
  { "title",      0, 0,         NULL, js_setTitle },
  { "type",       0, 0,         NULL, js_setType },
  { "loading",    0, 0,         NULL, js_setLoading },
  { NULL },
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
  JS_DefineProperties(cx, obj, page_properties);

  return obj;
}

