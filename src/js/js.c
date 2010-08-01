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

#include "js.h"

#include "backend/backend.h"
#include "misc/string.h"

static JSRuntime *runtime;
static  JSObject *global;
static  JSObject *showtimeobj;

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

  JS_SetOptions(cx, JSOPTION_STRICT | JSOPTION_WERROR);
  JS_SetErrorReporter(cx, err_reporter);
#ifdef JS_GC_ZEAL
  //  JS_SetGCZeal(cx, 2);
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
static JSFunctionSpec showtime_functions[] = {
    JS_FS("trace",            js_trace,    1, 0, 0),
    JS_FS("print",            js_print,    1, 0, 0),
    JS_FS("httpRequest",      js_httpRequest, 4, 0, 0),
    JS_FS("readFile",         js_readFile, 1, 0, 0),
    JS_FS("addURI",           js_addURI, 2, 0, 0),
    JS_FS("queryStringSplit", js_queryStringSplit, 1, 0, 0),
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
  jsval result;
  JSScript *s;
  JSObject *srcobj;

  JS_SetCStringsAreUTF8();

  runtime = JS_NewRuntime(0x100000); 
  cx = js_newctx();

  JS_BeginRequest(cx);
  
  global = JS_NewObject(cx, &global_class, NULL, NULL);
  JS_InitStandardClasses(cx, global);

  showtimeobj = JS_DefineObject(cx, global, "showtime",
				&showtime_class, NULL, 0);

  JS_DefineFunctions(cx, showtimeobj, showtime_functions);



  s = JS_CompileFile(cx, global, "/home/andoma/showtime/test.js");
  if(s != NULL) {
    srcobj = JS_NewScriptObject(cx, s);

    JS_AddNamedRoot(cx, &srcobj, "script");
    JS_ExecuteScript(cx, global, s, &result);
    JS_RemoveRoot(cx, &srcobj);
  }

  JS_EndRequest(cx);

  JS_DestroyContext(cx);

  return 0;
}




/**
 *
 */
static backend_t be_js = {
  .be_init = js_init,
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = js_page_open,
};

BE_REGISTER(js);
