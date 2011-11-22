/*
 *  Showtime HTTP common code
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

#ifndef HTTP_H__
#define HTTP_H__

#include <time.h>
#include "misc/queue.h"


typedef enum {
    HTTP_CMD_GET,
    HTTP_CMD_HEAD,
    HTTP_CMD_POST,
    HTTP_CMD_SUBSCRIBE,
    HTTP_CMD_UNSUBSCRIBE,
} http_cmd_t;


#define HTTP_STATUS_OK           200
#define HTTP_STATUS_FOUND        302
#define HTTP_STATUS_BAD_REQUEST  400
#define HTTP_STATUS_UNAUTHORIZED 401
#define HTTP_STATUS_NOT_FOUND    404
#define HTTP_STATUS_METHOD_NOT_ALLOWED 405
#define HTTP_STATUS_PRECONDITION_FAILED 412
#define HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE 415
#define HTTP_NOT_IMPLEMENTED 501

LIST_HEAD(http_header_list, http_header);

typedef struct http_header {
  LIST_ENTRY(http_header) hh_link;
  char *hh_key;
  char *hh_value;
} http_header_t;


void http_headers_free(struct http_header_list *headers);

const char *http_header_get(struct http_header_list *headers, 
			    const char *key);

void http_header_add(struct http_header_list *headers, const char *key,
		     const char *value);

void http_header_add_alloced(struct http_header_list *headers, const char *key,
			     char *value);

void http_header_add_lws(struct http_header_list *headers, const char *data);

void http_header_add_int(struct http_header_list *headers, const char *key,
			 int value);

void http_header_merge(struct http_header_list *dst,
		       const struct http_header_list *src);

int http_ctime(time_t *tp, const char *d);

const char *http_asctime(time_t tp, char *out, size_t outlen);

#endif // HTTP_H__
