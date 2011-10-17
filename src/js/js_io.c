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

#include "ext/spidermonkey/jsprvtd.h"
#include "ext/spidermonkey/jsxml.h"

#include "fileaccess/fileaccess.h"
#include "htsmsg/htsbuf.h"
#include "misc/string.h"
#include "misc/regex.h"
#include "backend/backend.h"

typedef struct js_http_response {
  char *data;
  size_t datalen;
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
  free(jhr->data);
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
  const char *r = jhr->data, *r2;
  char *tmpbuf = NULL;
  int isxml;
  const charset_t *cs = NULL;

  if(jhr->contenttype != NULL) {
    const char *charset = strstr(jhr->contenttype, "charset=");

    if(charset != NULL) {
      charset += strlen("charset=");

      if(strcasecmp(charset, "utf-8")) {
	cs = charset_get(charset);
	if(cs == NULL)
	  TRACE(TRACE_INFO, "JS", "Unable to handle charset %s", charset);
      }
    }

    isxml =
      strstr(jhr->contenttype, "application/xml") ||
      strstr(jhr->contenttype, "text/xml");
  } else {
    isxml = 0;
  }
  

  if(cs == NULL && !utf8_verify(jhr->data))
    cs = charset_get(NULL);

  if(cs != NULL)
    r = tmpbuf = utf8_from_bytes(jhr->data, jhr->datalen, cs->ptr);

  if(isxml && 
     (r2 = strstr(r, "<?xml ")) != NULL &&
     (r2 = strstr(r2, "?>")) != NULL) {
    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, r2+2));
  } else {
    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, r));
  }
  free(tmpbuf);
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
static JSBool 
js_http_request(JSContext *cx, jsval *rval,
		const char *url, JSObject *argobj, JSObject *postobj,
		JSObject *headerobj)
{
  char **httpargs = NULL;
  int i;
  char errbuf[256];
  char *result;
  size_t resultsize;
  htsbuf_queue_t *postdata = NULL;
  const char *postcontenttype = NULL;
  struct http_header_list in_headers;
  LIST_INIT(&in_headers);

  if(argobj != NULL)
    js_http_add_args(&httpargs, cx, argobj);

  if(postobj != NULL) {
    htsbuf_queue_t hq;
    JSIdArray *ida;
    const char *str;
    const char *prefix = NULL;

    if((ida = JS_Enumerate(cx, postobj)) == NULL)
      return JS_FALSE;

    htsbuf_queue_init(&hq, 0);

    for(i = 0; i < ida->length; i++) {
      jsval name, value;

      if(!JS_IdToValue(cx, ida->vector[i], &name) ||
	 !JSVAL_IS_STRING(name) ||
	 !JS_GetProperty(cx, postobj, JS_GetStringBytes(JSVAL_TO_STRING(name)),
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

      http_header_add(&in_headers,
		      JS_GetStringBytes(JSVAL_TO_STRING(name)),
		      JS_GetStringBytes(JS_ValueToString(cx, value)));
    }
    
    JS_DestroyIdArray(cx, ida);
  }

  int flags = 0;

  const js_context_private_t *jcp = JS_GetContextPrivate(cx);
  if(jcp != NULL && jcp->jcp_flags & JCP_DISABLE_AUTH)
    flags |= HTTP_DISABLE_AUTH;

  struct http_header_list response_headers;

  jsrefcount s = JS_SuspendRequest(cx);
  int n = http_request(url, (const char **)httpargs, 
		       &result, &resultsize, errbuf, sizeof(errbuf),
		       postdata, postcontenttype,
		       flags,
		       &response_headers, &in_headers, NULL);
  JS_ResumeRequest(cx, s);

  if(httpargs != NULL)
    strvec_free(httpargs);

  if(postdata != NULL)
    htsbuf_queue_flush(postdata);

  if(n) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  js_http_response_t *jhr = calloc(1, sizeof(js_http_response_t));

  jhr->data = result;
  jhr->datalen = resultsize;

  mystrset(&jhr->contenttype,
	   http_header_get(&response_headers, "content-type"));

  JSObject *robj = JS_NewObjectWithGivenProto(cx, &http_response_class,
					      NULL, NULL);
  JS_SetPrivate(cx, robj, jhr);
  *rval = OBJECT_TO_JSVAL(robj);

  JS_DefineFunctions(cx, robj, http_response_functions);

  if(!JS_EnterLocalRootScope(cx))
    return JS_FALSE;
    
  JSObject *hdrs = JS_NewObject(cx, NULL, NULL, NULL);
  http_header_t *hh;

  LIST_FOREACH(hh, &response_headers, hh_link) {
    jsval val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, hh->hh_value));
    JS_SetProperty(cx, hdrs, hh->hh_key, &val);
  }
  
  jsval val = OBJECT_TO_JSVAL(hdrs);
  JS_SetProperty(cx, robj, "headers", &val);
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

  if(!JS_ConvertArguments(cx, argc, argv, "s/oo", &url, &argobj, &hdrobj))
    return JS_FALSE;

  return js_http_request(cx, rval, url, argobj, NULL, hdrobj);
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
  JSObject *postobj = NULL;
  JSObject *hdrobj = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "so/oo", &url, &postobj, &argobj,
			  &hdrobj))
    return JS_FALSE;
  
  return js_http_request(cx, rval, url, argobj, postobj, hdrobj);
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
  void *result;
  size_t size;

  if(!JS_ConvertArguments(cx, argc, argv, "s", &url))
    return JS_FALSE;

  jsrefcount s = JS_SuspendRequest(cx);
  result = fa_load(url, &size, NULL, errbuf, sizeof(errbuf), NULL);
  JS_ResumeRequest(cx, s);

  if(result == NULL) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  *rval = STRING_TO_JSVAL(JS_NewString(cx, result, size));
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
static JSFunctionSpec http_auth_functions[] = {
    JS_FS("oauthToken",      js_oauth,       4, 0, 0),
    JS_FS("rawAuth",         js_rawAuth,     1, 0, 0),
    JS_FS("setHeader",       js_setHeader,   2, 0, 0),
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

  if(ret && JSVAL_IS_BOOLEAN(result) && JSVAL_TO_BOOLEAN(result))
    ret = 0;
  else
    ret = 1;
  JS_DestroyContext(cx);
  return ret;
}
