/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2009 Andreas Öman
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
#include <sys/types.h>
#include <string.h>
#include <assert.h>

#include "js.h"

#include "backend/backend.h"
#include "misc/string.h"
#include "fileaccess/fileaccess.h"


static JSRuntime *runtime;
static JSObject *showtimeobj;
static struct js_plugin_list js_plugins;

static JSClass global_class = {
  "global", JSCLASS_GLOBAL_FLAGS,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static void
err_reporter(JSContext *cx, const char *msg, JSErrorReport *r)
{
  int level;

  if(r->flags & JSREPORT_WARNING)
    level = TRACE_INFO;
  else
    level = TRACE_ERROR;

  TRACE(level, "JS", "%s:%u %s",  r->filename, r->lineno, msg);
}


/**
 *
 */
JSContext *
js_newctx(void)
{
  JSContext *cx = JS_NewContext(runtime, 8192);

  JS_SetOptions(cx, 
		JSOPTION_STRICT |
		JSOPTION_WERROR | 
		JSOPTION_VAROBJFIX);
  JS_SetErrorReporter(cx, err_reporter);
#ifdef JS_GC_ZEAL
  JS_SetGCZeal(cx, 1);
#endif
  return cx;
}



/**
 *
 */
static JSBool 
js_trace(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  TRACE(TRACE_DEBUG, "JS", "%s", str);
  *rval = JSVAL_VOID;  /* return undefined */
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  fprintf(stderr, "%s\n", str);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_queryStringSplit(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  char *s, *s0;
  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  s0 = s = strdup(str);

  while(s) {
    
    char *k = s;
    char *v = strchr(s, '=');
    if(v == NULL)
      break;

    *v++ = 0;

    if((s = strchr(v, '&')) != NULL)
      *s++ = 0;

    k = strdup(k);
    v = strdup(v);

    http_deescape(k);
    http_deescape(v);

    jsval val = STRING_TO_JSVAL(JS_NewString(cx, v, strlen(v)));
    JS_SetProperty(cx, robj, k, &val);
    free(k);
  }
  free(s0);
  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_httpEscape(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  char *r;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  size_t l = strlen(str);
  
  r = malloc((l * 3) + 1);
  
  path_escape(r, l * 3, str);

  *rval = STRING_TO_JSVAL(JS_NewString(cx, r, strlen(r)));
  return JS_TRUE;
}


/**
 *
 */
void
js_prop_set_from_jsval(JSContext *cx, prop_t *p, jsval value)
{
  if(JSVAL_IS_INT(value)) {
    prop_set_int(p, JSVAL_TO_INT(value));
  } else if(JSVAL_IS_DOUBLE(value)) {
    double d;
    if(JS_ValueToNumber(cx, value, &d))
      prop_set_float(p, d);
  } else {
    prop_set_string(p, JS_GetStringBytes(JS_ValueToString(cx, value)));
  }
}


/**
 *
 */
int
js_prop_from_object(JSContext *cx, JSObject *obj, prop_t *p)
{
  JSIdArray *ida;
  int i;

  if((ida = JS_Enumerate(cx, obj)) == NULL)
    return -1;
  
  for(i = 0; i < ida->length; i++) {
    jsval name, value;
    prop_t *val;
    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;
    
    if(!JSVAL_IS_STRING(name))
      continue;
    
    if(!JS_GetProperty(cx, obj, JS_GetStringBytes(JSVAL_TO_STRING(name)),
		       &value))
      continue;
    
    val = prop_create(p, JS_GetStringBytes(JSVAL_TO_STRING(name)));
    js_prop_set_from_jsval(cx, val, value);
  }
  JS_DestroyIdArray(cx, ida);
  return 0;
}


/**
 *
 */
static JSFunctionSpec showtime_functions[] = {
    JS_FS("trace",            js_trace,    1, 0, 0),
    JS_FS("print",            js_print,    1, 0, 0),
    JS_FS("httpGet",          js_httpRequest, 4, 0, 0),
    JS_FS("readFile",         js_readFile, 1, 0, 0),
    JS_FS("queryStringSplit", js_queryStringSplit, 1, 0, 0),
    JS_FS("httpEscape",       js_httpEscape, 1, 0, 0),
    JS_FS("createService",    js_createService, 3, 0, 0),
    JS_FS_END
};



static JSClass showtime_class = {
  "showtime", 0,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static int
js_init(void)
{
  JSContext *cx;

  JS_SetCStringsAreUTF8();

  runtime = JS_NewRuntime(0x100000); 

  cx = js_newctx();

  JS_BeginRequest(cx);

  showtimeobj = JS_NewObject(cx, &showtime_class, NULL, NULL);
  JS_DefineFunctions(cx, showtimeobj, showtime_functions);

  JS_AddNamedRoot(cx, &showtimeobj, "showtime");

  JS_EndRequest(cx);
  JS_DestroyContext(cx);

  return 0;
}





/**
 *
 */
static void
plugin_finalize(JSContext *cx, JSObject *obj)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  assert(LIST_FIRST(&jsp->jsp_routes) == NULL);
  assert(LIST_FIRST(&jsp->jsp_searchers) == NULL);

  TRACE(TRACE_INFO, "JS", "Plugin %s unloaded", jsp->jsp_url);
  
  LIST_REMOVE(jsp, jsp_link);
  
  free(jsp->jsp_url);
  free(jsp);
}


/**
 *
 */
static void
js_plugin_unload(JSContext *cx, js_plugin_t *jsp)
{
  js_page_flush_from_plugin(cx, jsp);

}

/**
 *
 */
static JSBool
js_forceUnload(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  js_plugin_unload(cx, JS_GetPrivate(cx, obj));
  *rval = JSVAL_VOID;
  return JS_TRUE;
}

/**
 *
 */
static JSBool 
jsp_setEnabled(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  jsp->jsp_enabled = on;

  TRACE(TRACE_DEBUG, "plugins", 
	"Plugin %s %sabled", jsp->jsp_id, jsp->jsp_enabled ? "en" : "dis");

  return JS_TRUE;
}


/**
 *
 */
static JSClass plugin_class = {
  "plugin", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, plugin_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};

/**
 *
 */
static JSClass plugin_conf_class = {
  "pluginconf", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSFunctionSpec plugin_functions[] = {
    JS_FS("addURI",           js_addURI,      2, 0, 0),
    JS_FS("addSearcher",      js_addSearcher, 3, 0, 0),
    JS_FS("forceUnload",      js_forceUnload, 0, 0, 0),
    JS_FS("createSettings",   js_createSettings, 2, 0, 0),
    JS_FS_END
};


/**
 *
 */
int
js_plugin_load(const char *id, const char *url, char *errbuf, size_t errlen)
{
  char *sbuf;
  size_t ssize;
  JSContext *cx;
  js_plugin_t *jsp;
  JSObject *pobj, *gobj, *confobj;
  JSScript *s;
  char path[PATH_MAX];
  jsval val;

  if((sbuf = fa_quickload(url, &ssize, NULL, errbuf, errlen)) == NULL)
    return -1;

  cx = js_newctx();
  JS_BeginRequest(cx);

  /* Remove any plugin with same URL */
  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    if(!strcmp(jsp->jsp_id, id))
      break;
  if(jsp != NULL)
    js_plugin_unload(cx, jsp);

  jsp = calloc(1, sizeof(js_plugin_t));
  jsp->jsp_url = strdup(url);
  jsp->jsp_id  = strdup(id);

  LIST_INSERT_HEAD(&js_plugins, jsp, jsp_link);

  gobj = JS_NewObject(cx, &global_class, NULL, NULL);
  JS_InitStandardClasses(cx, gobj);

  JS_DefineProperty(cx, gobj, "showtime", OBJECT_TO_JSVAL(showtimeobj),
		    NULL, NULL, JSPROP_READONLY | JSPROP_PERMANENT);

  /* Plugin object */
  pobj = JS_NewObject(cx, &plugin_class, NULL, gobj);
  JS_AddNamedRoot(cx, &pobj, "plugin");

  JS_SetPrivate(cx, pobj, jsp);

  JS_DefineFunctions(cx, pobj, plugin_functions);

  /* Plugin config object */
  confobj = JS_DefineObject(cx, pobj, "config", &plugin_conf_class, NULL, 0);

  JS_SetPrivate(cx, confobj, jsp);

  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, url));
  JS_SetProperty(cx, confobj, "url", &val);

  if(!fa_parent(path, sizeof(path), url)) {
    val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, path));
    JS_SetProperty(cx, confobj, "path", &val);
  }

  JS_DefineProperty(cx, confobj, "enabled", BOOLEAN_TO_JSVAL(1),
		    NULL, jsp_setEnabled, JSPROP_PERMANENT);

  s = JS_CompileScript(cx, pobj, sbuf, ssize, url, 0);
  free(sbuf);

  if(s != NULL) {
    JSObject *sobj = JS_NewScriptObject(cx, s);
    jsval result;

    JS_AddNamedRoot(cx, &sobj, "script");
    JS_ExecuteScript(cx, pobj, s, &result);
    JS_RemoveRoot(cx, &sobj);
  }

  JS_RemoveRoot(cx, &pobj);
 
  JS_EndRequest(cx);

  JS_GC(cx);

  JS_DestroyContext(cx);

  return 0;
}







/**
 *
 */
static backend_t be_js = {
  .be_init = js_init,
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = js_backend_open,
  .be_search = js_backend_search,
};

BE_REGISTER(js);
