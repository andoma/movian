/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "fileaccess/fileaccess.h"
#include "htsmsg/htsbuf.h"
#include "misc/dbl.h"
#include "misc/json.h"
#include "js.h"
#include "blobcache.h"

/**
 *
 */
static void
js_htsmsg_emit_str(JSContext *cx, jsval value, htsmsg_t *msg, const char *f)
{
  JSString *str = JS_ValueToString(cx, value);
  htsmsg_add_str(msg, f, JS_GetStringBytes(str));
}


/**
 *
 */
void
js_htsmsg_emit_jsval(JSContext *cx, jsval value, htsmsg_t *msg,
		     const char *fieldname)
{
  if(JSVAL_IS_BOOLEAN(value)) {
    if(JSVAL_TO_BOOLEAN(value))
      htsmsg_add_u32(msg, fieldname, 1);
    else
      htsmsg_add_u32(msg, fieldname, 0);
  } else if(JSVAL_IS_INT(value)) {
    htsmsg_add_s32(msg, fieldname, JSVAL_TO_INT(value));
  } else if(JSVAL_IS_DOUBLE(value)) {
    double dbl;
    if(JS_ValueToNumber(cx, value, &dbl))
      htsmsg_add_dbl(msg, fieldname, dbl);
  } else if(JSVAL_IS_STRING(value)) {
    js_htsmsg_emit_str(cx, value, msg, fieldname);
  } else if(JSVAL_IS_OBJECT(value)) {
    JSObject *obj = JSVAL_TO_OBJECT(value);
    JSClass *c = JS_GetClass(cx, obj);

    if(!strcmp(c->name, "XML"))   // Treat some classes special
      js_htsmsg_emit_str(cx, value, msg, fieldname);
    else
      htsmsg_add_msg(msg, fieldname, js_htsmsg_from_object(cx, obj));
  }
}


#define OBJTYPE_MAP  1
#define OBJTYPE_LIST 2
/**
 *
 */
htsmsg_t *
js_htsmsg_from_object(JSContext *cx, JSObject *obj)
{
  int objtype = 0;
  JSIdArray *ida;
  int i;
  const char *n;
  htsmsg_t *msg = NULL;

  if((ida = JS_Enumerate(cx, obj)) == NULL)
    return htsmsg_create_map();

  for(i = 0; i < ida->length; i++) {
    jsval name, value;
    char *fieldname = NULL;

    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;

    if(JSVAL_IS_STRING(name)) {
      JSString *str = JSVAL_TO_STRING(name);
      n = JS_GetStringBytes(str);
      if(!JS_GetProperty(cx, obj, n, &value))
	continue;

      if(objtype == 0) {
	assert(msg == NULL);
	msg = htsmsg_create_map();
	objtype = OBJTYPE_MAP;
      } else if(objtype != OBJTYPE_MAP)
	continue;

      fieldname = strdup(n);

    } else if(JSVAL_IS_INT(name)) {
      if(!JS_GetElement(cx, obj, JSVAL_TO_INT(name), &value) ||
	 JSVAL_IS_VOID(value))
	continue;

      if(objtype == 0) {
	assert(msg == NULL);
	msg = htsmsg_create_list();
	objtype = OBJTYPE_LIST;
      } else if(objtype != OBJTYPE_LIST)
	continue;

      fieldname = NULL;
      
    } else {
      continue;
    }

    js_htsmsg_emit_jsval(cx, value, msg, fieldname);
    free(fieldname);
  }
  JS_DestroyIdArray(cx, ida);

  if(msg == NULL) {
    if(!strcmp(JS_GetClass(cx, obj)->name, "Array"))
      return htsmsg_create_list();
    else
      return htsmsg_create_map();
  }
  return msg;
}


/**
 *
 */
static jsval
js_object_from_htsmsg0(JSContext *cx, const htsmsg_t *msg)
{
  JSObject *o;
  htsmsg_field_t *f;

  if(msg->hm_islist) {
    o =  JS_NewArrayObject(cx, 0, NULL);
  } else {
    o = JS_NewObject(cx, NULL, NULL, NULL);
  }

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {
    jsdouble *d;
    JSString *s;
    jsval v;
    switch(f->hmf_type) {
    case HMF_MAP:
      v = js_object_from_htsmsg0(cx, &f->hmf_msg);
      break;
    case HMF_LIST:
      v = js_object_from_htsmsg0(cx, &f->hmf_msg);
      break;
    case HMF_STR:
      if((s = JS_NewStringCopyZ(cx, f->hmf_str)) == NULL)
	continue;
      v = STRING_TO_JSVAL(s);
      break;
    case HMF_S64:
      if(f->hmf_s64 <= INT32_MAX && f->hmf_s64 >= INT32_MIN &&
	 INT_FITS_IN_JSVAL(f->hmf_s64)) 
	v = INT_TO_JSVAL(f->hmf_s64);
      else {
	if((d = JS_NewDouble(cx, f->hmf_s64)) == NULL)
	  continue;
	v = DOUBLE_TO_JSVAL(d);
      }
      break;
    case HMF_DBL:
      if((d = JS_NewDouble(cx, f->hmf_dbl)) == NULL)
	continue;
      v = DOUBLE_TO_JSVAL(d);
      break;
    default:
      continue;
    }
    js_set_prop_jsval(cx, o, msg->hm_islist ? NULL : f->hmf_name, v);
  }
  return OBJECT_TO_JSVAL(o);
}



/**
 *
 */
JSBool
js_object_from_htsmsg(JSContext *cx, const htsmsg_t *msg, jsval *rval)
{
  if(!JS_EnterLocalRootScope(cx))
    return JS_FALSE;
  
  *rval = js_object_from_htsmsg0(cx, msg);

  JS_LeaveLocalRootScope(cx);
  return JS_TRUE;
}
