/*
 *  JSAPI <-> I/O
 *  Copyright (C) 2010 Andreas Öman
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

#include "fileaccess/fileaccess.h"
#include "misc/string.h"

JSBool 
js_httpRequest(JSContext *cx, JSObject *obj, uintN argc,
	       jsval *argv, jsval *rval)
{
  const char *url;
  JSObject *argobj = NULL;
  const char *postdata = NULL;
  char **httpargs = NULL;
  int i;
  char errbuf[256];
  char *result;
  size_t resultsize;


  if(!JS_ConvertArguments(cx, argc, argv, "s/os", &url, &argobj, &postdata))
    return JS_FALSE;

  if(argobj != NULL) {
    JSIdArray *ida;
    int j = 0;
    if((ida = JS_Enumerate(cx, argobj)) == NULL)
      return JS_FALSE;

    httpargs = malloc(((ida->length * 2) + 1) * sizeof(char *));

    for(i = 0; i < ida->length; i++) {
      jsval name, value;
      if(!JS_IdToValue(cx, ida->vector[i], &name))
	continue;

      if(!JSVAL_IS_STRING(name))
	continue;

      if(!JS_GetProperty(cx, argobj, JS_GetStringBytes(JSVAL_TO_STRING(name)),
			 &value))
	continue;

      httpargs[j++] = strdup(JS_GetStringBytes(JSVAL_TO_STRING(name)));
      httpargs[j++] = strdup(JS_GetStringBytes(JS_ValueToString(cx, value)));
    }
    httpargs[j++] = NULL;
    
    JS_DestroyIdArray(cx, ida);
  }

  int n = http_request(url, (const char **)httpargs, 
		       &result, &resultsize, errbuf, sizeof(errbuf),
		       NULL, NULL, 0);

  if(httpargs != NULL)
    strvec_free(httpargs);

  if(n) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  *rval = STRING_TO_JSVAL(JS_NewString(cx, result, resultsize));
  return JS_TRUE;
}


JSBool 
js_readFile(JSContext *cx, JSObject *obj, uintN argc,
	       jsval *argv, jsval *rval)
{
  const char *url;
  char errbuf[256];
  void *result;
  size_t resultsize;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  result = fa_quickload(url, &resultsize, NULL, errbuf, sizeof(errbuf));

  if(result == NULL) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  *rval = STRING_TO_JSVAL(JS_NewString(cx, result, resultsize));
  return JS_TRUE;
}
