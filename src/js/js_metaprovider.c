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
#include "subtitles/subtitles.h"
#include "media.h"
#include "usage.h"

typedef struct js_subprovider {
  subtitle_provider_t super;

  atomic_t sp_refcnt;
  LIST_ENTRY(js_subprovider) sp_plugin_link;
  jsval sp_func;

  prop_t *sp_title;

  js_plugin_t *sp_jsp;

} js_subprovider_t;


typedef struct js_sub_job {
  int jsj_score;
  int jsj_autosel;
  sub_scanner_t *jsj_ss;
} js_sub_job_t;

static void js_sub_query(subtitle_provider_t *SP, sub_scanner_t *ss,
                         int score, int autosel);


/**
 *
 */
static void
js_sub_retain(subtitle_provider_t *SP)
{
  js_subprovider_t *sp = (js_subprovider_t *)SP;
  atomic_inc(&sp->sp_refcnt);
}


/**
 *
 */
static void
js_subprovider_release(JSContext *cx, js_subprovider_t *sp)
{
  if(atomic_dec(&sp->sp_refcnt))
    return;
  JS_RemoveRoot(cx, &sp->sp_func);
  prop_destroy(sp->sp_title);
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

  sp->super.sp_query        = js_sub_query;
  sp->super.sp_retain       = js_sub_retain;

  sp->sp_title = prop_create_root(NULL);
  prop_set_string(sp->sp_title, jsp->jsp_id);

  subtitle_provider_register(&sp->super,
                             jsp->jsp_id, sp->sp_title,
                             0, "plugin", 1, 1);

  sp->sp_jsp = jsp;
  LIST_INSERT_HEAD(&jsp->jsp_subproviders, sp, sp_plugin_link);

  sp->sp_func = argv[0];
  atomic_set(&sp->sp_refcnt, 1);
  JS_AddNamedRoot(cx, &sp->sp_func, "subprovider");

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

  while((sp = LIST_FIRST(&jsp->jsp_subproviders)) != NULL) {
    LIST_REMOVE(sp, sp_plugin_link);
    sp->sp_jsp = NULL;
    subtitle_provider_unregister(&sp->super);
    js_subprovider_release(cx, sp);
  }
}



/**
 *
 */
static JSBool
js_addSubtitle(JSContext *cx, JSObject *obj,
	       uintN argc, jsval *argv, jsval *rval)
{
  js_sub_job_t *jsj = JS_GetPrivate(cx, obj);
  const char *url;
  const char *title;
  const char *language = NULL;
  const char *format = NULL;
  const char *source = NULL;
  int score = 0;

  if(!JS_ConvertArguments(cx, argc, argv, "ss/sssu",
			  &url, &title, &language, &format, &source, &score))
    return JS_FALSE;

  sub_scanner_t *ss = jsj->jsj_ss;
  hts_mutex_lock(&ss->ss_mutex);
  mp_add_track(ss->ss_proproot,
	       title, url, format, NULL, language, source, NULL,
               jsj->jsj_score + score, jsj->jsj_autosel);

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
  js_sub_job_t *jsj = JS_GetPrivate(cx, obj);
  sub_scanner_release(jsj->jsj_ss);
  free(jsj);
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
static void
js_sub_query(subtitle_provider_t *SP, sub_scanner_t *ss, int score,
	     int autosel)
{
  js_subprovider_t *sp = (js_subprovider_t *)SP;

  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  if(ss != NULL) {
    JSObject *obj = JS_NewObject(cx, &subreq_class, NULL, NULL);

    JS_AddNamedRoot(cx, &obj, "subscanner");

    if(sp->sp_jsp != NULL)
      usage_inc_plugin_counter(sp->sp_jsp->jsp_id, "subsearch", 1);

    js_sub_job_t *jsj = malloc(sizeof(js_sub_job_t));
    jsj->jsj_ss = ss;
    jsj->jsj_score = score;
    jsj->jsj_autosel = autosel;
    sub_scanner_retain(ss);
    JS_SetPrivate(cx, obj, jsj);

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

    jsval result;
    jsval arg = OBJECT_TO_JSVAL(obj);
    JS_CallFunctionValue(cx, NULL, sp->sp_func, 1, &arg, &result);

    JS_RemoveRoot(cx, &obj);
  }
  js_subprovider_release(cx, sp);
  JS_DestroyContext(cx);
}
