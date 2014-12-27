#include <unistd.h>
#include <assert.h>

#include "ecmascript.h"
#include "fileaccess/http_client.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "misc/regex.h"
#include "htsmsg/htsbuf.h"
#include "task.h"
#include "backend/backend.h"

/**
 *
 */
typedef struct es_http_request {
  es_resource_t super;

  char *ehr_url;
  struct http_header_list ehr_request_headers;
  struct http_header_list ehr_response_headers;
  htsbuf_queue_t *ehr_postdata;
  char *ehr_postcontenttype;
  char *ehr_method;
  char **ehr_httpargs;

  int ehr_flags;
  int ehr_headreq;
  int ehr_min_expire;
  int ehr_cache;

  buf_t *ehr_result;

  char ehr_errbuf[512];

  int ehr_error;

} es_http_request_t;




/**
 *
 */
static void
ehr_cleanup(es_http_request_t *ehr)
{
  free(ehr->ehr_url);
  http_headers_free(&ehr->ehr_response_headers);
  if(ehr->ehr_postdata != NULL)
    htsbuf_queue_flush(ehr->ehr_postdata);
  free(ehr->ehr_postdata);
  free(ehr->ehr_postcontenttype);
  free(ehr->ehr_method);
  buf_release(ehr->ehr_result);
}


/**
 *
 */
static void
es_http_request_destroy(es_resource_t *eres)
{
  es_http_request_t *ehr = (es_http_request_t *)eres;
  ehr_cleanup(ehr);
  es_root_unregister(eres->er_ctx->ec_duk, eres);
  es_resource_unlink(&ehr->super);
}


/**
 *
 */
static void
es_http_request_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_http_request_t *ehr = (es_http_request_t *)eres;
  snprintf(dst, dstsize, "%s", ehr->ehr_url);
}


/**
 *
 */
static const es_resource_class_t es_resource_http_request = {
  .erc_name = "http_request",
  .erc_size = sizeof(es_http_request_t),
  .erc_destroy = es_http_request_destroy,
  .erc_info = es_http_request_info,
};




/**
 *
 */
