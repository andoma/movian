#include <unistd.h>
#include <assert.h>

#include "ecmascript.h"
#include "fileaccess/http_client.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "htsmsg/htsbuf.h"
#include "task.h"

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

  ehr->ehr_headreq = es_prop_is_true(ctx, 1, "headRequest");
  ehr->ehr_min_expire = es_prop_to_int(ctx, 1, "cacheTime", 0);
  ehr->ehr_cache = es_prop_is_true(ctx, 1, "caching") || ehr->ehr_min_expire;

  duk_get_prop_string(ctx, 1, "postdata");

  if(duk_is_object(ctx, -1)) {
    const char *prefix = "";

    ehr->ehr_postdata = malloc(sizeof(htsbuf_queue_t));
    htsbuf_queue_init(ehr->ehr_postdata, 0);

    duk_enum(ctx, -1, 0);

    while(duk_next(ctx, -1, 1)) {
      const char *k = duk_safe_to_string(ctx, -2);
      const char *v = duk_safe_to_string(ctx, -1);

      if(prefix)
        htsbuf_append(ehr->ehr_postdata, prefix, strlen(prefix));
      htsbuf_append_and_escape_url(ehr->ehr_postdata, k);
      htsbuf_append(ehr->ehr_postdata, "=", 1);
      htsbuf_append_and_escape_url(ehr->ehr_postdata, v);
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

  duk_pop(ctx);

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
    es_resource_link(&ehr->super, ec);
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


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_io[] = {
  { "httpReq",              es_http_req,      3 },
  { NULL, NULL, 0}
};
