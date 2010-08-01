#ifndef JS_H__
#define JS_H__


#include "showtime.h"
#include "prop/prop.h"
#include "ext/spidermonkey/jsapi.h"

JSContext *js_newctx(void);

JSBool js_httpRequest(JSContext *cx, JSObject *obj, uintN argc,
		      jsval *argv, jsval *rval);

JSBool js_readFile(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

JSBool js_addURI(JSContext *cx, JSObject *obj, uintN argc, 
		 jsval *argv, jsval *rval);

struct navigator;
struct backend;

struct nav_page *js_page_open(struct backend *be, struct navigator *nav, 
			      const char *url, const char *view,
			      char *errbuf, size_t errlen);

#endif // JS_H__ 