static void
http_add_args(duk_context *ctx, char ***httpargs)
{
  duk_enum(ctx, -1, 0);

  while(duk_next(ctx, -1, 1)) {
    strvec_addp(httpargs, duk_safe_to_string(ctx, -2));
    strvec_addp(httpargs, duk_safe_to_string(ctx, -1));
    duk_pop_2(ctx);
  }
  duk_pop(ctx);
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
static void
es_http_do_request(es_http_request_t *ehr)
{
  if(ehr->ehr_cache &&
     ehr->ehr_method == NULL &&
     ehr->ehr_headreq == 0 &&
     ehr->ehr_postdata == NULL) {

    /**
     * If it's a GET request and cache is enabled, run it thru
     * fa_load() to get caching
     */

    ehr->ehr_result =
      fa_load(ehr->ehr_url,
              FA_LOAD_ERRBUF(ehr->ehr_errbuf, sizeof(ehr->ehr_errbuf)),
              FA_LOAD_QUERY_ARGVEC(ehr->ehr_httpargs),
              FA_LOAD_FLAGS(ehr->ehr_flags),
              FA_LOAD_MIN_EXPIRE(ehr->ehr_min_expire),
              FA_LOAD_REQUEST_HEADERS(&ehr->ehr_request_headers),
              FA_LOAD_RESPONSE_HEADERS(&ehr->ehr_response_headers),
              NULL);

    if(ehr->ehr_result == NULL)
      ehr->ehr_error = 1;

  } else {

    ehr->ehr_error =
      http_req(ehr->ehr_url,
               HTTP_ARGLIST(ehr->ehr_httpargs),
               HTTP_RESULT_PTR(ehr->ehr_headreq ? NULL : &ehr->ehr_result),
               HTTP_ERRBUF(ehr->ehr_errbuf, sizeof(ehr->ehr_errbuf)),
               HTTP_POSTDATA(ehr->ehr_postdata, ehr->ehr_postcontenttype),
               HTTP_FLAGS(ehr->ehr_flags),
               HTTP_RESPONSE_HEADERS(&ehr->ehr_response_headers),
               HTTP_REQUEST_HEADERS(&ehr->ehr_request_headers),
               HTTP_METHOD(ehr->ehr_method),
               NULL);

    if(ehr->ehr_error)
      ehr->ehr_result = NULL;

  }

  if(ehr->ehr_httpargs != NULL)
    strvec_free(ehr->ehr_httpargs);

  http_headers_free(&ehr->ehr_request_headers);
}


/**
 *
 */
static void
es_http_push_result(duk_context *ctx, es_http_request_t *ehr)
{
  int res_idx = duk_push_object(ctx);

  void *ptr = duk_push_fixed_buffer(ctx, buf_len(ehr->ehr_result));
  memcpy(ptr, buf_data(ehr->ehr_result), buf_len(ehr->ehr_result));
  duk_put_prop_string(ctx, res_idx, "buffer");

  int arr_idx = duk_push_array(ctx);

  const http_header_t *hh;
  int idx = 0;
  LIST_FOREACH(hh, &ehr->ehr_response_headers, hh_link) {
    duk_push_string(ctx, hh->hh_key);
    duk_put_prop_index(ctx, arr_idx, idx++);
    duk_push_string(ctx, hh->hh_value);
    duk_put_prop_index(ctx, arr_idx, idx++);
  }

  http_headers_free(&ehr->ehr_response_headers);
  duk_put_prop_string(ctx, res_idx, "responseheaders");
}



/**
 *
 */
static void
ehr_task(void *aux)
{
  es_http_request_t *ehr = aux;
  es_http_do_request(ehr);


  es_context_t *ec = ehr->super.er_ctx;
  duk_context *ctx = ec->ec_duk;

  es_context_begin(ec);

  es_push_root(ctx, ehr);

  if(ehr->ehr_error == 0) {
    duk_push_boolean(ctx, 0);
    es_http_push_result(ctx, ehr);
  } else {
    duk_push_string(ctx, ehr->ehr_errbuf);
    duk_push_undefined(ctx);
  }

  int rc = duk_pcall(ctx, 2);
  if(rc)
    es_dump_err(ctx);

  duk_pop(ctx);

  es_resource_destroy(&ehr->super);

  es_context_end(ec);
}


/**
 *
 */
static int
es_http_req(duk_context *ctx)
{
  const char *url = duk_to_string(ctx, 0);

  es_context_t *ec = es_get(ctx);
  es_http_request_t *ehr = es_resource_alloc(&es_resource_http_request);

  ehr->ehr_url = strdup(url);
  LIST_INIT(&ehr->ehr_request_headers);
  LIST_INIT(&ehr->ehr_response_headers);

  ehr->ehr_flags |= es_prop_is_true(ctx, 1, "debug")       * FA_DEBUG;
  ehr->ehr_flags |= es_prop_is_true(ctx, 1, "noFollow")    * FA_NOFOLLOW;
  ehr->ehr_flags |= es_prop_is_true(ctx, 1, "compression") * FA_COMPRESSION;
  ehr->ehr_flags |= es_prop_is_true(ctx, 1, "noAuth")      * FA_DISABLE_AUTH;

  ehr->ehr_headreq = es_prop_is_true(ctx, 1, "headRequest");
  ehr->ehr_min_expire = es_prop_to_int(ctx, 1, "cacheTime", 0);
  ehr->ehr_cache = es_prop_is_true(ctx, 1, "caching") || ehr->ehr_min_expire;


  // -- Handle 'headers' argument

  duk_get_prop_string(ctx, 1, "headers");

  if(duk_is_object(ctx, -1)) {

    duk_enum(ctx, -1, 0);

    while(duk_next(ctx, -1, 1)) {
      if(duk_is_object_coercible(ctx, -1)) {
        const char *k = duk_safe_to_string(ctx, -2);
        const char *v = duk_safe_to_string(ctx, -1);

        http_header_add(&ehr->ehr_request_headers, k, v, 0);
      }
      duk_pop_2(ctx);
    }
    duk_pop(ctx); // Pop iterator
  }
  duk_pop(ctx); // pop headers object


  // --- Handle 'postdata' argument

  duk_get_prop_string(ctx, 1, "postdata");

  if(duk_is_object(ctx, -1)) {
    const char *prefix = "";

    ehr->ehr_postdata = malloc(sizeof(htsbuf_queue_t));
    htsbuf_queue_init(ehr->ehr_postdata, 0);

    duk_enum(ctx, -1, 0);

    while(duk_next(ctx, -1, 1)) {
      if(duk_is_object_coercible(ctx, -1)) {
        const char *k = duk_safe_to_string(ctx, -2);
        const char *v = duk_safe_to_string(ctx, -1);

        if(prefix)
          htsbuf_append(ehr->ehr_postdata, prefix, strlen(prefix));
        htsbuf_append_and_escape_url(ehr->ehr_postdata, k);
        htsbuf_append(ehr->ehr_postdata, "=", 1);
        htsbuf_append_and_escape_url(ehr->ehr_postdata, v);
        prefix="&";
      }
      duk_pop_2(ctx);
    }
    duk_pop(ctx); // Pop iterator

    ehr->ehr_postcontenttype = strdup("application/x-www-form-urlencoded");

  } else if(duk_is_string(ctx, -1)) {
    duk_size_t len;
    const char *str = duk_get_lstring(ctx, -1, &len);
    ehr->ehr_postdata = malloc(sizeof(htsbuf_queue_t));
    htsbuf_queue_init(ehr->ehr_postdata, 0);
    htsbuf_append(ehr->ehr_postdata, str, len);
    ehr->ehr_postcontenttype =  strdup("text/ascii");
  } else {

  }

  duk_pop(ctx); // pop postdata object

  /**
   * Extract args from control object
   */

  duk_get_prop_string(ctx, 1, "args");
  if(duk_is_object(ctx, -1))
    http_add_args(ctx, &ehr->ehr_httpargs);

  duk_pop(ctx);

  /**
   * Extract method from control object
   */

  duk_get_prop_string(ctx, 1, "method");
  const char *method = duk_get_string(ctx, -1);
  ehr->ehr_method = method ? strdup(method) : NULL;
  duk_pop(ctx);

  /**
   * If user add specific HTTP headers we will disable caching
   * A few header types are OK to send though since I don't
   * think it will affect result that much
   */
  if(ehr->ehr_cache)
    ehr->ehr_cache = !disable_cache_on_http_headers(&ehr->ehr_request_headers);

  if(duk_is_function(ctx, 2)) {
    // Async mode
    es_resource_link(&ehr->super, ec, 1);
    es_root_register(ctx, 2, ehr);
    task_run(ehr_task, ehr);
    return 0;
  }

  es_http_do_request(ehr);

  if(ehr->ehr_error)
    duk_error(ctx, DUK_ERR_ERROR, "HTTP request failed %s -- %s",
              ehr->ehr_url, ehr->ehr_errbuf);

  es_http_push_result(ctx, ehr);
  ehr_cleanup(ehr);
  free(ehr);
  return 1;
}



LIST_HEAD(es_http_inspector_list, es_http_inspector);

typedef struct es_http_inspector {
  es_resource_t super;
  LIST_ENTRY(es_http_inspector) ehi_link;
  char *ehi_pattern;
  hts_regex_t ehi_regex;
  int ehi_prio;
} es_http_inspector_t;

static struct es_http_inspector_list http_inspectors;

static HTS_MUTEX_DECL(http_inspector_mutex);

#define HRINAME "\xff""hri"

/**
 *
 */
static void
es_http_inspector_destroy(es_resource_t *eres)
{
  es_http_inspector_t *ehi = (es_http_inspector_t *)eres;

  es_root_unregister(eres->er_ctx->ec_duk, eres);

  hts_mutex_lock(&http_inspector_mutex);
  LIST_REMOVE(ehi, ehi_link);
  hts_mutex_unlock(&http_inspector_mutex);

  free(ehi->ehi_pattern);
  hts_regfree(&ehi->ehi_regex);

  es_resource_unlink(&ehi->super);
}


/**
 *
 */
static void
es_http_inspector_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_http_inspector_t *ehi = (es_http_inspector_t *)eres;
  snprintf(dst, dstsize, "%s (prio:%d)", ehi->ehi_pattern, ehi->ehi_prio);
}


