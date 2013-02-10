/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2009 Andreas Öman
 *  Copyright (C) 2011-2012 Fábio Ferreira
 *  Copyright (C) 2012 Henrik Andersson
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
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <limits.h>

#include "js.h"

#include "backend/backend.h"
#include "misc/str.h"
#include "fileaccess/fileaccess.h"
#include "keyring.h"
#include "notifications.h"
#include "networking/net.h"
#include "ui/webpopup.h"
#include "misc/md5.h"
#include "misc/sha.h"
#include "api/xmlrpc.h"
#include "i18n.h"

prop_courier_t *js_global_pc;
JSContext *js_global_cx;
prop_sub_t *js_event_sub;

static JSRuntime *runtime;
static JSObject *showtimeobj;
static JSObject *RichText;
static JSObject *Link;
struct js_plugin_list js_plugins;

static JSClass global_class = {
  "global", JSCLASS_GLOBAL_FLAGS,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};



/**
 *
 */
JSBool
js_is_prop_true(JSContext *cx, JSObject *o, const char *prop)
{
  jsval val;
  JSBool b;
  if(!JS_GetProperty(cx, o, prop, &val))
    return 0;
  if(!JS_ValueToBoolean(cx, val, &b))
    return 0;
  return b;
}



/**
 *
 */
rstr_t *
js_prop_rstr(JSContext *cx, JSObject *o, const char *prop)
{
  jsval val;
  if(!JS_GetProperty(cx, o, prop, &val))
    return NULL;
  if(!JSVAL_IS_STRING(val))
    return NULL;
  JSString *s = JS_ValueToString(cx, val);
  return s ? rstr_alloc(JS_GetStringBytes(s)) : NULL;
}


/**
 *
 */
int
js_prop_int_or_default(JSContext *cx, JSObject *o, const char *prop, int d)
{
  jsval val;
  if(!JS_GetProperty(cx, o, prop, &val))
    return d;
  double v;
  if(!JSVAL_IS_NUMBER(val) || !JS_ValueToNumber(cx, val, &v))
    return d;
  return v;
}


/**
 *
 */
void
js_set_prop_str(JSContext *cx, JSObject *o, const char *prop, const char *str)
{
  JSString *s = str ? JS_NewStringCopyZ(cx, str) : NULL;
  if(s != NULL)
    js_set_prop_jsval(cx, o, prop, STRING_TO_JSVAL(s));
}


/**
 *
 */
void
js_set_prop_rstr(JSContext *cx, JSObject *o, const char *prop, rstr_t *r)
{
  js_set_prop_str(cx, o, prop, rstr_get(r));
}


/**
 *
 */
void
js_set_prop_int(JSContext *cx, JSObject *o, const char *prop, int v)
{
  jsval val;
  if(v <= INT32_MAX && v >= INT32_MIN && INT_FITS_IN_JSVAL(v))
    val = INT_TO_JSVAL(v);
  else {
    jsdouble *d = JS_NewDouble(cx, v);
    if(d == NULL)
      return;
    val = DOUBLE_TO_JSVAL(d);
  }
   js_set_prop_jsval(cx, o, prop, val);
}


/**
 *
 */
void
js_set_prop_dbl(JSContext *cx, JSObject *o, const char *prop, double v)
{
  jsdouble *d = JS_NewDouble(cx, v);
  if(d != NULL)
    js_set_prop_jsval(cx, o, prop, DOUBLE_TO_JSVAL(d));
}


/**
 *
 */
void
js_set_prop_jsval(JSContext *cx, JSObject *obj, const char *name, jsval item)
{
  if(name)
    JS_SetProperty(cx, obj, name, &item);
  else {
    jsuint length;
    if(JS_GetArrayLength(cx, obj, &length))
      JS_SetElement(cx, obj, length, &item);
  }
}



/**
 *
 */
static void
err_reporter(JSContext *cx, const char *msg, JSErrorReport *r)
{
  int level;

  if(r->flags & JSREPORT_WARNING)
    level = TRACE_INFO;
  else
    level = TRACE_ERROR;

  TRACE(level, "JS", "%s:%u %s",  r->filename, r->lineno, msg);
}


/**
 *
 */
JSContext *
js_newctx(JSErrorReporter er)
{
  JSContext *cx = JS_NewContext(runtime, 8192);

  JS_SetOptions(cx, 
		JSOPTION_STRICT |
		JSOPTION_WERROR | 
		JSOPTION_VAROBJFIX);
  JS_SetErrorReporter(cx, er ?: err_reporter);
#ifdef JS_GC_ZEAL
  //  JS_SetGCZeal(cx, 1);
#endif
  return cx;
}



