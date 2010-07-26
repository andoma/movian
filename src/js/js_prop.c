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
static jsval
prop_to_val(JSContext *cx, prop_t *p)
{
  jsval r;
  JSObject *jo;
  JSString *js;
  jsdouble *jd;

  hts_mutex_lock(&prop_mutex);

  switch(p->hp_type) {
  default:
    r = JSVAL_VOID;
    break;

  case PROP_DIR:
    jo = js_object_from_prop(cx, p);
    r = OBJECT_TO_JSVAL(jo);
    break;

  case PROP_STRING:
    js = JS_NewStringCopyZ(cx, rstr_get(p->hp_rstring));
    r = STRING_TO_JSVAL(js);
    break;

  case PROP_FLOAT:
    jd = JS_NewDouble(cx, p->hp_float);
    r = DOUBLE_TO_JSVAL(jd);
    break;

  case PROP_INT:
    if(INT_FITS_IN_JSVAL(p->hp_int)) {
      r = INT_TO_JSVAL(p->hp_int);
    } else {
      jd = JS_NewDouble(cx, p->hp_int);
      r = DOUBLE_TO_JSVAL(jd);
    }
    break;
  }

  hts_mutex_unlock(&prop_mutex);
  return r;
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
pb_getProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  const char *name = name_by_id(id);

  prop_t *c = name ? prop_get_by_names(JS_GetPrivate(cx, obj), name, NULL) : NULL;
  
  printf("get prop %s = %p\n", name, c);

  if(c == NULL) {
    *vp = JSVAL_VOID;
  } else {
    *vp = prop_to_val(cx, c);
    prop_ref_dec(c);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
pb_setProperty(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  prop_t *c = prop_from_id(cx, obj, id);

  printf("Set prop %s to %lx\n", name_by_id(id), *vp);
  
  if(JSVAL_IS_INT(*vp)) {
    prop_set_int(c, JSVAL_TO_INT(*vp));
  } else if(JSVAL_IS_VOID(*vp)) {
    prop_set_void(c);
  } else if(JSVAL_IS_STRING(*vp)) {
    prop_set_string(c, JS_GetStringBytes(JSVAL_TO_STRING(*vp)));
  } else {
    printf("Unable to set prop 0x%lx\n", *vp);
  }

  hts_mutex_unlock(&prop_mutex);

  return JS_TRUE;
}


typedef struct enumstate {
  prop_t **vec;
  int pos;
  int num;
} enumstate_t;


/**
 *
 */
static JSBool
pb_enumerate(JSContext *cx, JSObject *obj, JSIterateOp enum_op,
	     jsval *statep, jsid *idp)
{
  enumstate_t *es;

  printf("enumerate %d\n", enum_op);

  switch(enum_op) {
  case JSENUMERATE_INIT:
    
    es = malloc(sizeof(enumstate_t));
    es->vec = prop_get_childs(JS_GetPrivate(cx, obj), &es->num);

    if(es->vec == NULL) {
      free(es);
      *statep = JSVAL_NULL;
    } else {
      printf("%d entries idp = %p\n", es->num, idp);

      if(idp != NULL)
	*idp = INT_TO_JSVAL(es->num);

      es->pos = 0;
      *statep = PRIVATE_TO_JSVAL(es);
    }
    break;

  case JSENUMERATE_NEXT:
    es = JSVAL_TO_PRIVATE(*statep);

    if(es->pos == es->num) {
      prop_pvec_free(es->vec);
      free(es);
      *statep = JSVAL_NULL;
    } else {
      prop_t *c = es->vec[es->pos++];
      if(c->hp_type != PROP_ZOMBIE) {
	const char *name = c->hp_name;
	printf("enumerating name %s\n", name);
	JSString *js = JS_NewStringCopyZ(cx, name);
	if(!JS_ValueToId(cx, STRING_TO_JSVAL(js), idp))
	  return JS_FALSE;
      } else {
	if(!JS_ValueToId(cx, JSVAL_VOID, idp))
	  return JS_FALSE;
      }
    }
    break;

  case JSENUMERATE_DESTROY:
    es = JSVAL_TO_PRIVATE(*statep);
    prop_pvec_free(es->vec);
    free(es);
    break;
  }
  printf("enumerate %d ok\n", enum_op);
  return JS_TRUE;
}

/**
 *
 */
static JSBool
pb_convert(JSContext *cx, JSObject *obj, JSType type, jsval *vp)
{
  abort();
  return JS_FALSE;
}


/**
 *
 */
static JSBool
pb_resolve(JSContext *cx, JSObject *obj, jsval id,
	   uintN flags, JSObject **objp)
{
  const char *name = name_by_id(id);
  printf("Resolve %s <%x>\n", name, flags);

  *objp = obj;
  return JS_TRUE;
}


/**
 *
 */
static void
pb_finalize(JSContext *cx, JSObject *obj)
{
  prop_t *p = JS_GetPrivate(cx, obj);
  printf("Finalizing object %p\n", p);
  prop_ref_dec(p);
}


/**
 *
 */
static JSClass prop_bridge_class = {
  "PropBridgeClass",
  JSCLASS_HAS_PRIVATE | JSCLASS_NEW_ENUMERATE | JSCLASS_NEW_RESOLVE,
  pb_setProperty, 
  pb_delProperty,
  pb_getProperty,
  pb_setProperty,
  (JSEnumerateOp)pb_enumerate,
  (JSResolveOp)pb_resolve,
  pb_convert,
  pb_finalize,
};



/**
 *
 */
JSObject *
js_object_from_prop(JSContext *cx, prop_t *p)
{
  printf("Constructing\n");
  JSObject *obj = JS_NewObjectWithGivenProto(cx, &prop_bridge_class,
					     NULL, NULL);

  printf("Creating object from %p == %p\n", p, obj);
  //  assert(obj != NULL);
  
  prop_ref_inc(p);
  JS_SetPrivate(cx, obj, p);
  return obj;
}