/**
 *
 */
static const es_resource_class_t es_resource_http_inspector = {
  .erc_name = "http-inspector",
  .erc_size = sizeof(es_http_inspector_t),
  .erc_destroy = es_http_inspector_destroy,
  .erc_info = es_http_inspector_info,
};


/**
 *
 */
static int
ehi_cmp(const es_http_inspector_t *a, const es_http_inspector_t *b)
{
  return b->ehi_prio - a->ehi_prio;
}


/**
 *
 */
static int
es_http_inspector_create(duk_context *ctx)
{
  const char *str = duk_safe_to_string(ctx, 0);

  if(str[0] != '^') {
    int l = strlen(str);
    char *s = alloca(l + 2);
    s[0] = '^';
    memcpy(s+1, str, l+1);
    str = s;
  }

  es_context_t *ec = es_get(ctx);

  hts_mutex_lock(&http_inspector_mutex);

  es_http_inspector_t *ehi;

  ehi = es_resource_alloc(&es_resource_http_inspector);
  if(hts_regcomp(&ehi->ehi_regex, str)) {
    hts_mutex_unlock(&http_inspector_mutex);
    free(ehi);
    duk_error(ctx, DUK_ERR_ERROR,
              "Invalid regular expression for http_inspector %s",
              str);
  }

  ehi->ehi_pattern = strdup(str);
  ehi->ehi_prio = strlen(str);

  LIST_INSERT_SORTED(&http_inspectors, ehi, ehi_link, ehi_cmp,
                     es_http_inspector_t);

  es_resource_link(&ehi->super, ec, 1);

  hts_mutex_unlock(&http_inspector_mutex);

  es_root_register(ctx, 1, ehi);

  es_resource_push(ctx, &ehi->super);
  return 1;
}



