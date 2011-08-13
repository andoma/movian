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
#include <unistd.h>
#include <limits.h>

#include "js.h"

#include "backend/backend.h"
#include "misc/string.h"
#include "fileaccess/fileaccess.h"
#include "keyring.h"
#include "notifications.h"

static JSRuntime *runtime;
static JSObject *showtimeobj;
static JSObject *RichText;
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
js_newctx(JSErrorReporter er)
{
  JSContext *cx = JS_NewContext(runtime, 8192);

  JS_SetOptions(cx, 
		JSOPTION_STRICT |
		JSOPTION_WERROR | 
		JSOPTION_VAROBJFIX);
  JS_SetErrorReporter(cx, er ?: err_reporter);
#ifdef JS_GC_ZEAL
  //  JS_SetGCZeal(cx, 1);
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

    url_deescape(k);
    url_deescape(v);

    jsval val = STRING_TO_JSVAL(JS_NewString(cx, v, strlen(v)));
    JS_SetProperty(cx, robj, k, &val);
    free(k);
  }
  free(s0);
  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}

static JSBool 
js_escape(JSContext *cx, JSObject *obj,
	  uintN argc, jsval *argv, jsval *rval, int how)
{

  const char *str;
  char *r;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  size_t len = url_escape(NULL, 0, str, how);
  r = malloc(len);
  url_escape(r, len, str, how);

  *rval = STRING_TO_JSVAL(JS_NewString(cx, r, len-1));
  return JS_TRUE;
}

/**
 *
 */
static JSBool 
js_pathEscape(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  return js_escape(cx, obj, argc, argv, rval, URL_ESCAPE_PATH);
}


/**
 *
 */
static JSBool 
js_paramEscape(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  return js_escape(cx, obj, argc, argv, rval, URL_ESCAPE_PARAM);
}


/**
 *
 */
static void
js_prop_from_str(JSContext *cx, prop_t *p, jsval value)
{
  prop_set_string(p, JS_GetStringBytes(JS_ValueToString(cx, value)));
}

/**
 *
 */
void
js_prop_set_from_jsval(JSContext *cx, prop_t *p, jsval value)
{
  JSBool b;
  if(JSVAL_IS_INT(value)) {
    prop_set_int(p, JSVAL_TO_INT(value));
  } else if(JSVAL_IS_BOOLEAN(value)) {
    prop_set_int(p, JSVAL_TO_BOOLEAN(value));
  } else if(JSVAL_IS_NULL(value) || JSVAL_IS_VOID(value)) {
    prop_set_void(p);
  } else if(JSVAL_IS_DOUBLE(value)) {
    double d;
    if(JS_ValueToNumber(cx, value, &d))
      prop_set_float(p, d);
  } else if(JS_HasInstance(cx, RichText, value, &b) && b) {
    JSObject *o = JSVAL_TO_OBJECT(value);
    jsval v2;

    if(!JS_EnterLocalRootScope(cx))
      return;

    if(!JS_GetProperty(cx, o, "text", &v2)) {
      JS_LeaveLocalRootScope(cx);
      return;
    }

    prop_set_string_ex(p, NULL, JS_GetStringBytes(JS_ValueToString(cx, v2)),
		       PROP_STR_RICH);
    JS_LeaveLocalRootScope(cx);
  } else if(JSVAL_IS_STRING(value)) {
    js_prop_from_str(cx, p, value);
  } else if(JSVAL_IS_OBJECT(value)) {
    JSObject *obj = JSVAL_TO_OBJECT(value);
    JSClass *c = JS_GetClass(cx, obj);

    if(!strcmp(c->name, "XML"))   // Treat some classes special
      js_prop_from_str(cx, p, value);
    else
      js_prop_from_object(cx, obj, p);
  } else {
    prop_set_void(p);
  }
}


/**
 *
 */
