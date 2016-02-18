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

#include "main.h"
#include "htsmsg/htsbuf.h"
#include "htsmsg/htsmsg.h"
#include "misc/strtab.h"
#include "http.h"
#include "asyncio.h"

typedef struct http_connection http_connection_t;

typedef int (http_callback_t)(http_connection_t *hc, 
			      const char *remain, void *opaque,
			      http_cmd_t method);

void *http_path_add(const char *path, void *opaque, http_callback_t *callback,
		    int leaf);


typedef int (websocket_callback_init_t)(http_connection_t *hc);

typedef int (websocket_callback_data_t)(http_connection_t *hc,
					int opcode,
					uint8_t *data,
					size_t len,
					void *opaque);

typedef void (websocket_callback_fini_t)(http_connection_t *hc,
					 void *opaque);

void *http_add_websocket(const char *path, 
			 websocket_callback_init_t *init,
			 websocket_callback_data_t *data,
			 websocket_callback_fini_t *fini);

void websocket_send(http_connection_t *hc, int opcode, const void *data,
		    size_t len);

void websocket_sendq(http_connection_t *hc, int opcode, htsbuf_queue_t *hq);

void http_set_opaque(http_connection_t *hc, void *opaque);

int http_send_reply(http_connection_t *hc, int rc, const char *content, 
		    const char *encoding, const char *location, int maxage,
		    htsbuf_queue_t *output);

int http_send_raw(http_connection_t *hc, int rc, const char *rctxt,
		  struct http_header_list *headers, htsbuf_queue_t *output);

int http_error(http_connection_t *hc, int error, const char *extra, ...);

int http_redirect(http_connection_t *hc, const char *location);

const char *http_arg_get_req(http_connection_t *hc, const char *name);

const char *http_arg_get_hdr(http_connection_t *hc, const char *name);

const char *http_get_my_host(http_connection_t *hc);

int http_get_my_port(http_connection_t *hc);

void *http_get_post_data(http_connection_t *hc, size_t *sizep, int steal);

void http_set_response_hdr(http_connection_t *hc, const char *name,
			   const char *value);

void http_req_args_fill_htsmsg(http_connection_t* hc, htsmsg_t* msg);
