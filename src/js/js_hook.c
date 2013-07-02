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
#include "media.h"

typedef struct js_hook {

  int jh_refcnt;

  LIST_ENTRY(js_hook) jh_global_link;
  LIST_ENTRY(js_hook) jh_plugin_link;

  jsval jh_func;

  int jh_type;

  prop_t *jh_prop_root;
  prop_sub_t *jh_prop_sub;

} js_hook_t;

static hts_mutex_t hook_mutex;
static struct js_hook_list js_hooks;


/**
 *
 */
void
js_hook_init(void)
{
  hts_mutex_init(&hook_mutex);
}


/**
 *
 */
static void
js_hook_release(JSContext *cx, js_hook_t *jh)
{
  if(atomic_add(&jh->jh_refcnt, -1) != 1)
    return;
  JS_RemoveRoot(cx, &jh->jh_func);
  free(jh);
}


/**
 *
 */
static void
hook_eventsub(void *opaque, prop_event_t event, ...)
{
  js_hook_t *jh = opaque;
  va_list ap;
  event_t *e;

  va_start(ap, event);

  switch(event) {
  default:
    break;

  case PROP_DESTROYED:
    prop_unsubscribe(jh->jh_prop_sub);
    js_hook_release(js_global_cx, jh);
    break;

  case PROP_EXT_EVENT:
    e = va_arg(ap, event_t *);
    if(event_is_type(e, EVENT_PROPREF)) {
      event_prop_t *ep = (event_prop_t *)e;

      JSContext *cx = js_global_cx;

      if(JS_EnterLocalRootScope(cx)) {
        JSObject *o = js_object_from_prop(cx, ep->p);
        JSObject *nav = e->e_nav ? js_nav_create(cx, e->e_nav) : NULL;

        jsval result;
        jsval argv[2];

        argv[0] = OBJECT_TO_JSVAL(o);
        argv[1] = nav ? OBJECT_TO_JSVAL(nav) : 0;

        JS_CallFunctionValue(cx, NULL, jh->jh_func, nav ? 2 : 1, argv, &result);

        JS_LeaveLocalRootScope(cx);
      }
    }
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
js_hook_destroy(JSContext *cx, js_hook_t *jh)
{
  LIST_REMOVE(jh, jh_global_link);
  LIST_REMOVE(jh, jh_plugin_link);

  prop_destroy(jh->jh_prop_root);
  js_hook_release(cx, jh);
}


/**
 *
 */
JSBool
js_addItemHook(JSContext *cx, JSObject *obj, uintN argc,
               jsval *argv, jsval *rval)
{
  js_hook_t *jh;
  JSObject *o;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  jsval fn;

  if(!JS_ConvertArguments(cx, argc, argv, "o", &o))
    return JS_FALSE;

  if(js_prop_fn(cx, o, "handler", &fn)) {
    JS_ReportError(cx, "Missing or invalid 'handler' in argument object");
    return JS_FALSE;
  }

  prop_t *p = prop_create_root(NULL);
  prop_t *m = prop_create(p, "metadata");
  prop_set(p, "itemtype", PROP_ADOPT_RSTRING, js_prop_rstr(cx, o, "itemtype"));
  prop_set(m, "title", PROP_ADOPT_RSTRING, js_prop_rstr(cx, o, "title"));
  prop_set(m, "icon",  PROP_ADOPT_RSTRING, js_prop_rstr(cx, o, "icon"));

  jh = calloc(1, sizeof(js_hook_t));

  jh->jh_prop_root = p;

  jh->jh_func = fn;
  jh->jh_refcnt = 2;
  JS_AddNamedRoot(cx, &jh->jh_func, "hook");

  hts_mutex_lock(&hook_mutex);
  LIST_INSERT_HEAD(&js_hooks,       jh, jh_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_hooks, jh, jh_plugin_link);
  hts_mutex_unlock(&hook_mutex);

  jh->jh_prop_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_NAME("self", "eventSink"),
                   PROP_TAG_CALLBACK, hook_eventsub, jh,
                   PROP_TAG_COURIER, js_global_pc,
                   PROP_TAG_NAMED_ROOT, jh->jh_prop_root, "self",
                   NULL);

  if(prop_set_parent(p, prop_create(prop_get_global(), "itemhooks")))
    abort();

  *rval = JSVAL_VOID;
  return JS_TRUE;
}



/**
 *
 */
void
js_hook_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_hook_t *sp;
  hts_mutex_lock(&hook_mutex);

  while((sp = LIST_FIRST(&jsp->jsp_hooks)) != NULL) {
    js_hook_destroy(cx, sp);
  }
  hts_mutex_unlock(&hook_mutex);
}

