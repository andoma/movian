/*
 *  Fast JSPAI JSON encoder / decoder
 *  Copyright (C) 2011 Andreas Öman
 *  Copyright (C) 2012 Henrik Andersson
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
#include <limits.h>

#include "fileaccess/fileaccess.h"
#include "htsmsg/htsbuf.h"
#include "misc/dbl.h"
#include "misc/json.h"
#include "js.h"
#include "blobcache.h"

static int json_encode_from_object(JSContext *cx, JSObject *obj,
				   htsbuf_queue_t *out);
/**
 *
 */
static void
js_json_emit_str(JSContext *cx, jsval value, htsbuf_queue_t *out)
{
  JSString *str = JS_ValueToString(cx, value);
  htsbuf_append_and_escape_jsonstr(out, JS_GetStringBytes(str));
}


/**
 *
 */
static void
js_json_emit_jsval(JSContext *cx, jsval value, htsbuf_queue_t *out)
{
  char buf[100];
  if(JSVAL_IS_BOOLEAN(value)) {
    if(JSVAL_TO_BOOLEAN(value))
      htsbuf_append(out, "true", 4);
    else
      htsbuf_append(out, "false", 5);
  } else if(JSVAL_IS_INT(value)) {
    snprintf(buf, sizeof(buf), "%d", JSVAL_TO_INT(value));
    htsbuf_append(out, buf, strlen(buf));
  } else if(JSVAL_IS_DOUBLE(value)) {
    double dbl;
    if(JS_ValueToNumber(cx, value, &dbl) &&
       !my_double2str(buf, sizeof(buf), dbl))
      htsbuf_append(out, buf, strlen(buf));
    else
      htsbuf_append(out, "null", 4);
  } else if(JSVAL_IS_NULL(value)) {
    htsbuf_append(out, "null", 4);
  } else if(JSVAL_IS_STRING(value)) {
    js_json_emit_str(cx, value, out);
  } else if(JSVAL_IS_OBJECT(value)) {
    JSObject *obj = JSVAL_TO_OBJECT(value);
    JSClass *c = JS_GetClass(cx, obj);

    if(!strcmp(c->name, "XML"))   // Treat some classes special
      js_json_emit_str(cx, value, out);
    else {
      if(json_encode_from_object(cx, obj, out))
	htsbuf_append(out, "null", 4);
    }
  }
}


#define OBJTYPE_MAP  1
#define OBJTYPE_LIST 2
/**
 *
 */
static int
json_encode_from_object(JSContext *cx, JSObject *obj, htsbuf_queue_t *out)
{
  int objtype = 0;
  JSIdArray *ida;
  int i;
  const char *n;

  if((ida = JS_Enumerate(cx, obj)) == NULL)
    return -1;
  
  for(i = 0; i < ida->length; i++) {
    jsval name, value;

    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;

    if(JSVAL_IS_STRING(name)) {
      JSString *str = JSVAL_TO_STRING(name);
      n = JS_GetStringBytes(str);
      if(!JS_GetProperty(cx, obj, n, &value))
	continue;

      if(objtype == 0) {
	htsbuf_append(out, "{", 1);
	objtype = OBJTYPE_MAP;
      } else if(objtype != OBJTYPE_MAP)
	continue;
      else
	htsbuf_append(out, ",", 1);
      htsbuf_append_and_escape_jsonstr(out, n);
      htsbuf_append(out, ":", 1);

    } else if(JSVAL_IS_INT(name)) {
      if(!JS_GetElement(cx, obj, JSVAL_TO_INT(name), &value) ||
	 JSVAL_IS_VOID(value))
	continue;

      if(objtype == 0) {
	htsbuf_append(out, "[", 1);
	objtype = OBJTYPE_LIST;
      } else if(objtype != OBJTYPE_LIST)
	continue;
      else
	htsbuf_append(out, ",", 1);
      
    } else {
      continue;
    }

    js_json_emit_jsval(cx, value, out);
  }
  JS_DestroyIdArray(cx, ida);

  switch(objtype) {
  case OBJTYPE_LIST:
    htsbuf_append(out, "]", 1);
    break;
  case OBJTYPE_MAP:
    htsbuf_append(out, "}", 1);
    break;
  default:
    return -1;
  }

  return 0;
}


JSBool 
js_json_encode(JSContext *cx, JSObject *obj,
	       uintN argc, jsval *argv, jsval *rval)
{
  JSObject *o;
  htsbuf_queue_t out;
  char *r;
  size_t len;

  if(!JS_ConvertArguments(cx, argc, argv, "o", &o))
    return JS_FALSE;

  if(o == NULL) {
    JS_ReportError(cx, "Not an object");
    return JS_FALSE;
  }

  htsbuf_queue_init(&out, 0);

  json_encode_from_object(cx, o, &out);

  len = out.hq_size;
  r = malloc(out.hq_size+1);
  r[len] = 0;
  htsbuf_read(&out, r, len);

  *rval = STRING_TO_JSVAL(JS_NewString(cx, r, len));
  return JS_TRUE;
}



/**
 *
 */

static void *
create_map(void *opaque)
{
  return JS_NewObject(opaque, NULL, NULL, NULL);
}

static void *
create_list(void *opaque)
{
  return JS_NewArrayObject(opaque, 0, NULL);
}

static void
destroy_obj(void *opaque, void *obj)
{
  // GC will take care of this
}


static void
add_obj(void *opaque, void *parent, const char *name, void *child)
{
  js_set_prop_jsval(opaque, parent, name, OBJECT_TO_JSVAL(child));
}


