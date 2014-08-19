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

#include <string.h>
#include "js.h"

#include "ext/spidermonkey/jsprvtd.h"
#include "ext/spidermonkey/jsxml.h"

#include "fileaccess/fileaccess.h"
#include "htsmsg/htsbuf.h"
#include "misc/str.h"
#include "misc/regex.h"
#include "backend/backend.h"

typedef struct js_http_response {
  buf_t *buf;
  char *url;
  char *contenttype;
} js_http_response_t;



/**
 *
 */
static void
http_response_finalize(JSContext *cx, JSObject *obj)
{
  js_http_response_t *jhr = JS_GetPrivate(cx, obj);

  free(jhr->contenttype);
  free(jhr->url);
  buf_release(jhr->buf);
  free(jhr);
}


/**
 *
 */
static JSBool
http_response_toString(JSContext *cx, JSObject *obj, uintN argc,
		       jsval *argv, jsval *rval)
{
  js_http_response_t *jhr = JS_GetPrivate(cx, obj);
  char *tmpbuf = NULL;
  buf_t *buf = NULL;
  int isxml;
  const charset_t *cs = NULL;

  if(jhr->buf == NULL) {
    *rval = JSVAL_NULL;
    return JS_TRUE;
  }

  const char *r = buf_cstr(jhr->buf), *r2;
  if(r == NULL) {
    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, ""));
    return JS_TRUE;
  }


  if(jhr->contenttype != NULL) {
    const char *charset = strstr(jhr->contenttype, "charset=");

    if(charset != NULL) {
      charset += strlen("charset=");

      if(strcasecmp(charset, "utf-8") && strcasecmp(charset, "utf8")) {
	cs = charset_get(charset);
	if(cs == NULL)
	  TRACE(TRACE_INFO, "JS", "%s: Unable to handle charset %s",
                jhr->url, charset);
        else
          TRACE(TRACE_DEBUG, "JS", "%s: Parsing charset %s as %s",
                jhr->url, charset, cs->id);
      } else {
	tmpbuf = utf8_cleanup(r);
	if(tmpbuf != NULL) {
	  TRACE(TRACE_DEBUG, "JS", "%s: Repairing broken UTF-8",
                jhr->url, charset);
	  r = tmpbuf;
	}
      }
    }

    isxml =
      strstr(jhr->contenttype, "application/xml") ||
      strstr(jhr->contenttype, "text/xml");
  } else {
    isxml = 0;
  }

  if(cs == NULL && !utf8_verify(r)) {
    // Does not parse as valid UTF-8, we need to do something

    // Try to scan document for <meta http-equiv -tags
    char *start = strstr(buf_cstr(jhr->buf), "<meta http-equiv=\"");
    if(start != NULL) {
      start += strlen("<meta http-equiv=\"");
      char *end = strchr(start + 1, '>');
      if(end != NULL) {
        int len = end - start;
        if(len < 1024) {
          char *copy = alloca(len + 1);
          memcpy(copy, start, len);
          copy[len] = 0;
          if(!strncasecmp(copy, "content-type", strlen("content-type"))) {
            const char *charset = strstr(copy, "charset=");
            if(charset != NULL) {
              charset += strlen("charset=");
              char *e = strchr(charset, '"');
              if(e != NULL) {
                *e = 0;

                TRACE(TRACE_DEBUG, "JS",
                      "%s: Found meta tag claiming charset %s",
                      jhr->url, charset);

                if(!strcasecmp(charset, "utf-8") ||
                   !strcasecmp(charset, "utf8")) {
                  tmpbuf = utf8_cleanup(r);
                  if(tmpbuf != NULL) {
                    TRACE(TRACE_DEBUG, "JS", "%s: Repairing broken UTF-8",
                          jhr->url, charset);
                    r = tmpbuf;
                  }
                  goto done;
                } else {

                  cs = charset_get(charset);
                }
              }
            }
          }
        }
      }
    }

    if(cs == NULL) {
      TRACE(TRACE_DEBUG, "JS", jhr->url,
            "%s: No charset found and not utf-8, treting as latin-1");
      cs = charset_get(NULL);
    }
  }

  if(cs != NULL) {
    // Convert from given character set
    buf = utf8_from_bytes(buf_cstr(jhr->buf), jhr->buf->b_size, cs, NULL, 0);
    r = buf_cstr(buf);
  }
 done:
  if(isxml && 
     (r2 = strstr(r, "<?xml ")) != NULL &&
     (r2 = strstr(r2, "?>")) != NULL) {
    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, r2+2));
  } else {
    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, r));
  }
  free(tmpbuf);
  buf_release(buf);
  return JS_TRUE;
}