/**
 *
 */
static JSBool 
js_trace(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  const char *id = "JS";

  if (!JS_ConvertArguments(cx, argc, argv, "s/s", &str, &id))
    return JS_FALSE;

  TRACE(TRACE_DEBUG, id, "%s", str);
  *rval = JSVAL_VOID;  /* return undefined */
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_print(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  fprintf(stderr, "%s\n", str);
  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_queryStringSplit(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  char *s, *s0;
  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  s0 = s = strdup(str);

  while(s) {
    
    char *k = s;
    char *v = strchr(s, '=');
    if(v == NULL)
      break;

    *v++ = 0;

    if((s = strchr(v, '&')) != NULL)
      *s++ = 0;

    k = strdup(k);
    v = strdup(v);

    url_deescape(k);
    url_deescape(v);

    jsval val = STRING_TO_JSVAL(JS_NewString(cx, v, strlen(v)));
    JS_SetProperty(cx, robj, k, &val);
    free(k);
  }
  free(s0);
  *rval = OBJECT_TO_JSVAL(robj);
  return JS_TRUE;
}

static JSBool 
js_escape(JSContext *cx, JSObject *obj,
	  uintN argc, jsval *argv, jsval *rval, int how)
{

  const char *str;
  char *r;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  size_t len = url_escape(NULL, 0, str, how);
  r = malloc(len);
  url_escape(r, len, str, how);

  *rval = STRING_TO_JSVAL(JS_NewString(cx, r, len-1));
  return JS_TRUE;
}

/**
 *
 */
static JSBool 
js_pathEscape(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  return js_escape(cx, obj, argc, argv, rval, URL_ESCAPE_PATH);
}


/**
 *
 */
static JSBool 
js_paramEscape(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  return js_escape(cx, obj, argc, argv, rval, URL_ESCAPE_PARAM);
}


/**
 *
 */
static void
js_prop_from_str(JSContext *cx, prop_t *p, jsval value)
{
  prop_set_string(p, JS_GetStringBytes(JS_ValueToString(cx, value)));
}

/**
 *
 */
void
js_prop_set_from_jsval(JSContext *cx, prop_t *p, jsval value)
{
  JSBool b;
  if(JSVAL_IS_INT(value)) {
    prop_set_int(p, JSVAL_TO_INT(value));
  } else if(JSVAL_IS_BOOLEAN(value)) {
    prop_set_int(p, JSVAL_TO_BOOLEAN(value));
  } else if(JSVAL_IS_NULL(value) || JSVAL_IS_VOID(value)) {
    prop_set_void(p);
  } else if(JSVAL_IS_DOUBLE(value)) {
    double d;
    if(JS_ValueToNumber(cx, value, &d))
      prop_set_float(p, d);
  } else if(JS_HasInstance(cx, RichText, value, &b) && b) {
    JSObject *o = JSVAL_TO_OBJECT(value);
    jsval v2;

    if(!JS_EnterLocalRootScope(cx))
      return;

    if(!JS_GetProperty(cx, o, "text", &v2)) {
      JS_LeaveLocalRootScope(cx);
      return;
    }

    prop_set_string_ex(p, NULL, JS_GetStringBytes(JS_ValueToString(cx, v2)),
		       PROP_STR_RICH);
    JS_LeaveLocalRootScope(cx);
  } else if(JS_HasInstance(cx, Link, value, &b) && b) {
    JSObject *o = JSVAL_TO_OBJECT(value);
    jsval v1;
    jsval v2;

    if(!JS_EnterLocalRootScope(cx))
      return;

    if(!JS_GetProperty(cx, o, "title", &v1)) {
      JS_LeaveLocalRootScope(cx);
      return;
    }
    if(!JS_GetProperty(cx, o, "url", &v2)) {
      JS_LeaveLocalRootScope(cx);
      return;
    }

    prop_set_link(p,
		  JS_GetStringBytes(JS_ValueToString(cx, v1)),
		  JS_GetStringBytes(JS_ValueToString(cx, v2)));
    JS_LeaveLocalRootScope(cx);
  } else if(JSVAL_IS_STRING(value)) {
    js_prop_from_str(cx, p, value);
  } else if(JSVAL_IS_OBJECT(value)) {
    JSObject *obj = JSVAL_TO_OBJECT(value);
    JSClass *c = JS_GetClass(cx, obj);

    if(!strcmp(c->name, "XML"))   // Treat some classes special
      js_prop_from_str(cx, p, value);
    else
      js_prop_from_object(cx, obj, p);
  } else {
    prop_set_void(p);
  }
}


/**
 *
 */
int
js_prop_from_object(JSContext *cx, JSObject *obj, prop_t *p)
{
  JSIdArray *ida;
  int i, r = 0;
  const char *n;
  int array_zapped = 0;

  if((ida = JS_Enumerate(cx, obj)) == NULL)
    return -1;
  
  for(i = 0; i < ida->length; i++) {
    jsval name, value;

    if(!JS_IdToValue(cx, ida->vector[i], &name))
      continue;

    if(JSVAL_IS_STRING(name)) {
      n = JS_GetStringBytes(JSVAL_TO_STRING(name));
      if(!JS_GetProperty(cx, obj, n, &value))
	continue;
    } else if(JSVAL_IS_INT(name)) {
      if(!JS_GetElement(cx, obj, JSVAL_TO_INT(name), &value) ||
	 JSVAL_IS_VOID(value))
	continue;
      if(!array_zapped) {
	array_zapped = 1;
	prop_destroy_by_name(p, NULL);
      }
      n = NULL;
    } else {
      continue;
    }

    if(JSVAL_TO_OBJECT(value) == obj)
      continue;

    js_prop_set_from_jsval(cx, prop_create(p, n), value);
  }
  JS_DestroyIdArray(cx, ida);
  return r;
}


/**
 *
 */
static JSBool 
js_canHandle(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  *rval = BOOLEAN_TO_JSVAL(!!backend_canhandle(str));
  return JS_TRUE;
}



/**
 *
 */
static JSBool
js_getAuthCredentials(JSContext *cx, JSObject *obj,
		      uintN argc, jsval *argv, jsval *rval)
{
  char buf[256];
  const char *id = NULL, *reason, *source;
  char *username, *password;
  JSBool query, forcetmp = 0;
  int r;
  jsval val;
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(!JS_ConvertArguments(cx, argc, argv, "ssb/sb",
			  &source, &reason, &query, &id, &forcetmp))
    return JS_FALSE;

  snprintf(buf, sizeof(buf), "plguin-%s%s%s", jsp->jsp_id,
	   id ? "-" : "", id ?: "");

  int flags = 0;
  flags |= query    ? KEYRING_QUERY_USER : 0;
  flags |= forcetmp ? 0 : KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET;


  r = keyring_lookup(buf, &username, &password, NULL, NULL,
		     source, reason, flags);

  if(r == 1) {
    *rval = BOOLEAN_TO_JSVAL(0);
    return JS_TRUE;
  }
  
  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

  if(r == -1) {
    val = BOOLEAN_TO_JSVAL(1);
    JS_SetProperty(cx, robj, "rejected", &val);
  } else {

    val = STRING_TO_JSVAL(JS_NewString(cx, username, strlen(username)));
    JS_SetProperty(cx, robj, "username", &val);

    val = STRING_TO_JSVAL(JS_NewString(cx, password, strlen(password)));
    JS_SetProperty(cx, robj, "password", &val);
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool
js_message(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *message;
  JSBool ok, cancel;
  int r;

  if(!JS_ConvertArguments(cx, argc, argv, "sbb", &message, &ok, &cancel))
    return JS_FALSE;

  r = message_popup(message, 
		    (ok     ? MESSAGE_POPUP_OK : 0) |
		    (cancel ? MESSAGE_POPUP_CANCEL : 0) | 
		    MESSAGE_POPUP_RICH_TEXT, NULL);

  switch(r) {
  case MESSAGE_POPUP_OK:
    *rval = BOOLEAN_TO_JSVAL(JS_TRUE);
    break;
  case MESSAGE_POPUP_CANCEL:
    *rval = BOOLEAN_TO_JSVAL(JS_FALSE);
    break;
  default:
    *rval = INT_TO_JSVAL(r);
    break;
  }
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_sleep(JSContext *cx, JSObject *obj,
	 uintN argc, jsval *argv, jsval *rval)
{
  int msec;

  if (!JS_ConvertArguments(cx, argc, argv, "u", &msec))
    return JS_FALSE;

  jsrefcount s = JS_SuspendRequest(cx);
  usleep(msec * 1000);
  JS_ResumeRequest(cx, s);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_time(JSContext *cx, JSObject *obj,
	uintN argc, jsval *argv, jsval *rval)
{
  time_t t;
  time(&t);
  jsdouble *d = JS_NewDouble(cx, t);
  *rval = DOUBLE_TO_JSVAL(d);
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_durationtostring(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  int s;
  char tmp[32];
  if (!JS_ConvertArguments(cx, argc, argv, "u", &s))
    return JS_FALSE;

  int m = s / 60;
  int h = s / 3600;
  
  if(h > 0) {
    snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
  } else {
    snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
  }

  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, tmp));
  return JS_TRUE;
}

/**
 *
 */
static JSBool
js_textDialog(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
  const char *message;
  char *input;
  JSBool ok, cancel;
  int r;
  jsval val;

  if(!JS_ConvertArguments(cx, argc, argv, "sbb", &message, &ok, &cancel))
    return JS_FALSE;

  r = text_dialog(message, &input, 
		    (ok     ? MESSAGE_POPUP_OK : 0) |
		    (cancel ? MESSAGE_POPUP_CANCEL : 0) | 
		    MESSAGE_POPUP_RICH_TEXT);
  
  if(r == 1) {
    *rval = BOOLEAN_TO_JSVAL(0);
    return JS_TRUE;
  }

  obj = JS_NewObject(cx, NULL, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(obj);

  if(r == -1 || input == NULL) {
    val = BOOLEAN_TO_JSVAL(1);
    JS_SetProperty(cx, obj, "rejected", &val);
  } else {

    val = STRING_TO_JSVAL(JS_NewString(cx, input, strlen(input)));
    JS_SetProperty(cx, obj, "input", &val);
  }
  
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_decodeEntety(JSContext *cx, JSObject *obj,
		    uintN argc, jsval *argv, jsval *rval)
{
  const char *in;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &in))
    return JS_FALSE;

  char *out = strdup(in);
  html_entities_decode(out);
  *rval = STRING_TO_JSVAL(JS_NewString(cx, out, strlen(out)));
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_notify(JSContext *cx, JSObject *obj,
		uintN argc, jsval *argv, jsval *rval)
{
  const char *text;
  int delay;
  const char *icon = NULL;

  if(!JS_ConvertArguments(cx, argc, argv, "si/s", &text, &delay, &icon))
    return JS_FALSE;

  notify_add(NULL, NOTIFY_INFO, icon, delay, rstr_alloc("%s"), text);

  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_sysipaddr(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  netif_t *ni = net_get_interfaces();
  if(ni) {
    char buf[32];
    uint32_t myaddr = ni[0].ipv4;
    free(ni);
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d",
	     (uint8_t)(myaddr >> 24),
	     (uint8_t)(myaddr >> 16),
	     (uint8_t)(myaddr >> 8),
	     (uint8_t)(myaddr));

    *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, buf));
  } else {
    *rval = JSVAL_VOID;
  } 
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_webpopup(JSContext *cx, JSObject *obj,
            uintN argc, jsval *argv, jsval *rval)
{
  const char *url;
  const char *title;
  const char *trap;

  if(!JS_ConvertArguments(cx, argc, argv, "sss", &url, &title, &trap))
    return JS_FALSE;

  JSObject *robj = JS_NewObject(cx, NULL, NULL, NULL);
  *rval = OBJECT_TO_JSVAL(robj);

#if ENABLE_WEBPOPUP
  jsrefcount s = JS_SuspendRequest(cx);
  webpopup_result_t *wr = webpopup_create(url, title, trap);
  JS_ResumeRequest(cx, s);
  

  const char *t;
  switch(wr->wr_resultcode) {
  case WEBPOPUP_TRAPPED_URL:
    t = "trapped";
    break;
  case WEBPOPUP_CLOSED_BY_USER:
    t = "userclose";
    break;
  case WEBPOPUP_LOAD_ERROR:
    t = "neterror";
    break;
  }

  jsval val;

  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, t));
  JS_SetProperty(cx, robj, "result", &val);


  if(wr->wr_trapped.url != NULL) {
    val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, wr->wr_trapped.url));
    JS_SetProperty(cx, robj, "trappedUrl", &val);
  }
  
  JSObject *hdrs = JS_NewObject(cx, NULL, NULL, NULL);
  http_header_t *hh;
  LIST_FOREACH(hh, &wr->wr_trapped.qargs, hh_link) {
    jsval val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, hh->hh_value));
    JS_SetProperty(cx, hdrs, hh->hh_key, &val);
  }
  
  val = OBJECT_TO_JSVAL(hdrs);
  JS_SetProperty(cx, robj, "args", &val);

  webpopup_result_free(wr);