int
js_prop_from_object(JSContext *cx, JSObject *obj, prop_t *p)
{
  JSIdArray *ida;
  int i, r = 0;
  const char *n;

  if((ida = JS_Enumerate(cx, obj)) == NULL)
    return -1;
  
  for(i = 0; i < ida->length; i++) {
    jsval name, value;

    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;

    if(JSVAL_IS_STRING(name)) {
      n = JS_GetStringBytes(JSVAL_TO_STRING(name));
      if(!JS_GetProperty(cx, obj, n, &value))
	continue;
    } else if(JSVAL_IS_INT(name)) {
      if(!JS_GetElement(cx, obj, JSVAL_TO_INT(name), &value) ||
	 JSVAL_IS_VOID(value))
	continue;
      n = NULL;
    } else {
      continue;
    }

    if(JSVAL_TO_OBJECT(value) == obj)
      continue;

    js_prop_set_from_jsval(cx, prop_create(p, n), value);
  }
  JS_DestroyIdArray(cx, ida);
  return r;
}


/**
 *
 */
static JSBool 
js_canHandle(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  *rval = BOOLEAN_TO_JSVAL(!!backend_canhandle(str));
  return JS_TRUE;
}



/**
 *
 */
static JSBool
js_getAuthCredentials(JSContext *cx, JSObject *obj,
		      uintN argc, jsval *argv, jsval *rval)
{
  char buf[256];
  const char *id = NULL, *reason, *source;
  char *username, *password;
  JSBool query, forcetmp = 0;
  int r;
  jsval val;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "ssb/sb",
			  &source, &reason, &query, &id, &forcetmp))
    return JS_FALSE;

  snprintf(buf, sizeof(buf), "plguin-%s%s%s", jsp->jsp_id,
	   id ? "-" : "", id ?: "");

  r = keyring_lookup(buf, &username, &password, NULL, query, source, reason,
		     forcetmp);

  if(r == 1) {
    *rval = BOOLEAN_TO_JSVAL(0);
    return JS_TRUE;
  }
  
  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  if(r == -1) {
    val = BOOLEAN_TO_JSVAL(1);
    JS_SetProperty(cx, robj, "rejected", &val);
  } else {

    val = STRING_TO_JSVAL(JS_NewString(cx, username, strlen(username)));
    JS_SetProperty(cx, robj, "username", &val);

    val = STRING_TO_JSVAL(JS_NewString(cx, password, strlen(password)));
    JS_SetProperty(cx, robj, "password", &val);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_message(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *message;
  JSBool ok, cancel;
  int r;

  if(!JS_ConvertArguments(cx, argc, argv, "sbb", &message, &ok, &cancel))
    return JS_FALSE;

  r = message_popup(message, 
		    (ok     ? MESSAGE_POPUP_OK : 0) |
		    (cancel ? MESSAGE_POPUP_CANCEL : 0) | 
		    MESSAGE_POPUP_RICH_TEXT);


  *rval = BOOLEAN_TO_JSVAL(r == MESSAGE_POPUP_OK);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_sleep(JSContext *cx, JSObject *obj,
	 uintN argc, jsval *argv, jsval *rval)
{
  int msec;

  if (!JS_ConvertArguments(cx, argc, argv, "u", &msec))
    return JS_FALSE;

  jsrefcount s = JS_SuspendRequest(cx);
  usleep(msec * 1000);
  JS_ResumeRequest(cx, s);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_time(JSContext *cx, JSObject *obj,
	uintN argc, jsval *argv, jsval *rval)
{
  time_t t;
  time(&t);
  jsdouble *d = JS_NewDouble(cx, t);
  *rval = DOUBLE_TO_JSVAL(d);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_durationtostring(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  int s;
  char tmp[32];
  if (!JS_ConvertArguments(cx, argc, argv, "u", &s))
    return JS_FALSE;

  int m = s / 60;
  int h = s / 3600;
  
  if(h > 0) {
    snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
  } else {
    snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
  }

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, tmp));
  return JS_TRUE;
}

/**
 *
 */
static JSFunctionSpec showtime_functions[] = {
    JS_FS("trace",            js_trace,    1, 0, 0),
    JS_FS("print",            js_print,    1, 0, 0),
    JS_FS("httpGet",          js_httpGet, 2, 0, 0),
    JS_FS("httpPost",         js_httpPost, 2, 0, 0),
    JS_FS("readFile",         js_readFile, 1, 0, 0),
    JS_FS("queryStringSplit", js_queryStringSplit, 1, 0, 0),
    JS_FS("pathEscape",       js_pathEscape, 1, 0, 0),
    JS_FS("paramEscape",      js_paramEscape, 1, 0, 0),
    JS_FS("canHandle",        js_canHandle, 1, 0, 0),
    JS_FS("message",          js_message, 3, 0, 0),
    JS_FS("sleep",            js_sleep, 1, 0, 0),
    JS_FS("JSONEncode",       js_json_encode, 1, 0, 0),
    JS_FS("JSONDecode",       js_json_decode, 1, 0, 0),
    JS_FS("time",             js_time, 0, 0, 0),
    JS_FS("durationToString", js_durationtostring, 0, 0, 0),
    JS_FS("probe",            js_probe, 1, 0, 0),
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
static JSBool 
js_RichText(JSContext *cx, JSObject *obj,
	    uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;
  jsval v = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str));

  JS_SetProperty(cx, obj, "text", &v);

  *rval = JSVAL_VOID;
  return JS_TRUE;
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
  assert(LIST_FIRST(&jsp->jsp_http_auths) == NULL);

  TRACE(TRACE_DEBUG, "JS", "Plugin %s unloaded", jsp->jsp_url);
  
  LIST_REMOVE(jsp, jsp_link);
  
  free(jsp->jsp_url);
  free(jsp->jsp_id);
  free(jsp);
}


/**
 *
 */
static void
js_plugin_unload0(JSContext *cx, js_plugin_t *jsp)
{
  js_page_flush_from_plugin(cx, jsp);
  js_io_flush_from_plugin(cx, jsp);
  js_service_flush_from_plugin(cx, jsp);
}

/**
 *
 */
static JSBool
js_forceUnload(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  js_plugin_unload0(cx, JS_GetPrivate(cx, obj));
  *rval = JSVAL_VOID;
  return JS_TRUE;
}

/**
 *
 */
static JSBool 
jsp_setEnableURIRoute(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  jsp->jsp_enable_uri_routing = on;

  TRACE(TRACE_DEBUG, "plugins", "Plugin %s %sabled URI routing",
	jsp->jsp_id, on ? "en" : "dis");

  return JS_TRUE;
}

/**
 *
 */
static JSBool 
jsp_setEnableSearch(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  jsp->jsp_enable_search = on;

  TRACE(TRACE_DEBUG, "plugins", "Plugin %s %sabled search",
	jsp->jsp_id, on ? "en" : "dis");

  return JS_TRUE;
}


static JSBool
plugin_add_del_prop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(jsp->jsp_protect_object) {
    JS_ReportError(cx, "Plugin object can not be modified");
    return JS_FALSE;
  }
  return JS_TRUE;
}