/**
 *
 */
static JSFunctionSpec http_response_functions[] = {
    JS_FS("toString",           http_response_toString,  0, 0, 0),
    JS_FS_END
};


/**
 *
 */
static JSClass http_response_class = {
  "httpresponse", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub, JS_ConvertStub, http_response_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static void
js_http_add_args(char ***httpargs, JSContext *cx, JSObject *argobj)
{
  JSIdArray *ida;
  int i;

  if((ida = JS_Enumerate(cx, argobj)) == NULL)
    return;
  
  for(i = 0; i < ida->length; i++) {
    jsval name, value;
    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;

    if(JSVAL_IS_STRING(name)) {
      
      if(!JS_GetProperty(cx, argobj, JS_GetStringBytes(JSVAL_TO_STRING(name)),
			 &value) || 
	 JSVAL_IS_VOID(value))
	continue;
      strvec_addp(httpargs, JS_GetStringBytes(JSVAL_TO_STRING(name)));
      strvec_addp(httpargs, JS_GetStringBytes(JS_ValueToString(cx, value)));
    } else if(JSVAL_IS_INT(name)) {
      if(!JS_GetElement(cx, argobj, JSVAL_TO_INT(name), &value) ||
	 !JSVAL_IS_OBJECT(value))
	continue;
      
      js_http_add_args(httpargs, cx, JSVAL_TO_OBJECT(value));
    }

  }
  JS_DestroyIdArray(cx, ida);
}


/**
 *
 */
static int
disable_cache_on_http_headers(struct http_header_list *list)
{
  http_header_t *hh;
  LIST_FOREACH(hh, list, hh_link) {
    if(!strcasecmp(hh->hh_key, "user-agent"))
      continue;
    return 1;
  }
  return 0;
}


/**
 *
 */
static JSBool 
js_http_request(JSContext *cx, jsval *rval,
		const char *url, JSObject *argobj, jsval *postval,
		JSObject *headerobj, JSObject *ctrlobj, const char *method)
{
  char **httpargs = NULL;
  int i;
  char errbuf[256];
  htsbuf_queue_t *postdata = NULL;
  const char *postcontenttype = NULL;
  struct http_header_list request_headers;
  int flags = 0;
  int headreq = 0;
  int cache = 0;
  int min_expire = 0;
  htsbuf_queue_t hq;

  LIST_INIT(&request_headers);

  if(ctrlobj) {
    if(js_is_prop_true(cx, ctrlobj, "debug"))
      flags |= FA_DEBUG;
    if(js_is_prop_true(cx, ctrlobj, "noFollow"))
      flags |= FA_NOFOLLOW;
    if(js_is_prop_true(cx, ctrlobj, "headRequest"))
      headreq = 1;
    if(js_is_prop_true(cx, ctrlobj, "caching"))
      cache = 1;
    if(js_is_prop_true(cx, ctrlobj, "compression"))
      flags |= FA_COMPRESSION;
    min_expire = js_prop_int_or_default(cx, ctrlobj, "cacheTime", 0);

    if(min_expire)
      cache = 1;
  }

  if(argobj != NULL)
    js_http_add_args(&httpargs, cx, argobj);

  if(postval != NULL) {
    JSIdArray *ida;
    const char *str;
    const char *prefix = NULL;

    if(JSVAL_IS_OBJECT(*postval)) {
      JSObject *postobj = JSVAL_TO_OBJECT(*postval);

      if((ida = JS_Enumerate(cx, postobj)) == NULL)
	return JS_FALSE;

      htsbuf_queue_init(&hq, 0);

      for(i = 0; i < ida->length; i++) {
	jsval name, value;

	if(!JS_IdToValue(cx, ida->vector[i], &name) ||
	   !JSVAL_IS_STRING(name) ||
	   !JS_GetProperty(cx, postobj,
			   JS_GetStringBytes(JSVAL_TO_STRING(name)),
			   &value) || JSVAL_IS_VOID(value))
	  continue;

	str = JS_GetStringBytes(JSVAL_TO_STRING(name));
	if(prefix)
	  htsbuf_append(&hq, prefix, strlen(prefix));
	htsbuf_append_and_escape_url(&hq, str);

	str = JS_GetStringBytes(JS_ValueToString(cx, value));
	htsbuf_append(&hq, "=", 1);
	htsbuf_append_and_escape_url(&hq, str);
      
	prefix = "&";
      }
    
      JS_DestroyIdArray(cx, ida);
      postdata = &hq;
      postcontenttype =  "application/x-www-form-urlencoded";
    } else if(JSVAL_IS_STRING(*postval)) {

      str = JS_GetStringBytes(JSVAL_TO_STRING(*postval));
      htsbuf_queue_init(&hq, 0);
      htsbuf_append(&hq, str, strlen(str));
      postdata = &hq;
      postcontenttype =  "text/ascii";
    }
  }

  if(headerobj != NULL) {
    JSIdArray *ida;

    if((ida = JS_Enumerate(cx, headerobj)) == NULL)
      return JS_FALSE;

    for(i = 0; i < ida->length; i++) {
      jsval name, value;

      if(!JS_IdToValue(cx, ida->vector[i], &name) ||
	 !JSVAL_IS_STRING(name) ||
	 !JS_GetProperty(cx, headerobj,
			 JS_GetStringBytes(JSVAL_TO_STRING(name)),
			 &value) || JSVAL_IS_VOID(value))
	continue;

      http_header_add(&request_headers,
		      JS_GetStringBytes(JSVAL_TO_STRING(name)),
		      JS_GetStringBytes(JS_ValueToString(cx, value)), 0);
    }
    
    JS_DestroyIdArray(cx, ida);
  }

  struct cancellable *c = NULL;
  const js_context_private_t *jcp = JS_GetContextPrivate(cx);
  if(jcp != NULL) {
    if(jcp->jcp_flags & JCP_DISABLE_AUTH)
      flags |= FA_DISABLE_AUTH;
    c = jcp->jcp_c;
  }

  if(method != NULL && !strcmp(method, "HEAD")) {
    method = NULL;
    headreq = 1;
  }

  struct http_header_list response_headers;
  js_http_response_t *jhr;
  jsrefcount s = JS_SuspendRequest(cx);

  LIST_INIT(&response_headers);

  /**
   * If user add specific HTTP headers we will disable caching
   * A few header types are OK to send though since I don't
   * think it will affect result that much
   */
  if(cache)
    cache = !disable_cache_on_http_headers(&request_headers);

  if(cache && method == NULL && !headreq && !postdata) {

    /**
     * If it's a GET request and cache is enabled, run it thru
     * fa_load() to get caching
     */

    buf_t *b = fa_load(url,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       FA_LOAD_QUERY_ARGVEC(httpargs),
                       FA_LOAD_FLAGS(flags),
                       FA_LOAD_CANCELLABLE(c),
                       FA_LOAD_MIN_EXPIRE(min_expire),
                       FA_LOAD_REQUEST_HEADERS(&request_headers),
                       FA_LOAD_RESPONSE_HEADERS(&response_headers),
                       NULL);
    JS_ResumeRequest(cx, s);

    if(b == NULL) {
      JS_ReportError(cx, errbuf);
      http_headers_free(&request_headers);
      return JS_FALSE;
    }

    jhr = calloc(1, sizeof(js_http_response_t));
    jhr->buf = b;
    mystrset(&jhr->contenttype, rstr_get(b->b_content_type));

  } else {

    buf_t *result = NULL;

    int n = http_req(url,
                     HTTP_ARGLIST(httpargs),
                     HTTP_RESULT_PTR(headreq ? NULL : &result),
                     HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                     HTTP_POSTDATA(postdata, postcontenttype),
                     HTTP_FLAGS(flags),
                     HTTP_RESPONSE_HEADERS(&response_headers),
                     HTTP_REQUEST_HEADERS(&request_headers),
                     HTTP_METHOD(method),
                     HTTP_CANCELLABLE(c),
                     NULL);

    JS_ResumeRequest(cx, s);

    if(httpargs != NULL)
      strvec_free(httpargs);

    if(n) {
      JS_ReportError(cx, errbuf);
      http_headers_free(&request_headers);
      return JS_FALSE;
    }

    jhr = calloc(1, sizeof(js_http_response_t));
  
    jhr->buf = result;
    mystrset(&jhr->contenttype,
	     http_header_get(&response_headers, "content-type"));
  }
  jhr->url = strdup(url);

  http_headers_free(&request_headers);

  JSObject *robj = JS_NewObjectWithGivenProto(cx, &http_response_class,
					      NULL, NULL);
  JS_SetPrivate(cx, robj, jhr);
  *rval = OBJECT_TO_JSVAL(robj);

  JS_DefineFunctions(cx, robj, http_response_functions);

  if(!JS_EnterLocalRootScope(cx))
    return JS_FALSE;

  // HTTP headers
    
  JSObject *hdrs = JS_NewObject(cx, NULL, NULL, NULL);
  http_header_t *hh;

  LIST_FOREACH(hh, &response_headers, hh_link) {
    jsval val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, hh->hh_value));
    JS_SetProperty(cx, hdrs, hh->hh_key, &val);
  }
  
  jsval val = OBJECT_TO_JSVAL(hdrs);
  JS_SetProperty(cx, robj, "headers", &val);


  JSObject *multiheaders = JS_NewObject(cx, NULL, NULL, NULL);

  LIST_FOREACH(hh, &response_headers, hh_link) {

    jsval key;
    JSObject *array;
    if(JS_GetProperty(cx, multiheaders, hh->hh_key, &key) &&
       JSVAL_IS_OBJECT(key)) {
      array = JSVAL_TO_OBJECT(key);
    } else {
      array = JS_NewObject(cx, NULL, NULL, NULL);
      key = OBJECT_TO_JSVAL(array);
      JS_SetProperty(cx, multiheaders, hh->hh_key, &key);
    }

    jsuint length;
    if(JS_GetArrayLength(cx, array, &length)) {
      jsval val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, hh->hh_value));
      JS_SetElement(cx, array, length, &val);
      JS_SetArrayLength(cx, array, length + 1);
    }
  }

  val = OBJECT_TO_JSVAL(multiheaders);
  JS_SetProperty(cx, robj, "multiheaders", &val);

  js_set_prop_str(cx, robj, "contenttype", jhr->contenttype);

  JS_LeaveLocalRootScope(cx);

  http_headers_free(&response_headers);
  return JS_TRUE;
}