#else
  jsval val;

  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, "unsupported"));
  JS_SetProperty(cx, robj, "result", &val);
#endif
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_md5digest(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  uint8_t d[16];
  char ret[33];

  if(!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  md5_decl(ctx);
  md5_init(ctx);
  md5_update(ctx, (void *)str, strlen(str));
  md5_final(ctx, d);
  bin2hex(ret, sizeof(ret), d, sizeof(d));
  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, ret));
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_sha1digest(JSContext *cx, JSObject *obj,
	     uintN argc, jsval *argv, jsval *rval)
{
  const char *str;
  uint8_t d[20];
  char ret[41];

  if(!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;

  sha1_decl(ctx);
  sha1_init(ctx);
  sha1_update(ctx, (void *)str, strlen(str));
  sha1_final(ctx, d);
  bin2hex(ret, sizeof(ret), d, sizeof(d));
  *rval = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, ret));
  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_xmlrpc(JSContext *cx, JSObject *obj,
	  uintN argc, jsval *argv, jsval *rval)
{
  char errbuf[256];
  JSString *urlstr    = JS_ValueToString(cx, argv[0]);
  JSString *methodstr = JS_ValueToString(cx, argv[1]);

  htsmsg_t *args = htsmsg_create_list();

  argc -= 2;
  argv += 2;

  while(argc > 0) {
    js_htsmsg_emit_jsval(cx, argv[0], args, NULL);
    argc--;
    argv++;
  }

  jsrefcount s = JS_SuspendRequest(cx);

  htsmsg_t *reply = xmlrpc_request(JS_GetStringBytes(urlstr),
				   JS_GetStringBytes(methodstr),
				   args, errbuf, sizeof(errbuf));

  JS_ResumeRequest(cx, s);

  if(reply == NULL) {
    JS_ReportError(cx, errbuf);
    return JS_FALSE;
  }

  js_object_from_htsmsg(cx, reply, rval);
  htsmsg_destroy(reply);

  return JS_TRUE;
}



