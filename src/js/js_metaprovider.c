/*
 *  JSAPI <-> Metadata providers
 *  Copyright (C) 2013 Andreas Öman
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
#include <assert.h>
#include <string.h>

#include "js.h"
#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "backend/search.h"
#include "navigator.h"
#include "misc/str.h"
#include "event.h"
#include "htsmsg/htsmsg_json.h"
#include "subtitles/sub_scanner.h"
#include "media.h"

typedef struct js_subprovider {
  int sp_refcnt;

  LIST_ENTRY(js_subprovider) sp_global_link;
  LIST_ENTRY(js_subprovider) sp_plugin_link;

  jsval sp_func;

} js_subprovider_t;

static hts_mutex_t meta_mutex; // protects global lists (should be elsewhere)
static struct js_subprovider_list js_subproviders;


/**
 *
 */
void
js_metaprovider_init(void)
{
  hts_mutex_init(&meta_mutex);
}


/**
 *
 */
static void
js_subprovider_release(JSContext *cx, js_subprovider_t *sp)
{
  if(atomic_add(&sp->sp_refcnt, -1) != 1)
    return;
  JS_RemoveRoot(cx, &sp->sp_func);
  free(sp);
}


/**
 *
 */
JSBool 
js_addsubprovider(JSContext *cx, JSObject *obj, uintN argc, 
		  jsval *argv, jsval *rval)
{
  js_subprovider_t *sp;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[0]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }
  
  sp = calloc(1, sizeof(js_subprovider_t));

  hts_mutex_lock(&meta_mutex);
  LIST_INSERT_HEAD(&js_subproviders, sp, sp_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_subproviders, sp, sp_plugin_link);

  sp->sp_func = argv[0];
  sp->sp_refcnt = 1;
  JS_AddNamedRoot(cx, &sp->sp_func, "subprovider");
  hts_mutex_unlock(&meta_mutex);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
void
js_subprovider_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_subprovider_t *sp;
  hts_mutex_lock(&meta_mutex);

  while((sp = LIST_FIRST(&jsp->jsp_subproviders)) != NULL) {
    LIST_REMOVE(sp, sp_global_link);
    LIST_REMOVE(sp, sp_plugin_link);
    js_subprovider_release(cx, sp);
  }
  hts_mutex_unlock(&meta_mutex);
}



/**
 *
 */
static JSBool
js_addSubtitle(JSContext *cx, JSObject *obj,
	       uintN argc, jsval *argv, jsval *rval)
{
  sub_scanner_t *ss = JS_GetPrivate(cx, obj);
  const char *url;
  const char *title;
  const char *language = NULL;
  const char *format = NULL;
  const char *source = NULL;
  int score = 0;

  if(!JS_ConvertArguments(cx, argc, argv, "ss/sssu",
			  &url, &title, &language, &format, &source, &score))
    return JS_FALSE;
  
  hts_mutex_lock(&ss->ss_mutex);
  mp_add_track(ss->ss_proproot,
	       title, url, format, NULL, language, source, NULL, score);

  hts_mutex_unlock(&ss->ss_mutex);
  return JS_TRUE;
}

     

/**
 *
 */
static JSFunctionSpec sub_functions[] = {
    JS_FS("addSubtitle",     js_addSubtitle, 6, 0, 0),
    JS_FS_END
};


static void
subreq_finalize(JSContext *cx, JSObject *obj)
{
  sub_scanner_t *ss = JS_GetPrivate(cx, obj);
  sub_scanner_release(ss);
}

/**
 *
 */
static JSClass subreq_class = {
  "subreq", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, subreq_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
void
js_sub_query(sub_scanner_t *ss)
{
  js_subprovider_t *sp, **v;

  hts_mutex_lock(&meta_mutex);

  int cnt = 0;
  LIST_FOREACH(sp, &js_subproviders, sp_global_link)
    cnt++;

  v = alloca(cnt * sizeof(js_subprovider_t *));
  cnt = 0;
  LIST_FOREACH(sp, &js_subproviders, sp_global_link) {
    v[cnt++] = sp;
    atomic_add(&sp->sp_refcnt, 1);
  }

  hts_mutex_unlock(&meta_mutex);


  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);
  
  sub_scanner_retain(ss);
  JSObject *obj = JS_NewObject(cx, &subreq_class, NULL, NULL);

  JS_AddNamedRoot(cx, &obj, "searcher");
  JS_SetPrivate(cx, obj, ss);
  
  JS_DefineFunctions(cx, obj, sub_functions);

  js_set_prop_rstr(cx, obj, "title", ss->ss_title);
  js_set_prop_rstr(cx, obj, "imdb", ss->ss_imdbid);

  if(ss->ss_season > 0)
    js_set_prop_int(cx, obj, "season", ss->ss_season);

  if(ss->ss_year > 0)
    js_set_prop_int(cx, obj, "year", ss->ss_year);

  if(ss->ss_episode > 0)
    js_set_prop_int(cx, obj, "episode", ss->ss_episode);

  if(ss->ss_fsize > 0)
    js_set_prop_dbl(cx, obj, "filesize", ss->ss_fsize);
  
  if(ss->ss_hash_valid) {
    char str[64];
    snprintf(str, sizeof(str), "%016" PRIx64, ss->ss_opensub_hash);
    js_set_prop_str(cx, obj, "opensubhash", str);

    bin2hex(str, sizeof(str), ss->ss_subdbhash, 16);
    js_set_prop_str(cx, obj, "subdbhash", str);
  }

  if(ss->ss_duration > 0)
    js_set_prop_int(cx, obj, "duration", ss->ss_duration);

  int i;
  for(i = 0; i < cnt; i++) {
    js_subprovider_t *sp = v[i];
    jsval result;
    void *mark;
    int argc = 1;
    jsval *argv = JS_PushArguments(cx, &mark, "o", obj);
    JS_CallFunctionValue(cx, NULL, sp->sp_func, argc, argv, &result);
    JS_PopArguments(cx, mark);
    
    js_subprovider_release(cx, sp);
    if(ss->ss_stop)
      break;

  }
  JS_RemoveRoot(cx, &obj);
  JS_DestroyContext(cx);
}
