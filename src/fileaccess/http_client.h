#pragma once

#include <stdarg.h>
#include "networking/http.h"
#include "fa_defs.h"
#include "compiler.h"

// HTTP client

enum {
  HTTP_TAG_ARG = 1,
  HTTP_TAG_ARGINT,
  HTTP_TAG_ARGINT64,
  HTTP_TAG_ARGBIN,
  HTTP_TAG_ARGLIST,
  HTTP_TAG_RESULT_PTR,
  HTTP_TAG_ERRBUF,
  HTTP_TAG_POSTDATA,
  HTTP_TAG_FLAGS,
  HTTP_TAG_REQUEST_HEADER,
  HTTP_TAG_REQUEST_HEADERS,
  HTTP_TAG_RESPONSE_HEADERS,
  HTTP_TAG_METHOD,
  HTTP_TAG_PROGRESS_CALLBACK,
  HTTP_TAG_CANCELLABLE,
  HTTP_TAG_CONNECT_TIMEOUT,
  HTTP_TAG_READ_TIMEOUT,
};


#define HTTP_ARG(a, b)                     HTTP_TAG_ARG, a, b
#define HTTP_ARGINT(a, b)                  HTTP_TAG_ARGINT, a, b
#define HTTP_ARGINT64(a, b)                HTTP_TAG_ARGINT64, a, b
#define HTTP_ARGBIN(a, b, c)               HTTP_TAG_ARGBIN, a, b, c
#define HTTP_ARGLIST(a)                    HTTP_TAG_ARGLIST, a
#define HTTP_RESULT_PTR(a)                 HTTP_TAG_RESULT_PTR, a
#define HTTP_ERRBUF(a, b)                  HTTP_TAG_ERRBUF, a, b
#define HTTP_POSTDATA(a, b)                HTTP_TAG_POSTDATA, a, b
#define HTTP_FLAGS(a)                      HTTP_TAG_FLAGS, a
#define HTTP_REQUEST_HEADER(a, b)          HTTP_TAG_REQUEST_HEADER, a, b
#define HTTP_REQUEST_HEADERS(a)            HTTP_TAG_REQUEST_HEADERS, a
#define HTTP_RESPONSE_HEADERS(a)           HTTP_TAG_RESPONSE_HEADERS, a
#define HTTP_METHOD(a)                     HTTP_TAG_METHOD, a
#define HTTP_PROGRESS_CALLBACK(a, b)       HTTP_TAG_PROGRESS_CALLBACK, a, b
#define HTTP_CANCELLABLE(a)                HTTP_TAG_CANCELLABLE, a
#define HTTP_CONNECT_TIMEOUT(a)            HTTP_TAG_CONNECT_TIMEOUT, a
#define HTTP_READ_TIMEOUT(a)               HTTP_TAG_READ_TIMEOUT, a

/**
 * Tell HTTP client to create an internal buffer. To be used when
 * in async mode
 */
#define HTTP_BUFFER_INTERNALLY             ((void *)-1)


/**
 *
 */
struct http_auth_req {
  const char *har_method;
  const char **har_parameters;
  const struct http_file *har_hf;
  struct http_header_list *har_headers;
  struct http_header_list *har_cookies;
  char *har_errbuf;
  size_t har_errlen;
  int har_force_fail;

} http_auth_req_t;




/**
 * Request object, used when in async mode
 */
typedef struct http_req_aux http_req_aux_t;


/**
 *
 */
int http_req(const char *url, ...) attribute_null_sentinel;

int http_reqv(const char *url, va_list ap,
              void (*async_callback)(http_req_aux_t *hra, void *opaque,
                                     int error),
              void *async_opaque);

struct buf *http_req_get_result(http_req_aux_t *hra);

void http_req_release(http_req_aux_t *hra);

http_req_aux_t *http_req_retain(http_req_aux_t *hra) attribute_unused_result;

int http_client_oauth(struct http_auth_req *har,
		      const char *consumer_key,
		      const char *consumer_secret,
		      const char *token,
		      const char *token_secret);

int http_client_rawauth(struct http_auth_req *har, const char *str);

void http_client_set_header(struct http_auth_req *har, const char *key,
			    const char *value);

void http_client_set_cookie(struct http_auth_req *har, const char *key,
			    const char *value);

void http_client_fail_req(struct http_auth_req *har, const char *reason);