/**
 *
 */
static JSBool 
js_getsublang(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  int i;
  JSObject *o = JS_NewArrayObject(cx, 0, NULL);
  *rval = OBJECT_TO_JSVAL(o);
  for(i = 0; i < 3; i++) {
    const char *lang = i18n_subtitle_lang(i);
    if(lang)
      js_set_prop_str(cx, o, NULL, lang);
  }
  
  return JS_TRUE;
}





/**
 *
 */
static JSFunctionSpec showtime_functions[] = {
    JS_FS("trace",            js_trace,    1, 0, 0),
    JS_FS("print",            js_print,    1, 0, 0),
    JS_FS("httpGet",          js_httpGet, 2, 0, 0),
    JS_FS("httpPost",         js_httpPost, 2, 0, 0),
#if ENABLE_RELEASE == 0
    JS_FS("readFile",         js_readFile, 1, 0, 0),
#endif
    JS_FS("queryStringSplit", js_queryStringSplit, 1, 0, 0),
    JS_FS("pathEscape",       js_pathEscape, 1, 0, 0),
    JS_FS("paramEscape",      js_paramEscape, 1, 0, 0),
    JS_FS("canHandle",        js_canHandle, 1, 0, 0),
    JS_FS("message",          js_message, 3, 0, 0),
    JS_FS("sleep",            js_sleep, 1, 0, 0),
    JS_FS("JSONEncode",       js_json_encode, 1, 0, 0),
    JS_FS("JSONDecode",       js_json_decode, 1, 0, 0),
    JS_FS("time",             js_time, 0, 0, 0),
    JS_FS("durationToString", js_durationtostring, 0, 0, 0),
    JS_FS("probe",            js_probe, 1, 0, 0),
    JS_FS("textDialog",       js_textDialog, 3, 0, 0),
    JS_FS("entityDecode",     js_decodeEntety, 1, 0, 0),
    JS_FS("notify",           js_notify, 2, 0, 0),
    JS_FS("systemIpAddress",  js_sysipaddr, 0, 0, 0),
    JS_FS("webpopup",         js_webpopup, 3, 0, 0),
    JS_FS("md5digest",        js_md5digest, 1, 0, 0),
    JS_FS("sha1digest",       js_sha1digest, 1, 0, 0),
    JS_FS("xmlrpc",           js_xmlrpc, 3, 0, 0),
    JS_FS("getSubtitleLanguages", js_getsublang, 0, 0, 0),
    JS_FS_END
};