/**
 *
 */
JSBool 
js_httpGet(JSContext *cx, JSObject *obj, uintN argc,
	   jsval *argv, jsval *rval)
{
  const char *url;
  JSObject *argobj = NULL;
  JSObject *hdrobj = NULL;
  JSObject *ctrlobj = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "s/ooo", &url, &argobj, &hdrobj,
			  &ctrlobj))
    return JS_FALSE;

  return js_http_request(cx, rval, url, argobj, NULL, hdrobj, ctrlobj, NULL);
}

/**
 *
 */
JSBool 
js_httpPost(JSContext *cx, JSObject *obj, uintN argc,
	   jsval *argv, jsval *rval)
{
  const char *url;
  JSObject *argobj = NULL;
  jsval postval;
  JSObject *hdrobj = NULL;
  JSObject *ctrlobj = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "sv/ooo", &url, &postval, &argobj,
			  &hdrobj, &ctrlobj))
    return JS_FALSE;
  
  return js_http_request(cx, rval, url, argobj, &postval, hdrobj,
			 ctrlobj, NULL);
}


/**
 *
 */
JSBool 
js_httpReq(JSContext *cx, JSObject *obj, uintN argc,
	   jsval *argv, jsval *rval)
{
  const char *url;
  jsval postval, *pv = NULL;
  JSObject *ctrlobj = NULL;
  JSObject *argobj = NULL;
  JSObject *hdrobj = NULL;
  rstr_t *method = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "s/o", &url, &ctrlobj))
    return JS_FALSE;

  if(ctrlobj != NULL) {
    
    argobj = js_prop_obj(cx, ctrlobj, "args");
    hdrobj = js_prop_obj(cx, ctrlobj, "headers");
    method = js_prop_rstr(cx, ctrlobj, "method");
    if(JS_GetProperty(cx, ctrlobj, "postdata", &postval))
      pv = &postval;
  }

  JSBool v = js_http_request(cx, rval, url, argobj, pv, hdrobj, ctrlobj,
			     rstr_get(method));
  rstr_release(method);
  return v;
}

