#ifndef JS_H__
#define JS_H__


#include "showtime.h"
#include "prop/prop.h"
#include "ext/spidermonkey/jsapi.h"

extern prop_courier_t *js_global_pc;
extern JSContext *js_global_cx;

LIST_HEAD(js_route_list, js_route);
LIST_HEAD(js_searcher_list, js_searcher);
LIST_HEAD(js_http_auth_list, js_http_auth);
LIST_HEAD(js_plugin_list, js_plugin);
LIST_HEAD(js_service_list, js_service);
LIST_HEAD(js_setting_group_list, js_setting_group);
LIST_HEAD(js_event_handler_list, js_event_handler);
LIST_HEAD(js_subscription_list, js_subscription);
LIST_HEAD(js_subprovider_list, js_subprovider);

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
  struct js_http_auth_list jsp_http_auths;
  struct js_service_list jsp_services;
  struct js_setting_group_list jsp_setting_groups;
  struct js_event_handler_list jsp_event_handlers;
  struct js_subscription_list jsp_subscriptions;
  struct js_subprovider_list jsp_subproviders;

  struct fa_handle *jsp_ref;

  int jsp_protect_object;

} js_plugin_t;


/**
 * Whenever JS_SetContextPrivate() is used to point to something, this
 * struct must be first in that
 */
typedef struct js_context_private {
  int jcp_flags;

#define JCP_DISABLE_AUTH 0x1

} js_context_private_t;

void js_load(const char *url);

JSContext *js_newctx(JSErrorReporter er);

htsmsg_t *js_htsmsg_from_object(JSContext *cx, JSObject *obj);

void js_htsmsg_emit_jsval(JSContext *cx, jsval value, htsmsg_t *msg,
			  const char *fieldname);

JSBool js_object_from_htsmsg(JSContext *cx, const htsmsg_t *msg, jsval *rval);


JSBool js_httpGet(JSContext *cx, JSObject *obj, uintN argc,
		  jsval *argv, jsval *rval);

JSBool js_httpPost(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

JSBool js_readFile(JSContext *cx, JSObject *obj, uintN argc,
		   jsval *argv, jsval *rval);

JSBool js_probe(JSContext *cx, JSObject *obj, uintN argc,
		jsval *argv, jsval *rval);

JSBool js_addURI(JSContext *cx, JSObject *obj, uintN argc, 
		 jsval *argv, jsval *rval);

JSBool js_addSearcher(JSContext *cx, JSObject *obj, uintN argc, 
		      jsval *argv, jsval *rval);

JSBool js_addHTTPAuth(JSContext *cx, JSObject *obj, uintN argc, 
		      jsval *argv, jsval *rval);

JSBool js_createService(JSContext *cx, JSObject *obj, uintN argc, 
			jsval *argv, jsval *rval);

JSBool js_createSettings(JSContext *cx, JSObject *obj, uintN argc, 
			 jsval *argv, jsval *rval);

JSBool js_createStore(JSContext *cx, JSObject *obj, uintN argc, 
		      jsval *argv, jsval *rval);

JSBool js_createPageOptions(JSContext *cx, JSObject *page, const char *url,
			    prop_t *options);

JSBool js_onEvent(JSContext *cx, JSObject *obj,
		  uintN argc, jsval *argv, jsval *rval);

struct backend;

int js_backend_open(prop_t *page, const char *url, int sync);

void js_backend_search(struct prop *model, const char *query);

int js_plugin_load(const char *id, const char *url,
		   char *errbuf, size_t errlen);

void js_plugin_unload(const char *id);

int js_prop_from_object(JSContext *cx, JSObject *obj, prop_t *p);

void js_prop_set_from_jsval(JSContext *cx, prop_t *p, jsval value);

void js_page_flush_from_plugin(JSContext *cx, js_plugin_t *jp);

void js_io_flush_from_plugin(JSContext *cx, js_plugin_t *jsp);

void js_setting_group_flush_from_plugin(JSContext *cx, js_plugin_t *jsp);

void js_service_flush_from_plugin(JSContext *cx, js_plugin_t *jsp);

void js_subscription_flush_from_list(JSContext *cx,
				     struct js_subscription_list *l);

void js_subprovider_flush_from_plugin(JSContext *cx, js_plugin_t *jsp);

JSObject *js_object_from_prop(JSContext *cx, prop_t *p);

JSBool js_wait_for_value(JSContext *cx, prop_t *root, const char *subname,
			 jsval value, jsval *rval);

JSBool js_json_encode(JSContext *cx, JSObject *obj,
		      uintN argc, jsval *argv, jsval *rval);

JSBool  js_json_decode(JSContext *cx, JSObject *obj,
		       uintN argc, jsval *argv, jsval *rval);

JSBool js_cache_put(JSContext *cx, JSObject *obj, uintN argc,
		    jsval *argv, jsval *rval);

JSBool js_cache_get(JSContext *cx, JSObject *obj, uintN argc,
		    jsval *argv, jsval *rval);

JSBool js_get_descriptor(JSContext *cx, JSObject *obj, uintN argc,
			 jsval *argv, jsval *rval);

JSBool js_subscribe_global(JSContext *cx, JSObject *obj, uintN argc,
			   jsval *argv, jsval *rval);

struct http_auth_req;
int js_http_auth_try(const char *url, struct http_auth_req *har);

void js_event_destroy_handlers(JSContext *cx,
			       struct js_event_handler_list *list);

void js_event_dispatch(JSContext *cx, struct js_event_handler_list *list,
		       event_t *e, JSObject *this);

void js_event_handler_create(JSContext *cx, struct js_event_handler_list *list,
			     const char *filter, jsval fun);

void js_page_init(void);

JSBool js_subscribe(JSContext *cx, uintN argc, 
		    jsval *argv, jsval *rval, prop_t *root, const char *pname,
		    struct js_subscription_list *list, prop_courier_t *pc,
		    int *subsptr);

JSBool js_is_prop_true(JSContext *cx, JSObject *o, const char *prop);

rstr_t *js_prop_rstr(JSContext *cx, JSObject *o, const char *prop);

int js_prop_int_or_default(JSContext *cx, JSObject *o, const char *prop, int d);

void js_set_prop_str(JSContext *cx, JSObject *o, const char *prop,
		     const char *str);

void js_set_prop_rstr(JSContext *cx, JSObject *o, const char *prop,
		      rstr_t *rstr);

void js_set_prop_int(JSContext *cx, JSObject *o, const char *prop, int v);

void js_set_prop_dbl(JSContext *cx, JSObject *o, const char *prop, double v);

void js_set_prop_jsval(JSContext *cx, JSObject *obj, const char *name,
		       jsval item);

void js_metaprovider_init(void);

JSBool js_addsubprovider(JSContext *cx, JSObject *obj, uintN argc, 
			 jsval *argv, jsval *rval);

struct sub_scanner;
void js_sub_query(struct sub_scanner *ss);

#endif // JS_H__ 