/**
 *
 */
static JSClass plugin_class = {
  "plugin", JSCLASS_HAS_PRIVATE,
  plugin_add_del_prop,plugin_add_del_prop,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, plugin_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSFunctionSpec plugin_functions[] = {
    JS_FS("addURI",           js_addURI,      2, 0, 0),
    JS_FS("addSearcher",      js_addSearcher, 3, 0, 0),
    JS_FS("addHTTPAuth",      js_addHTTPAuth, 2, 0, 0),
    JS_FS("forceUnload",      js_forceUnload, 0, 0, 0),
    JS_FS("createSettings",   js_createSettings, 2, 0, 0),
    JS_FS("createStore",   js_createStore, 1, 0, 0),
    JS_FS("createService",    js_createService, 4, 0, 0),
    JS_FS("getAuthCredentials",  js_getAuthCredentials, 3, 0, 0),
    JS_FS_END
};


/**
 *
 */
void
js_plugin_unload(const char *id)
{
  JSContext *cx;
  js_plugin_t *jsp;

  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    if(!strcmp(jsp->jsp_id, id))
      break;

  if(jsp == NULL)
    return;

  fa_unreference(jsp->jsp_ref);
    
  cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  js_plugin_unload0(cx, jsp);

  JS_EndRequest(cx);
  JS_GC(cx);
  JS_DestroyContext(cx);
}


/**
 *
 */
int
js_plugin_load(const char *id, const char *url, char *errbuf, size_t errlen)
{
  char *sbuf;
  struct fa_stat fs;
  JSContext *cx;
  js_plugin_t *jsp;
  JSObject *pobj, *gobj;
  JSScript *s;
  char path[PATH_MAX];
  jsval val;
  fa_handle_t *ref;
  
  ref = fa_reference(url);

  if((sbuf = fa_quickload(url, &fs, NULL, errbuf, errlen)) == NULL) {
    fa_unreference(ref);
    return -1;
  }

  cx = js_newctx(err_reporter);
  JS_BeginRequest(cx);

  /* Remove any plugin with same URL */
  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    if(!strcmp(jsp->jsp_id, id))
      break;
  if(jsp != NULL)
    js_plugin_unload0(cx, jsp);

  jsp = calloc(1, sizeof(js_plugin_t));
  jsp->jsp_url = strdup(url);
  jsp->jsp_id  = strdup(id);
  jsp->jsp_ref = ref;
  
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


  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, url));
  JS_SetProperty(cx, pobj, "url", &val);

  if(!fa_parent(path, sizeof(path), url)) {
    val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, path));
    JS_SetProperty(cx, pobj, "path", &val);
  }

  JS_DefineProperty(cx, pobj, "URIRouting", BOOLEAN_TO_JSVAL(1),
		    NULL, jsp_setEnableURIRoute, JSPROP_PERMANENT);
  jsp->jsp_enable_uri_routing = 1;

  JS_DefineProperty(cx, pobj, "search", BOOLEAN_TO_JSVAL(1),
		    NULL, jsp_setEnableSearch, JSPROP_PERMANENT);
  jsp->jsp_enable_search = 1;

  jsp->jsp_protect_object = 1;

  s = JS_CompileScript(cx, pobj, sbuf, fs.fs_size, url, 1);
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
static int
js_init(void)
{
  JSContext *cx;
  jsval val;

  JS_SetCStringsAreUTF8();

  runtime = JS_NewRuntime(0x1000000);

  cx = js_newctx(err_reporter);

  JS_BeginRequest(cx);

  showtimeobj = JS_NewObject(cx, &showtime_class, NULL, NULL);
  JS_DefineFunctions(cx, showtimeobj, showtime_functions);

  val = INT_TO_JSVAL(showtime_get_version_int());
  JS_SetProperty(cx, showtimeobj, "currentVersionInt", &val);

  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, htsversion));
  JS_SetProperty(cx, showtimeobj, "currentVersionString", &val);


  JSFunction *fn = JS_DefineFunction(cx, showtimeobj, "RichText",
				     js_RichText, 1, 0);
  RichText = JS_GetFunctionObject(fn);
	     
  JS_AddNamedRoot(cx, &showtimeobj, "showtime");

  JS_EndRequest(cx);
  JS_DestroyContext(cx);

  return 0;
}



/**
 *
 */
static void
js_fini(void)
{
  js_plugin_t *jsp, *n;
  JSContext *cx;

  cx = js_newctx(err_reporter);
  JS_BeginRequest(cx);

  for(jsp = LIST_FIRST(&js_plugins); jsp != NULL; jsp = n) {
    n = LIST_NEXT(jsp, jsp_link);
    js_plugin_unload0(cx, jsp);
  }

  JS_RemoveRoot(cx, &showtimeobj);

  JS_EndRequest(cx);
  JS_GC(cx);
  JS_DestroyContext(cx);

  JS_DestroyRuntime(runtime);
  JS_ShutDown();
}





/**
 *
 */
static backend_t be_js = {
  .be_init = js_init,
  .be_fini = js_fini,
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = js_backend_open,
  .be_search = js_backend_search,
};

BE_REGISTER(js);