/**
 *
 */
#if ENABLE_RELEASE == 0
JSBool 
js_readFile(JSContext *cx, JSObject *obj, uintN argc,
	       jsval *argv, jsval *rval)
{
  const char *url;
  char errbuf[256];
  buf_t *result;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  jsrefcount s = JS_SuspendRequest(cx);
  result = fa_load(url,
                    FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                    NULL);
  JS_ResumeRequest(cx, s);

  if(result == NULL) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, buf_cstr(result)));
  buf_release(result);
  return JS_TRUE;
}
#endif

/**
 *
 */
JSBool 
js_probe(JSContext *cx, JSObject *obj, uintN argc,
	 jsval *argv, jsval *rval)
{
  const char *url;
  char errbuf[256];
  backend_probe_result_t res;
  jsval val;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  jsrefcount s = JS_SuspendRequest(cx);
  res = backend_probe(url, errbuf, sizeof(errbuf));
  JS_ResumeRequest(cx, s);

  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  if(res != BACKEND_PROBE_OK) {
    val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, errbuf));
    JS_SetProperty(cx, robj, "errmsg", &val);
  }
  
  val = INT_TO_JSVAL(res);
  JS_SetProperty(cx, robj, "result", &val);
  return JS_TRUE;
}


/**
 *
 */