/**
 *
 */
static http_request_inspection_t *
get_hri(duk_context *ctx)
{
  duk_push_this(ctx);
  duk_get_prop_string(ctx, -1, HRINAME);
  http_request_inspection_t *hri = duk_get_pointer(ctx, -1);
  duk_pop_2(ctx);
  return hri;
}


/**
 *
 */
static int
es_http_inspector_fail(duk_context *ctx)
{
  http_client_fail_req(get_hri(ctx), duk_safe_to_string(ctx, 0));
  return 0;
}


/**
 *
 */
static int
es_http_inspector_set_header(duk_context *ctx)
{
  http_client_set_header(get_hri(ctx),
                         duk_safe_to_string(ctx, 0),
                         duk_safe_to_string(ctx, 1));
  return 0;
}


/**
 *
 */
static int
es_http_inspector_set_cookie(duk_context *ctx)
{
  http_client_set_cookie(get_hri(ctx),
                         duk_safe_to_string(ctx, 0),
                         duk_safe_to_string(ctx, 1));
  return 0;
}


/**
 * Inspector req object exposed functions
 */
const static duk_function_list_entry fnlist_inspector[] = {
  { "fail",              es_http_inspector_fail,        1 },
  { "setHeader",         es_http_inspector_set_header,  2 },
  { "setCookie",         es_http_inspector_set_cookie,  2 },
  { NULL, NULL, 0}
};


/**
 * Return 1 if we didn't do any processing
 */
static int
es_http_inspect(const char *url, http_request_inspection_t *hri)
{
  hts_regmatch_t matches[8];
  es_http_inspector_t *ehi;

  hts_mutex_lock(&http_inspector_mutex);

  LIST_FOREACH(ehi, &http_inspectors, ehi_link) {
    if(!hts_regexec(&ehi->ehi_regex, url, 8, matches, 0))
      break;
  }

  if(ehi == NULL) {
    hts_mutex_unlock(&http_inspector_mutex);
    return 1;
  }

  es_resource_retain(&ehi->super);

  hts_mutex_unlock(&http_inspector_mutex);

  es_context_t *ec = ehi->super.er_ctx;
  es_context_begin(ec);

  duk_context *ctx = ec->ec_duk;


  // Setup object to pass to callback

  int obj_idx = duk_push_object(ctx);

  duk_put_function_list(ctx, obj_idx, fnlist_inspector);

  duk_push_pointer(ctx, hri);
  duk_put_prop_string(ctx, obj_idx, HRINAME);

  duk_push_string(ctx, url);
  duk_put_prop_string(ctx, obj_idx, "url");


  // Push callback function
  es_push_root(ctx, ehi);
  duk_dup(ctx, obj_idx);

  int rval;
  int rc = duk_pcall(ctx, 1);
  if(rc) {
    es_dump_err(ctx);
    rval = 1;
  } else {
    rval = duk_get_boolean(ctx, -1);
  }

  duk_del_prop_string(ctx, obj_idx, HRINAME);
  duk_pop_2(ctx);

  es_context_end(ec);
  es_resource_release(&ehi->super);

  return rval;
}


REGISTER_HTTP_REQUEST_INSPECTOR(es_http_inspect);


/**
 *
 */
static int
es_probe(duk_context *ctx)
{
  const char *url = duk_require_string(ctx, 0);
  char errbuf[256];
  backend_probe_result_t res;

  res = backend_probe(url, errbuf, sizeof(errbuf));

  duk_push_object(ctx);

  if(res != BACKEND_PROBE_OK) {
    duk_push_string(ctx, errbuf);
    duk_put_prop_string(ctx, -2, "errmsg");
  }

  duk_push_int(ctx, res);
  duk_put_prop_string(ctx, -2, "result");
  return 1;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_io[] = {
  { "httpReq",              es_http_req,              3 },
  { "httpInspectorCreate",  es_http_inspector_create, 2 },
  { "probe",                es_probe,                 1 },
  { NULL, NULL, 0}
};