static JSClass showtime_class = {
  "showtime", 0,
  JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub,
  JSCLASS_NO_OPTIONAL_MEMBERS
};



/**
 *
 */
static JSBool 
js_RichText(JSContext *cx, JSObject *obj,
	    uintN argc, jsval *argv, jsval *rval)
{
  const char *str;

  if (!JS_ConvertArguments(cx, argc, argv, "s", &str))
    return JS_FALSE;
  jsval v = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str));

  JS_SetProperty(cx, obj, "text", &v);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static JSBool 
js_Link(JSContext *cx, JSObject *obj,
	uintN argc, jsval *argv, jsval *rval)
{
  const char *title, *url;

  if (!JS_ConvertArguments(cx, argc, argv, "ss", &title, &url))
    return JS_FALSE;
  jsval v1 = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, title));
  jsval v2 = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, url));

  JS_SetProperty(cx, obj, "title", &v1);
  JS_SetProperty(cx, obj, "url", &v2);

  *rval = JSVAL_VOID;
  return JS_TRUE;
}


/**
 *
 */
static void
plugin_finalize(JSContext *cx, JSObject *obj)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  assert(LIST_FIRST(&jsp->jsp_routes) == NULL);
  assert(LIST_FIRST(&jsp->jsp_searchers) == NULL);
  assert(LIST_FIRST(&jsp->jsp_http_auths) == NULL);

  TRACE(TRACE_DEBUG, "JS", "Plugin %s unloaded", jsp->jsp_url);
  
  LIST_REMOVE(jsp, jsp_link);
  
  free(jsp->jsp_url);
  free(jsp->jsp_id);
  free(jsp);
}