JSBool
js_basename(JSContext *cx, JSObject *obj, uintN argc,
            jsval *argv, jsval *rval)
{
  const char *url;
  char tmp[URL_MAX];

  if(!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  fa_url_get_last_component(tmp, sizeof(tmp), url);

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, tmp));
  return JS_TRUE;
}


/**
 *
 */
JSBool
js_copyfile(JSContext *cx, JSObject *obj, uintN argc,
            jsval *argv, jsval *rval)
{
  const char *from;
  const char *to;
  char *cleanup;
  char path[URL_MAX];
  char errbuf[256];

  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "ss", &from, &to))
    return JS_FALSE;

  cleanup = mystrdupa(to);

  fa_sanitize_filename(cleanup);

  snprintf(path, sizeof(path), "file://%s/plugins/%s/%s",
           gconf.cache_path, jsp->jsp_id, cleanup);

  TRACE(TRACE_DEBUG, "JS", "Copying file from '%s' to '%s'", from, path);

  if(fa_copy(path, from, errbuf, sizeof(errbuf))) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, path));
  return JS_TRUE;
}


static struct js_http_auth_list js_http_auths;


/**
 *
 */
typedef struct js_http_auth {
  js_plugin_t *jha_jsp;
  LIST_ENTRY(js_http_auth) jha_global_link;
  LIST_ENTRY(js_http_auth) jha_plugin_link;
  char *jha_pattern;
  hts_regex_t jha_regex;
  jsval jha_func;
} js_http_auth_t;



/**
 *
 */
static void
js_http_auth_delete(JSContext *cx, js_http_auth_t *jha)
{
  JS_RemoveRoot(cx, &jha->jha_func);

  LIST_REMOVE(jha, jha_global_link);
  LIST_REMOVE(jha, jha_plugin_link);

  hts_regfree(&jha->jha_regex);

  free(jha->jha_pattern);
  free(jha);
}



/**
 *
 */
