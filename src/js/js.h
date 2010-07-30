#ifndef JS_H__
#define JS_H__


#include "showtime.h"
#include "prop/prop.h"
#include "ext/spidermonkey/jsapi.h"

JSObject *js_page_object(JSContext *cx, prop_t *p);

JSBool js_httpRequest(JSContext *cx, JSObject *obj, uintN argc,
		      jsval *argv, jsval *rval);

JSBool js_readFile(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

#endif // JS_H__ 