/**
 *
 */
static void
js_plugin_unload0(JSContext *cx, js_plugin_t *jsp)
{
  js_page_flush_from_plugin(cx, jsp);
  js_io_flush_from_plugin(cx, jsp);
  js_service_flush_from_plugin(cx, jsp);
  js_setting_group_flush_from_plugin(cx, jsp);
  js_event_destroy_handlers(cx, &jsp->jsp_event_handlers);
  js_subscription_flush_from_list(cx, &jsp->jsp_subscriptions);
  js_subprovider_flush_from_plugin(cx, jsp);
}

/**
 *
 */
static JSBool
js_forceUnload(JSContext *cx, JSObject *obj,
	      uintN argc, jsval *argv, jsval *rval)
{
  js_plugin_unload0(cx, JS_GetPrivate(cx, obj));
  *rval = JSVAL_VOID;
  return JS_TRUE;
}

/**
 *
 */
static JSBool 
jsp_setEnableURIRoute(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  jsp->jsp_enable_uri_routing = on;

  TRACE(TRACE_DEBUG, "plugins", "Plugin %s %sabled URI routing",
	jsp->jsp_id, on ? "en" : "dis");

  return JS_TRUE;
}

/**
 *
 */
static JSBool 
jsp_setEnableSearch(JSContext *cx, JSObject *obj, jsval idval, jsval *vp)
{ 
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);
  JSBool on;

  if(!JS_ValueToBoolean(cx, *vp, &on))
    return JS_FALSE;

  jsp->jsp_enable_search = on;

  TRACE(TRACE_DEBUG, "plugins", "Plugin %s %sabled search",
	jsp->jsp_id, on ? "en" : "dis");

  return JS_TRUE;
}


static JSBool
plugin_add_del_prop(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
  js_plugin_t *jsp = JS_GetPrivate(cx, obj);

  if(jsp->jsp_protect_object) {
    JS_ReportError(cx, "Plugin object can not be modified");
    return JS_FALSE;
  }
  return JS_TRUE;
}


/**
 *
 */
static JSClass plugin_class = {
  "plugin", JSCLASS_HAS_PRIVATE,
  plugin_add_del_prop,plugin_add_del_prop,JS_PropertyStub,JS_PropertyStub,
  JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub, plugin_finalize,
  JSCLASS_NO_OPTIONAL_MEMBERS
};


/**
 *
 */
