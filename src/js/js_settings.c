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

  JSContext *jss_cx;
  JSObject *jss_obj;

} js_setting_t;


/**
 *
 */
typedef struct js_setting_group {

  setting_t *jsg_s;

  prop_t *jsg_root;

  htsmsg_t *jsg_store;
  char *jsg_spath;
  int jsg_frozen;
} js_setting_group_t;




/**
 *
 */
static void
setting_finalize(JSContext *cx, JSObject *obj)
{
  js_setting_t *jss = JS_GetPrivate(cx, obj);
  if(jss->jss_s != NULL)
    setting_destroy(jss->jss_s);
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



/**
 *
 */
static void
js_setting_group_save(void *opaque, htsmsg_t *msg)
{
  js_setting_group_t *jsg = opaque;
  htsmsg_store_save(msg, jsg->jsg_spath);
}


/**
 *
 */
static JSContext *
settings_get_cx(js_setting_t *jss)
{
  JSContext *cx;
  if(jss->jss_cx == NULL) {
    cx = js_newctx(NULL);
    JS_BeginRequest(cx);
  } else {
    cx = jss->jss_cx;
  }
  return cx;
}


/**
 *
 */
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
static void
js_store_update_int(void *opaque, int value)
{
  js_setting_t *jss = opaque;
  settings_update(settings_get_cx(jss), jss, INT_TO_JSVAL(value));
}


/**
 *
 */
static js_setting_t *
jss_create(JSContext *cx, JSObject *obj, const char *id, jsval *rval)
{
  js_setting_t *jss = calloc(1, sizeof(js_setting_t));

  jss->jss_obj = JS_DefineObject(cx, obj, id, &setting_class, NULL, 0);
  *rval = OBJECT_TO_JSVAL(jss->jss_obj);
  JS_SetPrivate(cx, jss->jss_obj, jss);
  return jss;
}

/**
 *
 */
static JSBool 
js_createBool(JSContext *cx, JSObject *obj, uintN argc, 
	      jsval *argv, jsval *rval)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *id;
  const char *title;
  JSBool def;
  JSObject *func;
  jsval v;

  if(!JS_ConvertArguments(cx, argc, argv, "ssbo",
			  &id, &title, &def, &func))
    return JS_FALSE;

  if(!JS_ObjectIsFunction(cx, func)) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_setting_t *jss = jss_create(cx, obj, id, rval);

  v = OBJECT_TO_JSVAL(func);
  JS_SetProperty(cx, jss->jss_obj, "callback", &v);
  jss->jss_cx = cx;
  jss->jss_s = settings_create_bool(jsg->jsg_root, id, _p(title),
				    def, jsg->jsg_store,
				    js_store_update_bool, jss,
				    SETTINGS_INITIAL_UPDATE, NULL,
				    js_setting_group_save, jsg);
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
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *id;
  const char *title;
  const char *def;
  JSObject *func;
  jsval v;

  if(!JS_ConvertArguments(cx, argc, argv, "ssso",
			  &id, &title, &def, &func))
    return JS_FALSE;

  if(!JS_ObjectIsFunction(cx, func)) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_setting_t *jss = jss_create(cx, obj, id, rval);

  v = OBJECT_TO_JSVAL(func);
  JS_SetProperty(cx, jss->jss_obj, "callback", &v);
  jss->jss_cx = cx;
  jss->jss_s = settings_create_string(jsg->jsg_root, id, _p(title),
				      def, jsg->jsg_store,
				      js_store_update_string, jss,
				      SETTINGS_INITIAL_UPDATE, NULL,
				      js_setting_group_save, jsg);
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
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *id, *icon, *text;

  if(!JS_ConvertArguments(cx, argc, argv, "sss", &id, &icon, &text))
    return JS_FALSE;

  settings_create_info(jsg->jsg_root, icon, _p(text));
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_createDivider(JSContext *cx, JSObject *obj, uintN argc, 
	      jsval *argv, jsval *rval)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *title;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &title))
    return JS_FALSE;

  settings_create_divider(jsg->jsg_root, _p(title));
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_createInt(JSContext *cx, JSObject *obj, uintN argc, 
		jsval *argv, jsval *rval)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *id;
  const char *title;
  int def;
  int min;
  int max;
  int step;
  const char* unit;
  JSObject *func;
  jsval v;

  if(!JS_ConvertArguments(cx, argc, argv, "ssiiiiso",
			  &id, &title, &def, &min, &max, &step, &unit, &func)){
    return JS_FALSE;
  }

  if(!JS_ObjectIsFunction(cx, func)) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_setting_t *jss = jss_create(cx, obj, id, rval);

  v = OBJECT_TO_JSVAL(func);
  JS_SetProperty(cx, jss->jss_obj, "callback", &v);
  jss->jss_cx = cx;
  jss->jss_s = settings_create_int(jsg->jsg_root, id, _p(title),
				      def, jsg->jsg_store,
                                      min, max, step,
				      js_store_update_int, jss,
				      SETTINGS_INITIAL_UPDATE, unit, NULL,
				      js_setting_group_save, jsg);
  jss->jss_cx = NULL;

  return JS_TRUE;
}


