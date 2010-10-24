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
static prop_t *
prop_from_id(JSContext *cx, JSObject *obj, jsval id)
{
  const char *name = name_by_id(id);
  return name ? prop_create(JS_GetPrivate(cx, obj), name) : NULL;
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
  prop_t *c = prop_from_id(cx, obj, id);

  if(c != NULL)
     js_prop_set_from_jsval(cx, c, *vp);

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
  prop_ref_inc(p);
  JS_SetPrivate(cx, obj, p);
  return obj;
}