JSBool 
js_addHTTPAuth(JSContext *cx, JSObject *obj, uintN argc, 
	       jsval *argv, jsval *rval)
{
  const char *str;
  js_http_auth_t *jha;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  str = JS_GetStringBytes(JS_ValueToString(cx, argv[0]));

  if(!JS_ObjectIsFunction(cx, JSVAL_TO_OBJECT(argv[1]))) {
    JS_ReportError(cx, "Argument is not a function");
    return JS_FALSE;
  }

  if(str[0] != '^') {
    int l = strlen(str);
    char *s = alloca(l + 2);
    s[0] = '^';
    memcpy(s+1, str, l+1);
    str = s;
  }

 
  jha = calloc(1, sizeof(js_http_auth_t));
  jha->jha_jsp = jsp;
  if(hts_regcomp(&jha->jha_regex, str)) {
    free(jha);
    JS_ReportError(cx, "Invalid regular expression");
    return JS_FALSE;
  }
  
  jha->jha_pattern = strdup(str);
  
  LIST_INSERT_HEAD(&js_http_auths, jha, jha_global_link);
  LIST_INSERT_HEAD(&jsp->jsp_http_auths, jha, jha_plugin_link);

  TRACE(TRACE_DEBUG, "JS", "Add auth handler for %s", str);

  jha->jha_func = argv[1];
  JS_AddNamedRoot(cx, &jha->jha_func, "authuri");

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
void
js_io_flush_from_plugin(JSContext *cx, js_plugin_t *jsp)
{
  js_http_auth_t *jha;

  while((jha = LIST_FIRST(&jsp->jsp_http_auths)) != NULL)
    js_http_auth_delete(cx, jha);
}



/**
 *
 */
static JSClass http_auth_class = {
  "httpauth", JSCLASS_HAS_PRIVATE,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSBool
js_oauth(JSContext *cx, JSObject *obj,
	 uintN argc, jsval *argv, jsval *rval)
{
  const char *consumer_key;
  const char *consumer_secret;
  const char *token;
  const char *token_secret;

  if(!JS_ConvertArguments(cx, argc, argv, "ssss",
			  &consumer_key, &consumer_secret,
			  &token, &token_secret))
    return JS_FALSE;

  *rval = BOOLEAN_TO_JSVAL(!http_client_oauth(JS_GetPrivate(cx, obj),
					      consumer_key, consumer_secret,
					      token, token_secret));
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_rawAuth(JSContext *cx, JSObject *obj,
	   uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  *rval = BOOLEAN_TO_JSVAL(!http_client_rawauth(JS_GetPrivate(cx, obj), str));
  return JS_TRUE;
}



/**
 *
 */
static JSBool
js_setHeader(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *key;
  const char *value;

  if(!JS_ConvertArguments(cx, argc, argv, "ss", &key, &value))
    return JS_FALSE;

  *rval = JSVAL_NULL;
  http_client_set_header(JS_GetPrivate(cx, obj), key, value);
  return JS_TRUE;
}




/**
 *
 */
static JSBool
js_setCookie(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *key;
  const char *value;

  if(!JS_ConvertArguments(cx, argc, argv, "ss", &key, &value))
    return JS_FALSE;

  *rval = JSVAL_NULL;
  http_client_set_cookie(JS_GetPrivate(cx, obj), key, value);
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_fail(JSContext *cx, JSObject *obj,
        uintN argc, jsval *argv, jsval *rval)
{
  const char *reason;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &reason))
    return JS_FALSE;

  *rval = JSVAL_NULL;
  http_client_fail_req(JS_GetPrivate(cx, obj), reason);
  return JS_TRUE;
}



/**
 *
 */
static JSFunctionSpec http_auth_functions[] = {
    JS_FS("oauthToken",      js_oauth,       4, 0, 0),
    JS_FS("rawAuth",         js_rawAuth,     1, 0, 0),
    JS_FS("setHeader",       js_setHeader,   2, 0, 0),
    JS_FS("setCookie",       js_setCookie,   2, 0, 0),
    JS_FS("fail",            js_fail,        1, 0, 0),
    JS_FS_END
};


/**
 *
 */
int
js_http_auth_try(const char *url, struct http_auth_req *har)
{
  js_http_auth_t *jha;
  hts_regmatch_t matches[8];
  jsval *argv, result;
  void *mark;
  int argc, ret;
  JSObject *pobj;
  js_context_private_t jcp = {0};

  LIST_FOREACH(jha, &js_http_auths, jha_global_link)
    if(!hts_regexec(&jha->jha_regex, url, 8, matches, 0))
      break;

  if(jha == NULL)
    return 1;

  JSContext *cx = js_newctx(NULL);

  jcp.jcp_flags = JCP_DISABLE_AUTH;
  JS_SetContextPrivate(cx, &jcp);

  JS_BeginRequest(cx);

  pobj = JS_NewObject(cx, &http_auth_class, NULL, NULL);
  JS_AddNamedRoot(cx, &pobj, "plugin");

  JS_SetPrivate(cx, pobj, har);

  JS_DefineFunctions(cx, pobj, http_auth_functions);

  argc = 1;
  argv = JS_PushArguments(cx, &mark, "o", pobj);
  
  ret = JS_CallFunctionValue(cx, NULL, jha->jha_func, argc, argv, &result);
  JS_PopArguments(cx, mark);

  JS_RemoveRoot(cx, &pobj);
  JS_DestroyContext(cx);

  if(!ret) {
    http_client_fail_req(har, "Script error");
    return 0;
  }

  if(ret && JSVAL_IS_BOOLEAN(result) && JSVAL_TO_BOOLEAN(result))
    ret = 0;
  else
    ret = 1;
  return ret;
}
