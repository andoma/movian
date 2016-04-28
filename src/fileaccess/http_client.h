/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
  HTTP_TAG_LOCATION,
  HTTP_TAG_RESPONSE_CODE,
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
#define HTTP_LOCATION(a)                   HTTP_TAG_LOCATION, a
#define HTTP_RESPONSE_CODE(a)              HTTP_TAG_RESPONSE_CODE, a

/**
 * Tell HTTP client to create an internal buffer. To be used when
 * in async mode
 */
#define HTTP_BUFFER_INTERNALLY             ((void *)-1)


/**
 *
 */
typedef struct http_request_inspection {
  const char *hri_method;
  const char **hri_parameters;
  const struct http_file *hri_hf;
  struct http_header_list *hri_headers;
  struct http_header_list *hri_cookies;
  char *hri_errbuf;
  size_t hri_errlen;
  int hri_force_fail;
  int hri_auth_has_failed;

} http_request_inspection_t;


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

int http_client_rawauth(http_request_inspection_t *hri, const char *str);

void http_client_set_header(http_request_inspection_t *hri, const char *key,
			    const char *value);

void http_client_set_cookie(http_request_inspection_t *hri, const char *key,
			    const char *value);

void http_client_fail_req(http_request_inspection_t *hri, const char *reason);


/**
 *
 */
typedef struct http_request_inspector {
  LIST_ENTRY(http_request_inspector) link;
  int (*check)(const char *url, http_request_inspection_t *hri);
} http_request_inspector_t;

void http_request_inspector_register(http_request_inspector_t *hri);


#define REGISTER_HTTP_REQUEST_INSPECTOR(a)			   \
  static http_request_inspector_t http_request_inspector = {       \
    .check = a,                                                    \
  };								   \
  INITIALIZER(http_request_inspector_initializer)                  \
  { http_request_inspector_register(&http_request_inspector);      \
 }
