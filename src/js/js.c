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
#include <string.h>

#include "js.h"

#include "backend/backend.h"
#include "navigator.h"

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
static JSContext *
newctx(void)
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
typedef struct jsbackend {
  backend_t jsb_be;
  jsval jsb_object;

} jsbackend_t;


/**
 *
 */
static int
js_canhandle(backend_t *be, const char *url)
{
  jsbackend_t *jsb = (jsbackend_t *)be;
  JSObject *o = JSVAL_TO_OBJECT(jsb->jsb_object);
  JSContext *cx;
  jsval canHandle, *argv, result;
  void *mark;
  uint32_t score = 0;

  cx = newctx();

  JS_BeginRequest(cx);

  if(JS_GetProperty(cx, o, "canHandle",  &canHandle)) {
    if((argv = JS_PushArguments(cx, &mark, "s", url)) != NULL) {

      if(JS_CallFunctionValue(cx, o, canHandle, 1, argv, &result))
	JS_ValueToECMAUint32(cx, result, &score);
    }
    JS_PopArguments(cx, mark);
  }

  JS_EndRequest(cx);
  JS_DestroyContext(cx);
  return score;
}


/**
 *
 */
static void
js_open_invoke(JSContext *cx, const char *url, prop_t *root, jsbackend_t *jsb)
{
  JSObject *this = JSVAL_TO_OBJECT(jsb->jsb_object);
  jsval open, *argv, result;
  void *mark;
  uint32_t score;

  if(!JS_GetProperty(cx, this, "open",  &open))
    return;

  JSObject *p = js_page_object(cx, root);

  if((argv = JS_PushArguments(cx, &mark, "so", url, p)) == NULL)
    return;

  if(!JS_CallFunctionValue(cx, this, open, 2, argv, &result) ||
     !JS_ValueToECMAUint32(cx, result, &score))
    score = 0;
  
  prop_print_tree(root, 0);

  JS_PopArguments(cx, mark);
}


/**
 *
 */
struct js_open_args {
  char *url;
  prop_t *root;
  jsbackend_t *jsb;
};


/**
 *
 */
static void *
js_open_trampoline(void *arg)
{
  struct js_open_args *joa = arg;
  
  JSContext *cx = newctx();
  JS_BeginRequest(cx);

  js_open_invoke(cx, joa->url, joa->root, joa->jsb);

  JS_EndRequest(cx);
  JS_DestroyContext(cx);

  free(joa->url);
  prop_ref_dec(joa->root);
  free(joa);
  return NULL;
}



/**
 *
 */
static nav_page_t *
js_open(backend_t *be, struct navigator *nav, 
	const char *url, const char *view,
	char *errbuf, size_t errlen)
{
  jsbackend_t *jsb = (jsbackend_t *)be;
  struct js_open_args *joa = malloc(sizeof(struct js_open_args));
  nav_page_t *np;

  np = nav_page_create(nav, url, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  joa->url = strdup(url);
  joa->root = np->np_prop_root;
  joa->jsb = jsb;
  prop_ref_inc(np->np_prop_root);

  hts_thread_create_detached("jsapi", js_open_trampoline, joa);
			     
  prop_set_int(prop_create(prop_create(np->np_prop_root, "model"),
			   "loading"), 1);
  return np;
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
js_registerBackend(JSContext *cx, JSObject *obj, uintN argc, 
		     jsval *argv, jsval *rval)
{
  if(!JSVAL_IS_OBJECT(argv[0])) {
    JS_ReportError(cx, "Argument is not an oject");
    return JS_FALSE;
  }
    
  jsbackend_t *jsb = calloc(1, sizeof(jsbackend_t));
  backend_t *be = &jsb->jsb_be;

  jsb->jsb_object = argv[0];

  JS_AddNamedRoot(cx, &jsb->jsb_object, "backend");

  be->be_canhandle = js_canhandle;
  be->be_open      = js_open;
  
  backend_register(be);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec showtime_functions[] = {
    JS_FS("trace",           js_trace,    1, 0, 0),
    JS_FS("registerBackend", js_registerBackend, 1, 0, 0),
    JS_FS("httpRequest",     js_httpRequest, 4, 0, 0),
    JS_FS("readFile",        js_readFile, 1, 0, 0),
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
  cx = newctx();

  JS_BeginRequest(cx);
  
  global = JS_NewObject(cx, &global_class, NULL, NULL);
  JS_InitStandardClasses(cx, global);

  showtimeobj = JS_DefineObject(cx, global, "showtime",
				&showtime_class, NULL, 0);

  JS_DefineFunctions(cx, showtimeobj, showtime_functions);



  s = JS_CompileFile(cx, global, "/home/andoma/showtime/test.js");

  srcobj = JS_NewScriptObject(cx, s);

  JS_AddNamedRoot(cx, &srcobj, "script");
  JS_ExecuteScript(cx, global, s, &result);
  JS_RemoveRoot(cx, &srcobj);

  JS_EndRequest(cx);

  JS_DestroyContext(cx);

  return 0;
}




/**
 *
 */
static backend_t be_js = {
  .be_init = js_init,
};

BE_REGISTER(js);