static JSFunctionSpec plugin_functions[] = {
    JS_FS("addURI",           js_addURI,      2, 0, 0),
    JS_FS("addSearcher",      js_addSearcher, 3, 0, 0),
    JS_FS("addHTTPAuth",      js_addHTTPAuth, 2, 0, 0),
    JS_FS("forceUnload",      js_forceUnload, 0, 0, 0),
    JS_FS("createSettings",   js_createSettings, 2, 0, 0),
    JS_FS("createStore",   js_createStore, 1, 0, 0),
    JS_FS("createService",    js_createService, 4, 0, 0),
    JS_FS("getAuthCredentials",  js_getAuthCredentials, 3, 0, 0),
    JS_FS("onEvent",             js_onEvent, 2, 0, 0),
    JS_FS("cacheGet",         js_cache_get, 2, 0, 0),
    JS_FS("cachePut",         js_cache_put, 4, 0, 0),
    JS_FS("getDescriptor",    js_get_descriptor, 0, 0, 0),
    JS_FS("subscribe",        js_subscribe_global, 2, 0, 0),
    JS_FS("addSubtitleProvider", js_addsubprovider, 1, 0, 0),
    JS_FS_END
};


/**
 *
 */
void
js_plugin_unload(const char *id)
{
  JSContext *cx;
  js_plugin_t *jsp;

  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    if(!strcmp(jsp->jsp_id, id))
      break;

  if(jsp == NULL)
    return;

  fa_unreference(jsp->jsp_ref);
    
  cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  js_plugin_unload0(cx, jsp);

  JS_EndRequest(cx);
  JS_GC(cx);
  JS_DestroyContext(cx);
}


/**
 *
 */
int
js_plugin_load(const char *id, const char *url, char *errbuf, size_t errlen)
{
  char *sbuf;
  size_t size;
  JSContext *cx;
  js_plugin_t *jsp;
  JSObject *pobj, *gobj;
  JSScript *s;
  char path[PATH_MAX];
  jsval val;
  fa_handle_t *ref;
  
  ref = fa_reference(url);

  if((sbuf = fa_load(url, &size, NULL, errbuf, errlen, NULL, 0,
		     NULL, NULL)) == NULL) {
    fa_unreference(ref);
    return -1;
  }

  cx = js_newctx(err_reporter);
  JS_BeginRequest(cx);

  /* Remove any plugin with same URL */
  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    if(!strcmp(jsp->jsp_id, id))
      break;
  if(jsp != NULL)
    js_plugin_unload0(cx, jsp);

  jsp = calloc(1, sizeof(js_plugin_t));
  jsp->jsp_url = strdup(url);
  jsp->jsp_id  = strdup(id);
  jsp->jsp_ref = ref;
  
  LIST_INSERT_HEAD(&js_plugins, jsp, jsp_link);

  gobj = JS_NewObject(cx, &global_class, NULL, NULL);
  JS_InitStandardClasses(cx, gobj);

  JS_DefineProperty(cx, gobj, "showtime", OBJECT_TO_JSVAL(showtimeobj),
		    NULL, NULL, JSPROP_READONLY | JSPROP_PERMANENT);

  /* Plugin object */
  pobj = JS_NewObject(cx, &plugin_class, NULL, gobj);
  JS_AddNamedRoot(cx, &pobj, "plugin");

  JS_SetPrivate(cx, pobj, jsp);

  JS_DefineFunctions(cx, pobj, plugin_functions);


  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, url));
  JS_SetProperty(cx, pobj, "url", &val);

  if(!fa_parent(path, sizeof(path), url)) {
    val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, path));
    JS_SetProperty(cx, pobj, "path", &val);
  }

  JS_DefineProperty(cx, pobj, "URIRouting", BOOLEAN_TO_JSVAL(1),
		    NULL, jsp_setEnableURIRoute, JSPROP_PERMANENT);
  jsp->jsp_enable_uri_routing = 1;

  JS_DefineProperty(cx, pobj, "search", BOOLEAN_TO_JSVAL(1),
		    NULL, jsp_setEnableSearch, JSPROP_PERMANENT);
  jsp->jsp_enable_search = 1;

  jsp->jsp_protect_object = 1;

  s = JS_CompileScript(cx, pobj, sbuf, size, url, 1);
  free(sbuf);

  if(s != NULL) {
    JSObject *sobj = JS_NewScriptObject(cx, s);
    jsval result;

    JS_AddNamedRoot(cx, &sobj, "script");
    JS_ExecuteScript(cx, pobj, s, &result);
    JS_RemoveRoot(cx, &sobj);
  }

  JS_RemoveRoot(cx, &pobj);
  JS_EndRequest(cx);
  JS_GC(cx);
  JS_DestroyContext(cx);
  return 0;
}


