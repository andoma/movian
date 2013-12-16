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


#include "event.h"


/**
 *
 */
typedef struct js_event_handler {
  LIST_ENTRY(js_event_handler) jeh_link;
  char *jeh_filter;
  jsval jeh_function;
} js_event_handler_t;


/**
 *
 */
void
js_event_destroy_handlers(JSContext *cx, struct js_event_handler_list *list)
{
  js_event_handler_t *jeh;

  while((jeh = LIST_FIRST(list)) != NULL) {
    LIST_REMOVE(jeh, jeh_link);
    JS_RemoveRoot(cx, &jeh->jeh_function);
    free(jeh->jeh_filter);
    free(jeh);
  }
}


/**
 *
 */
void
js_event_handler_create(JSContext *cx, struct js_event_handler_list *list,
			const char *filter, jsval fun)
{
  js_event_handler_t *jeh;
  jeh = malloc(sizeof(js_event_handler_t));
  jeh->jeh_filter = filter ? strdup(filter) : NULL;
  jeh->jeh_function = fun;
  JS_AddNamedRoot(cx, &jeh->jeh_function, "eventhandler");
  LIST_INSERT_HEAD(list, jeh, jeh_link);
}


/**
 *
 */
static void
js_event_dispatch_action(JSContext *cx, struct js_event_handler_list *list,
			 const char *action, JSObject *this)
{
  js_event_handler_t *jeh;
  jsval result;
  if(action == NULL)
    return;

  LIST_FOREACH(jeh, list, jeh_link) {
    if(jeh->jeh_filter == NULL || !strcasecmp(jeh->jeh_filter, action)) {

      void *mark;
      jsval *argv;
      argv = JS_PushArguments(cx, &mark, "s", action);
      JS_CallFunctionValue(cx, this, jeh->jeh_function, 1, argv, &result);
      JS_PopArguments(cx, mark);
      return;
    }
  }
}


/**
 *
 */
void
js_event_dispatch(JSContext *cx, struct js_event_handler_list *list,
		  event_t *e, JSObject *this)
{
  if(event_is_type(e, EVENT_ACTION_VECTOR)) {
    event_action_vector_t *eav = (event_action_vector_t *)e;
    int i;
    for(i = 0; i < eav->num; i++)
      js_event_dispatch_action(cx, list,
                               action_code2str(eav->actions[i]), this);
  } else if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
    js_event_dispatch_action(cx, list, e->e_payload, this);
  }
}


/**
 *
 */
JSBool 
js_onEvent(JSContext *cx, JSObject *obj,
	   uintN argc, jsval *argv, jsval *rval)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  js_event_handler_create(cx, &jsp->jsp_event_handlers,
			  JSVAL_IS_STRING(argv[0]) ? 
			  JS_GetStringBytes(JS_ValueToString(cx,
							     argv[0])) : 
			  NULL, argv[1]);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}