static void 
add_string(void *opaque, void *parent, const char *name, char *str)
{
  JSString *s = JS_NewString(opaque, str, strlen(str));
  if(s == NULL)
    free(str);
  else
    js_set_prop_jsval(opaque, parent, name, STRING_TO_JSVAL(s));
}

static void 
add_double(void *opaque, void *parent, const char *name, double v)
{
  jsdouble *d = JS_NewDouble(opaque, v);
  if(d != NULL)
    js_set_prop_jsval(opaque, parent, name, DOUBLE_TO_JSVAL(d));
}

static void 
add_long(void *opaque, void *parent, const char *name, long v)
{
  if(v <= INT32_MAX && v >= INT32_MIN && INT_FITS_IN_JSVAL(v))
    js_set_prop_jsval(opaque, parent, name, INT_TO_JSVAL(v));
  else
    add_double(opaque, parent, name, v);
}


static void 
add_bool(void *opaque, void *parent, const char *name, int v)
{
  js_set_prop_jsval(opaque, parent, name, BOOLEAN_TO_JSVAL(!!v));
}

static void 
add_null(void *opaque, void *parent, const char *name)
{
  js_set_prop_jsval(opaque, parent, name, JSVAL_NULL);
}


/**
 *
 */
static const json_deserializer_t json_to_jsapi = {
  .jd_create_map      = create_map,
  .jd_create_list     = create_list,
  .jd_destroy_obj     = destroy_obj,
  .jd_add_obj         = add_obj,
  .jd_add_string      = add_string,
  .jd_add_long        = add_long,
  .jd_add_double      = add_double,
  .jd_add_bool        = add_bool,
  .jd_add_null        = add_null,
};



JSBool 
js_json_decode(JSContext *cx, JSObject *obj,
	       uintN argc, jsval *argv, jsval *rval)
{
  char *str;
  JSObject *o;
  char errbuf[256];
  if(!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  if(!JS_EnterLocalRootScope(cx))
    return JS_FALSE;

  o = json_deserialize(str, &json_to_jsapi, cx, errbuf, sizeof(errbuf));

  *rval = OBJECT_TO_JSVAL(o);

  JS_LeaveLocalRootScope(cx);

  if(o == NULL) {
    JS_ReportError(cx, "Invalid JSON -- %s", errbuf);
    return JS_FALSE;
  }
  return JS_TRUE;
}

/**
 *
 */
JSBool
js_cache_put(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  char stash[256];
  const char *key,*lstash;
  char *value;
  uint32_t len, maxage;
  JSObject *o;
  htsbuf_queue_t out;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if (!JS_ConvertArguments(cx, argc, argv, "ssou",
			   &lstash, &key, &o, &maxage))
    return JS_FALSE;

  if (o == NULL) {
    JS_ReportError(cx, "Not an object");
    return JS_FALSE;
  }

  // json encode object
  htsbuf_queue_init(&out, 0);
  if (json_encode_from_object(cx, o, &out) != 0) {
    JS_ReportError(cx, "Not an JSON object");
    return JS_FALSE;
  }

  len = out.hq_size;
  value = JS_malloc(cx,len + 1);
  value[len] = '\0';
  htsbuf_read(&out, value, len);

  // put json encoded object onto cache
  snprintf(stash, sizeof(stash), "plugin/%s/%s", jsp->jsp_id, lstash);
  blobcache_put(key, stash, value, len, maxage, NULL, 0);

  return JS_TRUE;
}

/**
 *
 */
JSBool
js_cache_get(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  void *value;
  size_t vsize;
  char stash[256];
  char errbuf[256];
  const char *key,*lstash;
  JSObject *o;

  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if (!JS_ConvertArguments(cx, argc, argv, "ss", &lstash, &key))
    return JS_FALSE;

  // fetch json from cache
  snprintf(stash, sizeof(stash), "plugin/%s/%s", jsp->jsp_id, lstash);
  value = blobcache_get(key, stash, &vsize, 0, NULL, NULL, NULL);

  if(value == NULL) {
    *rval = OBJECT_TO_JSVAL(NULL);
    return JS_TRUE;
  }

  // deserialize into json object
  if(!JS_EnterLocalRootScope(cx))
    return JS_FALSE;

  o = json_deserialize(value, &json_to_jsapi, cx, errbuf, sizeof(errbuf));

  *rval = OBJECT_TO_JSVAL(o);

  JS_LeaveLocalRootScope(cx);

  if(o == NULL) {
    JS_ReportError(cx, "Invalid JSON stored in cache -- %s", errbuf);
    return JS_FALSE;
  }

  return JS_TRUE;
}

/**
 *
 */
JSBool 
js_get_descriptor(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  char pdesc[PATH_MAX];
  char *pe, *desc;
  char errbuf[128];
  JSObject *o;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  snprintf(pdesc, sizeof(pdesc),"%s", jsp->jsp_url);
  pe = strrchr(pdesc, '/');
  if (pe == NULL)
    return JS_FALSE;

  snprintf(pe + 1, sizeof(pdesc) - (pe - pdesc), "plugin.json");

  desc = fa_load(pdesc, NULL, NULL, errbuf, sizeof(errbuf), 
		NULL, 0, NULL, NULL);
  if (desc == NULL) {
    TRACE(TRACE_ERROR, "JS", "Unable to read %s -- %s", pdesc, errbuf);
    return JS_FALSE;
  }

  if (!JS_EnterLocalRootScope(cx))
    return JS_FALSE;

  o = json_deserialize(desc, &json_to_jsapi, cx, errbuf, sizeof(errbuf));
  free(desc);
  *rval = OBJECT_TO_JSVAL(o);

  JS_LeaveLocalRootScope(cx);

  return JS_TRUE;
}
