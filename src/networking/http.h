/*
 *  Showtime HTTP server
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

#include "showtime.h"
#include "htsmsg/htsbuf.h"
#include "misc/strtab.h"

#define HTTP_STATUS_OK           200
#define HTTP_STATUS_FOUND        302
#define HTTP_STATUS_BAD_REQUEST  400
#define HTTP_STATUS_UNAUTHORIZED 401
#define HTTP_STATUS_NOT_FOUND    404

typedef struct http_connection http_connection_t;

typedef int (http_callback_t)(http_connection_t *hc, 
			      const char *remain, void *opaque);

void http_server_init(void);

void *http_path_add(const char *path, void *opaque, http_callback_t *callback);

int http_send_reply(http_connection_t *hc, int rc, const char *content, 
		    const char *encoding, const char *location, int maxage,
		    htsbuf_queue_t *output);

int http_error(http_connection_t *hc, int error);

int http_redirect(http_connection_t *hc, const char *location);

const char *http_arg_get_req(http_connection_t *hc, const char *name);

extern int http_server_port;

#endif // HTTP_H__
