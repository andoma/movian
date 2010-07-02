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

#include "ext/spidermonkey/jsapi.h"
#include "js.h"

static JSRuntime *runtime;

static JSClass global_class = {
  "global",0,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub
};


void
js_init(void)
{
  JSObject *global;
  JSContext *cx;

  runtime = JS_NewRuntime(0x100000); 
  
  cx = JS_NewContext(runtime, 0x1000);
  
  global = JS_NewObject(cx, &global_class, NULL, NULL);
  JS_InitStandardClasses(cx, global);
  

  const char *script = "42 * 2;";
  jsval rval;
  JSString *str;
  JSBool ok;

  ok = JS_EvaluateScript(cx, global, script, strlen(script),
			 "inline", 1, &rval);
  str = JS_ValueToString(cx, rval);
  printf("script result: %s\n", JS_GetStringBytes(str));  
}
