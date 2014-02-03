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
#include "fileaccess/fa_proto.h"

typedef struct js_faprovider {
  fa_protocol_t fap;

  LIST_ENTRY(js_faprovider) jf_plugin_link;

  jsval jf_obj;
  char *jf_id;
} js_faprovider_t;


/**
 *
 */
static void
jf_fini(fa_protocol_t *fap)
{
  js_faprovider_t *jf = (js_faprovider_t *)fap;
  free(jf->jf_id);
  free(jf);
}




/**
 *
 */
static fa_handle_t *
jf_open(struct fa_protocol *fap, const char *url,
        char *errbuf, size_t errsize, int flags,
        struct fa_open_extra *foe)
{
  js_faprovider_t *jf = (js_faprovider_t *)fap;
  jsval openfn;
  const char *redirect = NULL;
  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  JS_GetProperty(cx, JSVAL_TO_OBJECT(jf->jf_obj), "open", &openfn);

  const char *mode;

  if(flags & FA_WRITE)
    mode = flags & FA_APPEND ? "write-append" : "write";
  else
    mode = "read";

  JS_EnterLocalRootScope(cx);

  void *mark;
  jsval *argv, result;
  argv = JS_PushArguments(cx, &mark, "ss", url, mode);
  int r = JS_CallFunctionValue(cx, NULL, openfn, 2, argv, &result);
  JS_PopArguments(cx, mark);

  if(!r) {
    snprintf(errbuf, errsize, "Unable to open file");
    goto bad;
  }

  if(JSVAL_IS_STRING(result)) {
    redirect = mystrdupa(JS_GetStringBytes(JSVAL_TO_STRING(result)));
    JS_LeaveLocalRootScope(cx);
    JS_DestroyContext(cx);
    return fa_open_ex(redirect, errbuf, errsize, flags, foe);
  }

  snprintf(errbuf, errsize, "Unable to open file, unsupported retaval");

 bad:
  JS_LeaveLocalRootScope(cx);
  JS_DestroyContext(cx);
  return NULL;
}


/**
 *
 */
static int
jf_stat(struct fa_protocol *fap, const char *url, struct fa_stat *buf,
        char *errbuf, size_t errsize, int non_interactive)
{
  js_faprovider_t *jf = (js_faprovider_t *)fap;
  jsval statfn;
  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  JS_GetProperty(cx, JSVAL_TO_OBJECT(jf->jf_obj), "stat", &statfn);

  JS_EnterLocalRootScope(cx);

  void *mark;
  jsval *argv, result;
  argv = JS_PushArguments(cx, &mark, "s", url);
  int r = JS_CallFunctionValue(cx, NULL, statfn, 1, argv, &result);
  JS_PopArguments(cx, mark);
  if(!r) {
    snprintf(errbuf, errsize, "Unable to stat file");
    r = -1;
    goto done;
  }


  if(JSVAL_IS_OBJECT(result) && result) {
    JSObject *o = JSVAL_TO_OBJECT(result);

    buf->fs_size = js_prop_int64_or_default(cx, o, "size", 0);
    buf->fs_type = js_prop_bool(cx, o, "isDir", 0) ?
      CONTENT_DIR : CONTENT_FILE;
    buf->fs_mtime = js_prop_int64_or_default(cx, o, "mtime", 0);

    r = 0;
  } else {
    snprintf(errbuf, errsize, "No such file or directory");
    r = -1;
  }

 done:
  JS_LeaveLocalRootScope(cx);
  JS_DestroyContext(cx);
  return r;
}


/**
 *
 */
static int
jf_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
           char *errbuf, size_t errsize)
{
  js_faprovider_t *jf = (js_faprovider_t *)fap;
  jsval scandirfn;
  JSContext *cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  JS_GetProperty(cx, JSVAL_TO_OBJECT(jf->jf_obj), "scandir", &scandirfn);

  JS_EnterLocalRootScope(cx);

  void *mark;
  jsval *argv, result;
  argv = JS_PushArguments(cx, &mark, "s", url);
  int r = JS_CallFunctionValue(cx, NULL, scandirfn, 1, argv, &result);
  JS_PopArguments(cx, mark);
  if(!r) {
    snprintf(errbuf, errsize, "Unable to stat file");
    r = -1;
    goto done;
  }

  if(result && JSVAL_IS_OBJECT(result)) {
    JSIdArray *ida;
    JSObject *obj = js_prop_obj(cx, JSVAL_TO_OBJECT(result), "items");

    if((ida = JS_Enumerate(cx, obj)) != NULL) {
      for(int i = 0; i < ida->length; i++) {
        jsval name, value;
        if(!JS_IdToValue(cx, ida->vector[i], &name) ||
           !JSVAL_IS_INT(name) ||
           !JS_GetElement(cx, obj, JSVAL_TO_INT(name), &value) ||
           !JSVAL_IS_OBJECT(value))
          continue;

        JSObject *o = JSVAL_TO_OBJECT(value);

        rstr_t *url  = js_prop_rstr(cx, o, "url");
        rstr_t *filename = js_prop_rstr(cx, o, "name");
        int type = js_prop_bool(cx, o, "isDir", 0) ? CONTENT_DIR : CONTENT_FILE;

        if(url != NULL && filename != NULL) {
          fa_dir_entry_t *fde = fa_dir_add(fd, rstr_get(url),
                                           rstr_get(filename), type);
          printf("%s type = %d\n", rstr_get(url), type);
          if(fde != NULL && type == CONTENT_FILE) {
            int64_t size = js_prop_int64_or_default(cx, o, "size", -1);
            int mtime = js_prop_int64_or_default(cx, o, "mtime", 0);

            if(size >= 0 && mtime) {
              fde->fde_stat.fs_size = size;
              fde->fde_stat.fs_type = type;
              fde->fde_stat.fs_mtime = mtime;
              fde->fde_statdone = 1;
            }
          }
        }
        rstr_release(url);
        rstr_release(filename);
      }
    }
    JS_DestroyIdArray(cx, ida);
    r = 0;
  } else {
    snprintf(errbuf, errsize, "No such file or directory");
    r = -1;
  }

 done:
  JS_LeaveLocalRootScope(cx);
  JS_DestroyContext(cx);
  return r;
}


/**
 *
 */
JSBool
js_addfaprovider(JSContext *cx, JSObject *obj, uintN argc,
		  jsval *argv, jsval *rval)
{
  js_faprovider_t *jf;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  jsval v;
  const char *name;
  fa_protocol_t *fap;

  if (!JS_ConvertArguments(cx, argc, argv, "sv", &name, &v))
    return JS_FALSE;

  jf = calloc(1, sizeof(js_faprovider_t));
  fap = &jf->fap;
  jf->jf_obj = v;
  jf->jf_id = strdup(name);
  JS_AddNamedRoot(cx, &jf->jf_obj, "faprovider");

  LIST_INSERT_HEAD(&jsp->jsp_faproviders, jf, jf_plugin_link);

  fap->fap_fini = jf_fini;
  fap->fap_name = jf->jf_id;
  fap->fap_open = jf_open;
  fap->fap_stat = jf_stat;
  fap->fap_scan = jf_scandir;
  fileaccess_register_dynamic(fap);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
void
js_faprovider_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_faprovider_t *jf;

  while((jf = LIST_FIRST(&jsp->jsp_faproviders)) != NULL) {
    fileaccess_unregister_dynamic(&jf->fap);
    LIST_REMOVE(jf, jf_plugin_link);
    jf->jf_obj = JSVAL_NULL;
    JS_RemoveRoot(cx, &jf->jf_obj);
  }
}