/**
 * Prop lockmanager for locking JS global context
 */
static void
js_lockmgr(void *ptr, int lock)
{
  if(lock)
    JS_BeginRequest(ptr);
  else
    JS_EndRequest(ptr);
}


static void
js_global_pc_prologue(void)
{
  JS_SetContextThread(js_global_cx);
}


static void
js_global_pc_epilogue(void)
{
  JS_ClearContextThread(js_global_cx);
}



static void
js_global_event(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  va_start(ap, event);

  if(event != PROP_EXT_EVENT)
    return;

  event_t *e = va_arg(ap, event_t *);
  js_plugin_t *jsp;

  LIST_FOREACH(jsp, &js_plugins, jsp_link)
    js_event_dispatch(js_global_cx, &jsp->jsp_event_handlers, e, NULL);
  va_end(ap);
}


/**
 *
 */
static int
js_init(void)
{
  JSContext *cx;
  jsval val;
  JSFunction *fn;

  js_page_init();
  js_metaprovider_init();

  JS_SetCStringsAreUTF8();

  runtime = JS_NewRuntime(0x1000000);

  cx = js_newctx(err_reporter);

  JS_BeginRequest(cx);

  showtimeobj = JS_NewObject(cx, &showtime_class, NULL, NULL);
  JS_DefineFunctions(cx, showtimeobj, showtime_functions);

  val = INT_TO_JSVAL(showtime_get_version_int());
  JS_SetProperty(cx, showtimeobj, "currentVersionInt", &val);

  val = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, htsversion));
  JS_SetProperty(cx, showtimeobj, "currentVersionString", &val);


  fn = JS_DefineFunction(cx, showtimeobj, "RichText", js_RichText, 1, 0);
  RichText = JS_GetFunctionObject(fn);

  fn = JS_DefineFunction(cx, showtimeobj, "Link", js_Link, 2, 0);
  Link = JS_GetFunctionObject(fn);
	     
  JS_AddNamedRoot(cx, &showtimeobj, "showtime");

  js_global_cx = cx;
  JS_EndRequest(cx);
  JS_ClearContextThread(cx);
  js_global_pc = prop_courier_create_lockmgr("js", js_lockmgr, cx,
					     js_global_pc_prologue,
					     js_global_pc_epilogue);

  js_event_sub = prop_subscribe(0,
				PROP_TAG_CALLBACK, js_global_event, NULL,
				PROP_TAG_NAME("global", "eventsink"),
				PROP_TAG_COURIER, js_global_pc,
				NULL);

  return 0;
}



/**
 *
 */
static void
js_fini(void)
{
  js_plugin_t *jsp, *n;
  JSContext *cx;

  cx = js_newctx(NULL);
  JS_BeginRequest(cx);

  for(jsp = LIST_FIRST(&js_plugins); jsp != NULL; jsp = n) {
    n = LIST_NEXT(jsp, jsp_link);
    js_plugin_unload0(cx, jsp);
  }

  JS_RemoveRoot(cx, &showtimeobj);

  JS_EndRequest(cx);
  JS_GC(cx);
  JS_DestroyContext(cx);


  prop_unsubscribe(js_event_sub);

  prop_courier_destroy(js_global_pc);

  cx = js_global_cx;
  JS_SetContextThread(cx);
  JS_DestroyContext(cx);

  JS_DestroyRuntime(runtime);
  JS_ShutDown();
}




static void *
js_load_thread(void *aux)
{
  const char *url = aux;
  char errbuf[128];
  
  if(js_plugin_load("test-fromcmdline", url, errbuf, sizeof(errbuf)))
    TRACE(TRACE_ERROR, "JS", "Unable to load %s -- %s", url, errbuf);
  return NULL;
}

/**
 *
 */
void
js_load(const char *url)
{
  char *u = strdup(url);
  
  hts_thread_create_detached("rawjs", js_load_thread, u, THREAD_PRIO_LOW);
}


/**
 *
 */
static backend_t be_js = {
  .be_init = js_init,
  .be_fini = js_fini,
  .be_flags = BACKEND_OPEN_CHECKS_URI,
  .be_open = js_backend_open,
  .be_search = js_backend_search,
};

BE_REGISTER(js);
