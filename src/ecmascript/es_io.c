#include <assert.h>

#include "ecmascript.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "htsmsg/htsbuf.h"

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
static int
es_http_req(duk_context *ctx)
{
  struct http_header_list request_headers;
  struct http_header_list response_headers;
  htsbuf_queue_t *postdata = NULL;
  const char *postcontenttype = NULL;
  const char *method = NULL;

  LIST_INIT(&request_headers);
  LIST_INIT(&response_headers);

  const char *url = duk_to_string(ctx, 0);

  int flags = 0;

  flags |= es_prop_is_true(ctx, 1, "debug")       * FA_DEBUG;
  flags |= es_prop_is_true(ctx, 1, "noFollow")    * FA_NOFOLLOW;
  flags |= es_prop_is_true(ctx, 1, "compression") * FA_COMPRESSION;

  const int headreq = es_prop_is_true(ctx, 1, "headRequest");
  int min_expire    = es_prop_to_int(ctx, 1, "cacheTime", 0);
  int cache         = es_prop_is_true(ctx, 1, "caching") || min_expire;

  /**
   * Extract args from control object
   */

  char **httpargs = NULL;
  duk_get_prop_string(ctx, 1, "args");
  if(duk_is_object(ctx, -1))
    http_add_args(ctx, &httpargs);

  duk_pop(ctx);

  /**
   * Extract method from control object
   */

  duk_get_prop_string(ctx, 1, "method");
  method = duk_get_string(ctx, -1);

  /**
   * If user add specific HTTP headers we will disable caching
   * A few header types are OK to send though since I don't
   * think it will affect result that much
   */
  if(cache)
    cache = !disable_cache_on_http_headers(&request_headers);

  char errbuf[512];

  buf_t *result = NULL;

  if(cache && method == NULL && !headreq && !postdata) {

    /**
     * If it's a GET request and cache is enabled, run it thru
     * fa_load() to get caching
     */

    result = fa_load(url,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     FA_LOAD_QUERY_ARGVEC(httpargs),
                     FA_LOAD_FLAGS(flags),
                     FA_LOAD_MIN_EXPIRE(min_expire),
                     FA_LOAD_REQUEST_HEADERS(&request_headers),
                     FA_LOAD_RESPONSE_HEADERS(&response_headers),
                     NULL);

    http_headers_free(&request_headers);

    if(httpargs)
      strvec_free(httpargs);

    if(result == NULL)
      duk_error(ctx, DUK_ERR_ERROR, "HTTP request failed %s -- %s",
                url, errbuf);

  } else {

    int n = http_req(url,
                     HTTP_ARGLIST(httpargs),
                     HTTP_RESULT_PTR(headreq ? NULL : &result),
                     HTTP_ERRBUF(errbuf, sizeof(errbuf)),
                     HTTP_POSTDATA(postdata, postcontenttype),
                     HTTP_FLAGS(flags),
                     HTTP_RESPONSE_HEADERS(&response_headers),
                     HTTP_REQUEST_HEADERS(&request_headers),
                     HTTP_METHOD(method),
                     NULL);

    http_headers_free(&request_headers);

    if(httpargs)
      strvec_free(httpargs);

    if(postdata != NULL)
      htsbuf_queue_flush(postdata);

    if(n)
      duk_error(ctx, DUK_ERR_ERROR, "HTTP request failed %s -- %s",
                url, errbuf);
  }

  int res_idx = duk_push_object(ctx);

  void *ptr = duk_push_fixed_buffer(ctx, buf_len(result));
  memcpy(ptr, buf_data(result), buf_len(result));
  duk_put_prop_string(ctx, res_idx, "buffer");

  int arr_idx = duk_push_array(ctx);

  const http_header_t *hh;
  int idx = 0;
  LIST_FOREACH(hh, &response_headers, hh_link) {
    duk_push_string(ctx, hh->hh_key);
    duk_put_prop_index(ctx, arr_idx, idx++);
    duk_push_string(ctx, hh->hh_value);
    duk_put_prop_index(ctx, arr_idx, idx++);
  }

  http_headers_free(&response_headers);
  duk_put_prop_string(ctx, res_idx, "responseheaders");
  return 1;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_io[] = {
  { "httpReq",              es_http_req,      2 },
  { NULL, NULL, 0}
};
