/*
 *  JSAPI <-> Setting objects
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
#include "htsmsg/htsmsg.h"

#include "settings.h"


/**
 *
 */
typedef struct js_setting {

  setting_t *jss_s;
  prop_t *jss_p;

  JSContext *jss_cx;
  JSObject *jss_obj;

  htsmsg_t *jss_store;
  char *jss_spath;

} js_setting_t;


/**
 *
 */
static void
setting_finalize(JSContext *cx, JSObject *obj)
{
  js_setting_t *jss = JS_GetPrivate(cx, obj);
  if(jss->jss_s != NULL)
    setting_destroy(jss->jss_s);
  if(jss->jss_p != NULL)
    prop_destroy(jss->jss_p);
  if(jss->jss_store != NULL)
    htsmsg_destroy(jss->jss_store);
  
  free(jss->jss_spath);
  free(jss);
}



/**
 *
 */
static JSClass setting_class = {
  "setting", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, setting_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};



static void
js_settings_save(void *opaque, htsmsg_t *msg)
{
  js_setting_t *jss = opaque;
  htsmsg_store_save(msg, jss->jss_spath);
}

static JSContext *
settings_get_cx(js_setting_t *jss)
{
  JSContext *cx;
  if(jss->jss_cx == NULL) {
    cx = js_newctx();
    JS_BeginRequest(cx);
  } else {
    cx = jss->jss_cx;
  }
  return cx;
}


static void
settings_update(JSContext *cx, js_setting_t *jss, jsval v)
{
  jsval cb, *argv, result;
  void *mark;

  JS_SetProperty(cx, jss->jss_obj, "value", &v);
  
  JS_GetProperty(cx, jss->jss_obj, "callback", &cb);
  argv = JS_PushArguments(cx, &mark, "v", v);
  JS_CallFunctionValue(cx, NULL, cb, 1, argv, &result);
  JS_PopArguments(cx, mark);

  if(jss->jss_cx == NULL) {
    JS_EndRequest(cx);
    JS_DestroyContext(cx);
  }
}


/**
 *
 */
static void
js_store_update_bool(void *opaque, int value)
{
  js_setting_t *jss = opaque;
  settings_update(settings_get_cx(jss), jss, BOOLEAN_TO_JSVAL(!!value));
}

/**
 *
 */
static void
js_store_update_string(void *opaque, const char *str)
{
  js_setting_t *jss = opaque;
  JSContext *cx = settings_get_cx(jss);

  settings_update(cx, jss,
		  str ? STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str))
		  : JSVAL_NULL);
}



/**
 *
 */
static JSBool 
js_createBool(JSContext *cx, JSObject *obj, uintN argc, 
	      jsval *argv, jsval *rval)
{
  js_setting_t *parent = JS_GetPrivate(cx, obj);
  const char *id;
  const char *title;
  JSBool def;
  JSObject *func, *robj;
  jsval v;

  if(!JS_ConvertArguments(cx, argc, argv, "ssbo",
			  &id, &title, &def, &func))
    return JS_FALSE;

  if(!JS_ObjectIsFunction(cx, func)) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_setting_t *jss = calloc(1, sizeof(js_setting_t));

  robj = JS_DefineObject(cx, obj, id, &setting_class, NULL, 0);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jss);

  v = OBJECT_TO_JSVAL(func);
  JS_SetProperty(cx, robj, "callback", &v);
  
  jss->jss_cx = cx;
  jss->jss_obj = robj;
  jss->jss_s = settings_create_bool(parent->jss_p, id, title,
				    def, parent->jss_store,
				    js_store_update_bool, jss,
				    SETTINGS_INITIAL_UPDATE, NULL,
				    js_settings_save, parent);
  jss->jss_cx = NULL;

  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_createString(JSContext *cx, JSObject *obj, uintN argc, 
		jsval *argv, jsval *rval)
{
  js_setting_t *parent = JS_GetPrivate(cx, obj);
  const char *id;
  const char *title;
  const char *def;
  JSObject *func, *robj;
  jsval v;

  if(!JS_ConvertArguments(cx, argc, argv, "ssso",
			  &id, &title, &def, &func))
    return JS_FALSE;

  if(!JS_ObjectIsFunction(cx, func)) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_setting_t *jss = calloc(1, sizeof(js_setting_t));

  robj = JS_DefineObject(cx, obj, id, &setting_class, NULL, 0);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jss);

  v = OBJECT_TO_JSVAL(func);
  JS_SetProperty(cx, robj, "callback", &v);
  
  jss->jss_cx = cx;
  jss->jss_obj = robj;
  jss->jss_s = settings_create_string(parent->jss_p, id, title,
				      def, parent->jss_store,
				      js_store_update_string, jss,
				      SETTINGS_INITIAL_UPDATE, NULL,
				      js_settings_save, parent);
  jss->jss_cx = NULL;

  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_createInfo(JSContext *cx, JSObject *obj, uintN argc, 
	      jsval *argv, jsval *rval)
{
  js_setting_t *parent = JS_GetPrivate(cx, obj);
  const char *id;
  const char *icon;
  const char *text;
  JSObject *robj;

  if(!JS_ConvertArguments(cx, argc, argv, "sss", &id, &icon, &text))
    return JS_FALSE;

  js_setting_t *jss = calloc(1, sizeof(js_setting_t));

  robj = JS_DefineObject(cx, obj, id, &setting_class, NULL, 0);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jss);
  
  jss->jss_obj = robj;
  settings_create_info(parent->jss_p, icon, text);
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec setting_functions[] = {
    JS_FS("createBool", js_createBool, 4, 0, 0),
    JS_FS("createString", js_createString, 4, 0, 0),
    JS_FS("createInfo", js_createInfo, 3, 0, 0),
    JS_FS_END
};


/**
 *
 */
JSBool 
js_createSettings(JSContext *cx, JSObject *obj, uintN argc, 
		  jsval *argv, jsval *rval)
{
  const char *title;
  const char *type;
  const char *icon = NULL;
  char spath[URL_MAX];

  if(!JS_ConvertArguments(cx, argc, argv, "ss/s", &title, &type, &icon))
    return JS_FALSE;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  snprintf(spath, sizeof(spath), "plugins/%s", jsp->jsp_id);

  js_setting_t *jss = calloc(1, sizeof(js_setting_t));
  JSObject *robj;

  jss->jss_spath = strdup(spath);
  jss->jss_store = htsmsg_store_load(spath) ?: htsmsg_create_map();

  jss->jss_p = settings_add_dir(settings_apps, title, type, icon);

  robj = JS_NewObjectWithGivenProto(cx, &setting_class, NULL, obj);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jss);

  JS_DefineFunctions(cx, robj, setting_functions);
  return JS_TRUE;
}

