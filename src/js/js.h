#ifndef JS_H__
#define JS_H__


#include "showtime.h"
#include "prop/prop.h"
#include "ext/spidermonkey/jsapi.h"

LIST_HEAD(js_route_list, js_route);
LIST_HEAD(js_searcher_list, js_searcher);
LIST_HEAD(js_plugin_list, js_plugin);

/**
 *
 */
typedef struct js_plugin {
  LIST_ENTRY(js_plugin) jsp_link;

  char *jsp_url;
  char *jsp_id;

  char jsp_enable_uri_routing;
  char jsp_enable_search;

  struct js_route_list jsp_routes;
  struct js_searcher_list jsp_searchers;

} js_plugin_t;

JSContext *js_newctx(JSErrorReporter er);

JSBool js_httpGet(JSContext *cx, JSObject *obj, uintN argc,
		  jsval *argv, jsval *rval);

JSBool js_httpPost(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

JSBool js_readFile(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

JSBool js_addURI(JSContext *cx, JSObject *obj, uintN argc, 
		 jsval *argv, jsval *rval);

JSBool js_addSearcher(JSContext *cx, JSObject *obj, uintN argc, 
		      jsval *argv, jsval *rval);

JSBool js_createService(JSContext *cx, JSObject *obj, uintN argc, 
			jsval *argv, jsval *rval);

JSBool js_createSettings(JSContext *cx, JSObject *obj, uintN argc, 
			 jsval *argv, jsval *rval);

struct backend;

int js_backend_open(prop_t *page, const char *url);

void js_backend_search(struct prop *model, const char *query);

int js_plugin_load(const char *id, const char *url,
		   char *errbuf, size_t errlen);

int js_prop_from_object(JSContext *cx, JSObject *obj, prop_t *p);

void js_prop_set_from_jsval(JSContext *cx, prop_t *p, jsval value);

void js_page_flush_from_plugin(JSContext *cx, js_plugin_t *jp);

JSObject *js_object_from_prop(JSContext *cx, prop_t *p);

JSBool js_wait_for_value(JSContext *cx, prop_t *root, const char *subname,
			 jsval value, jsval *rval);

JSBool js_json_encode(JSContext *cx, JSObject *obj,
		      uintN argc, jsval *argv, jsval *rval);

JSBool  js_json_decode(JSContext *cx, JSObject *obj,
		       uintN argc, jsval *argv, jsval *rval);

#endif // JS_H__ 