/**
 *
 */
static jsval
jsval_from_htsmsgfield(JSContext *cx, htsmsg_field_t *f)
{
  jsdouble *d;
  switch(f->hmf_type) {
  case HMF_STR:
    return STRING_TO_JSVAL(JS_NewStringCopyZ(cx, f->hmf_str));

  case HMF_S64:
    if(f->hmf_s64 <= INT32_MAX && f->hmf_s64 >= INT32_MIN &&
       INT_FITS_IN_JSVAL(f->hmf_s64))
      return INT_TO_JSVAL(f->hmf_s64);
    if((d = JS_NewDouble(cx, f->hmf_s64)) != NULL)
      return DOUBLE_TO_JSVAL(d);
    break;

  case HMF_DBL:
    if((d = JS_NewDouble(cx, f->hmf_dbl)) != NULL)
      return DOUBLE_TO_JSVAL(d);
    break;
  }
  return JSVAL_NULL;
}

/**
 *
 */
static JSFunctionSpec setting_functions[] = {
    JS_FS("createBool", js_createBool, 4, 0, 0),
    JS_FS("createString", js_createString, 4, 0, 0),
    JS_FS("createInfo", js_createInfo, 3, 0, 0),
    JS_FS("createDivider", js_createDivider, 1, 0, 0),
    JS_FS("createInt", js_createInt, 8, 0, 0),
    JS_FS_END
};




/**
 *
 */
static void
setting_group_finalize(JSContext *cx, JSObject *obj)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  if(jsg->jsg_root != NULL)
    prop_destroy(jsg->jsg_root);
  if(jsg->jsg_store != NULL)
    htsmsg_destroy(jsg->jsg_store);
  
  free(jsg->jsg_spath);
  free(jsg);
}


/**
 *
 */
static JSBool
jsg_set_prop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *name;

  if(jsg->jsg_frozen)
    return JS_TRUE;

  name = JSVAL_IS_STRING(id) ? JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;
  if(name != NULL) {
    jsval v = *vp;
    
    if(JSVAL_IS_INT(v)) {
      htsmsg_delete_field(jsg->jsg_store, name);
      htsmsg_add_s32(jsg->jsg_store, name, JSVAL_TO_INT(v));
    } else if(JSVAL_IS_DOUBLE(v)) {
      double d;
      if(JS_ValueToNumber(cx, v, &d)) {
	htsmsg_delete_field(jsg->jsg_store, name);
	htsmsg_add_dbl(jsg->jsg_store, name, d);
      }
    } else if(JSVAL_IS_STRING(v)) {
      htsmsg_delete_field(jsg->jsg_store, name);
      htsmsg_add_str(jsg->jsg_store, name, 
		     JS_GetStringBytes(JS_ValueToString(cx, v)));
    } else {
      return JS_TRUE;
    }
    htsmsg_store_save(jsg->jsg_store, jsg->jsg_spath);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
jsg_del_prop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *name;
  name = JSVAL_IS_STRING(id) ? JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;
  if(name != NULL) {
    htsmsg_delete_field(jsg->jsg_store, name);
    htsmsg_store_save(jsg->jsg_store, jsg->jsg_spath);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
jsg_get_prop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *name;
  name = JSVAL_IS_STRING(id) ? JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;
  if(name != NULL) {
    htsmsg_field_t *f;

    if((f = htsmsg_field_find(jsg->jsg_store, name)) != NULL)
      *vp = jsval_from_htsmsgfield(cx, f);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
jsg_resolve_prop(JSContext *cx, JSObject *obj, jsval id)
{
  js_setting_group_t *jsg = JS_GetPrivate(cx, obj);
  const char *name;
  name = JSVAL_IS_STRING(id) ? JS_GetStringBytes(JSVAL_TO_STRING(id)) : NULL;
  if(name != NULL) {
    htsmsg_field_t *f;

    if((f = htsmsg_field_find(jsg->jsg_store, name)) != NULL) {
      jsval vp = jsval_from_htsmsgfield(cx, f);
      JS_SetProperty(cx, obj, name, &vp);
    }
  }
  return JS_TRUE;
}


/**
 *
 */
static JSClass setting_group_class = {
  "settinggroup", JSCLASS_HAS_PRIVATE,
  jsg_set_prop, jsg_del_prop, jsg_get_prop, jsg_set_prop,
  JS_EnumerateStub,jsg_resolve_prop,JS_ConvertStub, setting_group_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
JSBool 
js_createSettings(JSContext *cx, JSObject *obj, uintN argc, 
		  jsval *argv, jsval *rval)
{
  const char *title;
  const char *icon = NULL;
  const char *desc = NULL;
  char spath[URL_MAX];

  if(!JS_ConvertArguments(cx, argc, argv, "s/ss", &title, &icon, &desc))
    return JS_FALSE;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  snprintf(spath, sizeof(spath), "plugins/%s", jsp->jsp_id);

  js_setting_group_t *jsg = calloc(1, sizeof(js_setting_group_t));
  JSObject *robj;
  jsg->jsg_frozen = 1;
  jsg->jsg_spath = strdup(spath);
  jsg->jsg_store = htsmsg_store_load(spath) ?: htsmsg_create_map();
  jsg->jsg_root = settings_add_dir(settings_apps, _p(title), NULL, icon,
				   _p(desc));

  robj = JS_NewObjectWithGivenProto(cx, &setting_group_class, NULL, obj);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jsg);

  JS_DefineFunctions(cx, robj, setting_functions);
  jsg->jsg_frozen = 0;
  return JS_TRUE;
}



/**
 *
 */
JSBool 
js_createStore(JSContext *cx, JSObject *obj, uintN argc, 
	       jsval *argv, jsval *rval)
{
  const char *id;
  char spath[URL_MAX];
  JSBool per_user = 0;

  if(!JS_ConvertArguments(cx, argc, argv, "s/b", &id, &per_user))
    return JS_FALSE;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  snprintf(spath, sizeof(spath), "jsstore/%s-%s", jsp->jsp_id, id);

  js_setting_group_t *jsg = calloc(1, sizeof(js_setting_group_t));
  JSObject *robj;
  jsg->jsg_frozen = 1;
  jsg->jsg_spath = strdup(spath);
  jsg->jsg_store = htsmsg_store_load(spath) ?: htsmsg_create_map();

  robj = JS_NewObjectWithGivenProto(cx, &setting_group_class, NULL, obj);
  *rval = OBJECT_TO_JSVAL(robj);
  JS_SetPrivate(cx, robj, jsg);
  jsg->jsg_frozen = 0;
  return JS_TRUE;
}

