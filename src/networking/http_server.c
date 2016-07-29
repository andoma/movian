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
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include <libavutil/base64.h>

#define hsprintf(fmt...)  // TRACE(TRACE_DEBUG, "HTTP", fmt)

#include "http.h"
#include "http_server.h"
#include "misc/sha.h"
#include "prop/prop.h"
#include "arch/arch.h"
#include "asyncio.h"
#include "websocket.h"
#include "upnp/upnp.h"
#include "misc/bytestream.h"

static LIST_HEAD(, http_path) http_paths;
static HTS_LWMUTEX_DECL(http_paths_lwmutex);

LIST_HEAD(http_connection_list, http_connection);
int http_server_port;

/**
 *
 */
struct http_path {
  LIST_ENTRY(http_path) hp_link;
  char *hp_path;
  void *hp_opaque;
  http_callback_t *hp_callback;
  int hp_len;
  int hp_mode;
  atomic_t hp_refcount;
#define HTTP_PATH_MODE_NORMAL    0
#define HTTP_PATH_MODE_LEAF      1
#define HTTP_PATH_MODE_WEBSOCKET 2

  websocket_callback_connected_t *hp_ws_connected;
  websocket_callback_data_t *hp_ws_data;
  websocket_callback_disconnected_t *hp_ws_disconnected;
  websocket_callback_removed_t *hp_ws_removed;
};


/**
 *
 */
struct http_connection {

  asyncio_fd_t *hc_afd;

  LIST_ENTRY(http_connection) hc_link;

  int hc_state;
#define HCS_COMMAND   0
#define HCS_HEADERS   1
#define HCS_POSTDATA  2
#define HCS_WEBSOCKET 3

  struct http_header_list hc_request_headers;
  struct http_header_list hc_req_args;
  struct http_header_list hc_response_headers;

  htsbuf_queue_t hc_output;

  http_cmd_t hc_cmd;

  enum {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
    HTTP_VERSION_unknown = -1,
  } hc_version;

  char *hc_url;
  char *hc_url_orig;

  char hc_keep_alive;
  char hc_no_output;


  char *hc_post_data;
  size_t hc_post_len;
  size_t hc_post_offset;


  net_addr_t hc_local_addr;

  http_path_t *hc_path;
  void *hc_opaque;

  char hc_my_addr[128]; // hc_local_addr as text
  char hc_remote_addr[128]; // hc_remote_addr as text

  websocket_state_t hc_ws;

  asyncio_timer_t hc_ws_timeout;
  int hc_ws_missing_ping;
};

static struct http_connection_list http_connections;


/**
 *
 */
static struct strtab HTTP_cmdtab[] = {
  { "GET",         HTTP_CMD_GET },
  { "HEAD",        HTTP_CMD_HEAD },
  { "POST",        HTTP_CMD_POST },
  { "SUBSCRIBE",   HTTP_CMD_SUBSCRIBE },
  { "UNSUBSCRIBE", HTTP_CMD_UNSUBSCRIBE },
};


/**
 *
 */
static struct strtab HTTP_versiontab[] = {
  { "HTTP/1.0",        HTTP_VERSION_1_0 },
  { "HTTP/1.1",        HTTP_VERSION_1_1 },
};


static asyncio_fd_t *http_server_fd;

static int http_write(http_connection_t *hc);

static void http_ws_send_ping(void *aux);

/**
 *
 */
void *
http_get_post_data(http_connection_t *hc, size_t *sizep, int steal)
{
  void *r = hc->hc_post_data;
  if(sizep != NULL)
    *sizep = hc->hc_post_len;
  if(steal)
    hc->hc_post_data = NULL;
  return r;
}

/**
 *
 */
static http_path_t *
http_path_retain(http_path_t *hp)
{
  atomic_inc(&hp->hp_refcount);
  return hp;
}


/**
 *
 */
static int
hp_cmp(const http_path_t *a, const http_path_t *b)
{
  return b->hp_len - a->hp_len;
}

/**
 * Add a callback for a given "virtual path" on our HTTP server
 */
http_path_t *
http_path_add(const char *path, void *opaque, http_callback_t *callback,
	      int leaf)
{
  http_path_t *hp = calloc(1, sizeof(http_path_t));
  atomic_set(&hp->hp_refcount, 1);
  hp->hp_len = strlen(path);
  hp->hp_path = strdup(path);
  hp->hp_opaque = opaque;
  hp->hp_callback = callback;
  hp->hp_mode = !!leaf;
  hts_lwmutex_lock(&http_paths_lwmutex);
  LIST_INSERT_SORTED(&http_paths, hp, hp_link, hp_cmp, http_path_t);
  hts_lwmutex_unlock(&http_paths_lwmutex);
  return hp;
}


/**
 *
 */
http_path_t *
http_add_websocket(const char *path,
                   void *opaque,
		   websocket_callback_connected_t *co,
		   websocket_callback_data_t *data,
		   websocket_callback_disconnected_t *disco,
                   websocket_callback_removed_t *removed)
{
  http_path_t *hp = calloc(1, sizeof(http_path_t));
  atomic_set(&hp->hp_refcount, 1);
  hp->hp_opaque = opaque;
  hp->hp_len = strlen(path);
  hp->hp_path = strdup(path);
  hp->hp_ws_connected = co;
  hp->hp_ws_data = data;
  hp->hp_ws_disconnected = disco;
  hp->hp_ws_removed = removed;
  hp->hp_mode = HTTP_PATH_MODE_WEBSOCKET;
  hts_lwmutex_lock(&http_paths_lwmutex);
  LIST_INSERT_HEAD(&http_paths, hp, hp_link);
  hts_lwmutex_unlock(&http_paths_lwmutex);
  return hp;
}

/**
 *
 */
static void
http_path_release(http_path_t *hp)
{
  if(hp == NULL || atomic_dec(&hp->hp_refcount))
    return;

  if(hp->hp_ws_removed != NULL)
    hp->hp_ws_removed(hp->hp_opaque);

  free(hp->hp_path);
  free(hp);
}


/**
 *
 */
void
http_path_remove(struct http_path *hp)
{
  hts_lwmutex_lock(&http_paths_lwmutex);
  LIST_REMOVE(hp, hp_link);
  hts_lwmutex_unlock(&http_paths_lwmutex);
  http_path_release(hp);
}

/**
 *
 */
static http_path_t *
http_resolve(http_connection_t *hc, char **remainp, char **argsp)
{
  http_path_t *hp;
  char *url = hc->hc_url;
  if(!strncmp(url, "/showtime/", strlen("/showtime/"))) {
    url += 5;
    memcpy(url, "/api", 4);
  }

  LIST_FOREACH(hp, &http_paths, hp_link) {
    if(!strncmp(url, hp->hp_path, hp->hp_len)) {
      if(url[hp->hp_len] == 0 ||
         url[hp->hp_len] == '/' ||
	 url[hp->hp_len] == '?')
	break;
    }
  }

  if(hp == NULL)
    return NULL;

  char *v = url + hp->hp_len;

  *remainp = NULL;
  *argsp = NULL;

  switch(*v) {
  case 0:
    break;

  case '/':
    if(v[1] == '?') {
      *argsp = v + 1;
      break;
    }

    *remainp = v + 1;
    v = strchr(v + 1, '?');
    if(v != NULL) {
      *v = 0;  /* terminate remaining url */
      *argsp = v + 1;
    }
    break;

  case '?':
    *argsp = v + 1;
    break;

  default:
    return NULL;
  }

  return hp;
}


/**
 * HTTP status code to string
 */
static const char *
http_rc2str(int code)
{
  switch(code) {
  case HTTP_STATUS_OK:              return "Ok";
  case HTTP_STATUS_NOT_FOUND:       return "Not found";
  case HTTP_STATUS_UNAUTHORIZED:    return "Unauthorized";
  case HTTP_STATUS_BAD_REQUEST:     return "Bad request";
  case HTTP_STATUS_FOUND:           return "Found";
  case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method not allowed";
  case HTTP_STATUS_PRECONDITION_FAILED: return "Precondition failed";
  case HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE: return "Unsupported media type";
  case HTTP_NOT_IMPLEMENTED: return "Not implemented";
  case 500: return "Internal Server Error";
  default:
    return "Unknown returncode";
    break;
  }
}

/**
 * Transmit a HTTP reply
 */
static void
http_send_header(http_connection_t *hc, int rc, const char *content,
		 int contentlen, const char *encoding, const char *location,
		 int maxage, const char *range)
{
  htsbuf_queue_t hdrs;
  time_t t;
  http_header_t *hh;
  char date[64];

  time(&t);

  htsbuf_queue_init(&hdrs, 0);

  htsbuf_qprintf(&hdrs, "%s %d %s\r\n",
		 val2str(hc->hc_version, HTTP_versiontab),
		 rc, http_rc2str(rc));

  htsbuf_qprintf(&hdrs, "Server: "APPNAMEUSER" %s\r\n",
		 htsversion_full);

  htsbuf_qprintf(&hdrs, "Date: %s\r\n", http_asctime(t, date, sizeof(date)));

  if(maxage == 0) {
    htsbuf_qprintf(&hdrs, "Cache-Control: no-cache\r\n");
  } else {

    htsbuf_qprintf(&hdrs,  "Last-Modified: %s\r\n",
		   http_asctime(t, date, sizeof(date)));

    t += maxage;

    htsbuf_qprintf(&hdrs, "Expires: %s\r\n",
		   http_asctime(t, date, sizeof(date)));

    htsbuf_qprintf(&hdrs, "Cache-Control: max-age=%d\r\n", maxage);
  }

  htsbuf_qprintf(&hdrs, "Connection: %s\r\n",
		 hc->hc_keep_alive ? "Keep-Alive" : "Close");

  if(encoding != NULL)
    htsbuf_qprintf(&hdrs, "Content-Encoding: %s\r\n", encoding);

  if(location != NULL)
    htsbuf_qprintf(&hdrs, "Location: %s\r\n", location);

  if(content != NULL)
    htsbuf_qprintf(&hdrs, "Content-Type: %s\r\n", content);

  htsbuf_qprintf(&hdrs, "Content-Length: %d\r\n", contentlen);

  LIST_FOREACH(hh, &hc->hc_response_headers, hh_link)
    htsbuf_qprintf(&hdrs, "%s: %s\r\n", hh->hh_key, hh->hh_value);

  htsbuf_qprintf(&hdrs, "\r\n");

  htsbuf_appendq(&hc->hc_output, &hdrs);
}


/**
 * Transmit a HTTP reply
 */
int
http_send_raw(http_connection_t *hc, int rc, const char *rctxt,
	      struct http_header_list *headers, htsbuf_queue_t *output)
{
  htsbuf_queue_t hdrs;
  http_header_t *hh;
#if 0
  struct tm tm0, *tm;
  time_t t;
#endif
  htsbuf_queue_init(&hdrs, 0);

  htsbuf_qprintf(&hdrs, "%s %d %s\r\n",
		 val2str(hc->hc_version, HTTP_versiontab),
		 rc, rctxt);

  if(headers != NULL) {
    LIST_FOREACH(hh, headers, hh_link)
      htsbuf_qprintf(&hdrs, "%s: %s\r\n", hh->hh_key, hh->hh_value);
    http_headers_free(headers);
  }

  htsbuf_qprintf(&hdrs, "\r\n");

  htsbuf_appendq(&hc->hc_output, &hdrs);

  if(output != NULL) {
    if(hc->hc_no_output)
      htsbuf_queue_flush(output);
    else
      htsbuf_appendq(&hc->hc_output, output);
  }
  return 0;
}


/**
 * Transmit a HTTP reply
 */
int
http_send_reply(http_connection_t *hc, int rc, const char *content,
		const char *encoding, const char *location, int maxage,
		htsbuf_queue_t *output)
{

  http_send_header(hc, rc ?: 200, content, output ? output->hq_size : 0,
		   encoding, location, maxage, 0);

  if(output != NULL) {
    if(hc->hc_no_output)
      htsbuf_queue_flush(output);
    else
      htsbuf_appendq(&hc->hc_output, output);
  }
  http_write(hc);
  return 0;
}


/**
 * Send HTTP error back
 */
int
http_error(http_connection_t *hc, int error, const char *fmt, ...)
{
  const char *errtxt = http_rc2str(error);
  htsbuf_queue_t hq;
  va_list ap;
  char extra[200];

  htsbuf_queue_init(&hq, 0);

  if(fmt != NULL) {
    va_start(ap, fmt);
    vsnprintf(extra, sizeof(extra), fmt, ap);
    va_end(ap);
  } else {
    extra[0] = 0;
  }


  TRACE(TRACE_ERROR, "HTTPSRV", "%d %s%s%s", error, hc->hc_url_orig,
	*extra ? " -- " : "", extra),


    htsbuf_qprintf(&hq,
		   "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		   "<HTML><HEAD>\r\n"
		   "<TITLE>%d %s</TITLE>\r\n"
		   "</HEAD><BODY>\r\n"
		   "<H1>%d %s</H1>\r\n"
		   "<p>%s</p>\r\n"
		   "</BODY></HTML>\r\n",
		   error, errtxt, error, errtxt, extra);

  return http_send_reply(hc, error, "text/html", NULL, NULL, 0, &hq);
}


/**
 * Send an HTTP REDIRECT
 */
int
http_redirect(http_connection_t *hc, const char *location)
{
  htsbuf_queue_t hq;

  htsbuf_queue_init(&hq, 0);

  htsbuf_qprintf(&hq,
		 "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\r\n"
		 "<HTML><HEAD>\r\n"
		 "<TITLE>Redirect</TITLE>\r\n"
		 "</HEAD><BODY>\r\n"
		 "Please follow <a href=\"%s\">%s</a>\r\n"
		 "</BODY></HTML>\r\n",
		 location, location);

  return http_send_reply(hc, HTTP_STATUS_FOUND,
			 "text/html", NULL, location, 0, &hq);
}


/**
 *
 */
static void
http_exec(http_connection_t *hc, const http_path_t *hp, char *remain,
	  http_cmd_t method)
{
  hsprintf("%p: Dispatching [%s] on thread 0x%lx\n",
           hc, hp->hp_path, (unsigned long)pthread_self());
  int err = hp->hp_callback(hc, remain, hp->hp_opaque, method);
  hsprintf("%p: Returned from fn, err = %d\n", hc, err);

  if(err == HTTP_STATUS_OK) {
    htsbuf_queue_t out;
    htsbuf_queue_init(&out, 0);
    htsbuf_append(&out, "OK\n", 3);
    http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
    return;
  } else if(err > 0)
    http_error(hc, err, NULL);
  else if(err == 0)
    return;
  else
    abort();
}


/**
 *
 */
const char *
http_arg_get_req(http_connection_t *hc, const char *name)
{
  return http_header_get(&hc->hc_req_args, name);
}


/**
 *
 */
const char *
http_arg_get_hdr(http_connection_t *hc, const char *name)
{
  return http_header_get(&hc->hc_request_headers, name);
}


/**
 *
 */
void
http_set_response_hdr(http_connection_t *hc, const char *name,
		      const char *value)
{
  http_header_add(&hc->hc_response_headers, name, value, 0);
}


/**
 * Split a string in components delimited by 'delimiter'
 */
static int
http_tokenize(char *buf, char **vec, int vecsize, int delimiter)
{
  int n = 0;

  while(1) {
    while((*buf > 0 && *buf < 33) || *buf == delimiter)
      buf++;
    if(*buf == 0)
      break;
    vec[n++] = buf;
    if(n == vecsize)
      break;
    while(*buf > 32 && *buf != delimiter)
      buf++;
    if(*buf == 0)
      break;
    *buf = 0;
    buf++;
  }
  return n;
}



#define WSGUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/**
 *
 */
static int
http_cmd_start_websocket(http_connection_t *hc, http_path_t *hp)
{
  sha1_decl(shactx);
  char sig[64];
  uint8_t d[20];
  struct http_header_list headers;
  int err;
  const char *k = http_header_get(&hc->hc_request_headers, "Sec-WebSocket-Key");

  if(k == NULL) {
    http_error(hc, HTTP_STATUS_BAD_REQUEST, NULL);
    return 0;
  }

  hc->hc_opaque = NULL;
  assert(hc->hc_path == NULL);
  hc->hc_path = http_path_retain(hp);

  if((err = hp->hp_ws_connected(hc, hp->hp_opaque)) != 0)
    return http_error(hc, err, NULL);

  sha1_init(shactx);
  sha1_update(shactx, (const uint8_t *)k, strlen(k));
  sha1_update(shactx, (const uint8_t *)WSGUID, strlen(WSGUID));
  sha1_final(shactx, d);

  av_base64_encode(sig, sizeof(sig), d, 20);

  LIST_INIT(&headers);

  http_header_add(&headers, "Connection", "Upgrade", 0);
  http_header_add(&headers, "Upgrade", "websocket", 0);
  http_header_add(&headers, "Sec-WebSocket-Accept", sig, 0);

  http_send_raw(hc, 101, "Switching Protocols", &headers, NULL);
  hc->hc_state = HCS_WEBSOCKET;

  asyncio_timer_init(&hc->hc_ws_timeout, http_ws_send_ping, hc);
  asyncio_timer_arm_delta_sec(&hc->hc_ws_timeout, 5);

  return 0;
}


/**
 *
 */
static int
http_cmd_get(http_connection_t *hc, http_cmd_t method)
{
  http_path_t *hp;
  char *remain;
  char *args;
  int r = 0;
  hts_lwmutex_lock(&http_paths_lwmutex);

  hp = http_resolve(hc, &remain, &args);
  if(hp == NULL || (hp->hp_mode && remain != NULL)) {
    hts_lwmutex_unlock(&http_paths_lwmutex);
    http_error(hc, HTTP_STATUS_NOT_FOUND, NULL);
    return 0;
  }

  hp = http_path_retain(hp);
  hts_lwmutex_unlock(&http_paths_lwmutex);

  if(args != NULL)
    http_parse_uri_args(&hc->hc_req_args, args, 0);

  const char *c = http_header_get(&hc->hc_request_headers, "Connection");
  const char *u = http_header_get(&hc->hc_request_headers, "Upgrade");

  if(c && u && !strcasecmp(c, "Upgrade") && !strcasecmp(u, "websocket")) {
    if(hp->hp_mode != HTTP_PATH_MODE_WEBSOCKET) {
      http_error(hc, HTTP_STATUS_METHOD_NOT_ALLOWED, NULL);
    } else {
      r = http_cmd_start_websocket(hc, hp);
    }
  } else if(hp->hp_mode == HTTP_PATH_MODE_WEBSOCKET) {
    // Websocket endpoint don't wanna deal with normal HTTP requests
    http_error(hc, HTTP_STATUS_METHOD_NOT_ALLOWED, NULL);
  } else {
    http_exec(hc, hp, remain, method);
  }
  http_path_release(hp);
  return r;
}


/**
 *
 */
static int
http_read_post(http_connection_t *hc, htsbuf_queue_t *q)
{
  http_path_t *hp;
  const char *content_type;
  char *v, *argv[2], *args, *remain;
  int n;
  size_t size = q->hq_size;
  size_t rsize = hc->hc_post_len - hc->hc_post_offset;

  if(size > rsize)
    size = rsize;

  n = htsbuf_read(q, hc->hc_post_data + hc->hc_post_offset, size);
  assert(n == size);

  hc->hc_post_offset += size;
  assert(hc->hc_post_offset <= hc->hc_post_len);

  if(hc->hc_post_offset < hc->hc_post_len)
    return 0;

  hc->hc_state = HCS_COMMAND;

  /* Parse content-type */
  content_type = http_header_get(&hc->hc_request_headers, "Content-Type");

  if(content_type != NULL) {

    v = mystrdupa(content_type);

    n = http_tokenize(v, argv, 2, ';');
    if(n == 0) {
      http_error(hc, HTTP_STATUS_BAD_REQUEST, "Content-Type malformed");
      return 0;
    }

    if(!strcmp(argv[0], "application/x-www-form-urlencoded"))
      http_parse_uri_args(&hc->hc_req_args, hc->hc_post_data, 0);
  }

  hts_lwmutex_lock(&http_paths_lwmutex);
  hp = http_resolve(hc, &remain, &args);
  if(hp == NULL) {
    hts_lwmutex_unlock(&http_paths_lwmutex);
    http_error(hc, HTTP_STATUS_NOT_FOUND, NULL);
    return 0;
  }
  hp = http_path_retain(hp);
  hts_lwmutex_unlock(&http_paths_lwmutex);
  http_exec(hc, hp, remain, HTTP_CMD_POST);
  http_path_release(hp);
  return 0;
}


/**
 *
 */
static int
http_cmd_post(http_connection_t *hc, htsbuf_queue_t *q)
{
  const char *v;

  v = http_header_get(&hc->hc_request_headers, "Content-Length");
  if(v == NULL) {
    /* No content length in POST, make us disconnect */
    return 1;
  }

  hc->hc_post_len = atoi(v);
  if(hc->hc_post_len > 16 * 1024 * 1024) {
    /* Bail out if POST data > 16 Mb */
    hc->hc_keep_alive = 0;
    return 1;
  }

  /* Allocate space for data, we add a terminating null char to ease
     string processing on the content */

  hc->hc_post_data = malloc(hc->hc_post_len + 1);
  if(hc->hc_post_data == NULL) {
    hc->hc_keep_alive = 0;
    return 1;
  }

  hc->hc_post_data[hc->hc_post_len] = 0;
  hc->hc_post_offset = 0;

  v = http_header_get(&hc->hc_request_headers, "Expect");
  if(v != NULL) {
    if(!strcasecmp(v, "100-continue")) {
      htsbuf_qprintf(&hc->hc_output, "HTTP/1.1 100 Continue\r\n\r\n");
    }
  }


  hc->hc_state = HCS_POSTDATA;
  return http_read_post(hc, q);
}

/**
 *
 */
static int
http_handle_request(http_connection_t *hc, htsbuf_queue_t *q)
{
  hc->hc_state = HCS_COMMAND;
  /* Set keep-alive status */
  const char *v = http_header_get(&hc->hc_request_headers, "connection");

  switch(hc->hc_version) {
  case HTTP_VERSION_1_0:
    /* Keep-alive is default off, but can be enabled */
    hc->hc_keep_alive = v != NULL && !strcasecmp(v, "keep-alive");
    break;

  case HTTP_VERSION_1_1:
    /* Keep-alive is default on, but can be disabled */
    hc->hc_keep_alive = !(v != NULL && !strcasecmp(v, "close"));
    break;

  default:
    http_error(hc, HTTP_NOT_IMPLEMENTED, NULL);
    return 0;
  }

  hc->hc_no_output = hc->hc_cmd == HTTP_CMD_HEAD;

  switch(hc->hc_cmd) {
  default:
    http_error(hc, HTTP_NOT_IMPLEMENTED, NULL);
    return 0;

  case HTTP_CMD_POST:
    return http_cmd_post(hc, q);

  case HTTP_CMD_HEAD:
    hc->hc_no_output = 1;
    // FALLTHRU
  case HTTP_CMD_GET:
  case HTTP_CMD_SUBSCRIBE:
  case HTTP_CMD_UNSUBSCRIBE:
    return http_cmd_get(hc, hc->hc_cmd);
  }
  return 1;
}


/**
 *
 */
void
http_set_opaque(http_connection_t *hc, void *opaque)
{
  hc->hc_opaque = opaque;
}


/**
 *
 */
void
websocket_send(http_connection_t *hc, int opcode, const void *data,
	       size_t len)
{
  websocket_append_hdr(&hc->hc_output, opcode, len, NULL);
  htsbuf_append(&hc->hc_output, data, len);
  http_write(hc);
}


/**
 *
 */
void
websocket_sendq(http_connection_t *hc, int opcode, htsbuf_queue_t *hq)
{
  websocket_append_hdr(&hc->hc_output, opcode, hq->hq_size, NULL);
  htsbuf_appendq(&hc->hc_output, hq);
  http_write(hc);
}


/**
 *
 */
static int
websocket_input(void *opaque, int opcode, uint8_t *data, int len)
{
  http_connection_t *hc = opaque;

  switch(opcode) {
  case 8:
    return 1;
  case 9:
    websocket_send(hc, 10, data, len);
    return 0;
  case 10:
    hc->hc_ws_missing_ping = 0;
    return 0;

  default:
    hc->hc_path->hp_ws_data(hc, opcode, data, len, hc->hc_opaque);
    return 0;
  }
}


/**
 *
 */
static int
http_handle_input(http_connection_t *hc, htsbuf_queue_t *q)
{
  char *buf;
  char *argv[3], *c;
  int n;

  while(1) {

    switch(hc->hc_state) {
    case HCS_COMMAND:
      free(hc->hc_post_data);
      hc->hc_post_data = NULL;

      buf = http_read_line(q);
      if(buf == NULL)
	return 0;

      if(buf == (void *)-1)
	return -1;

      hsprintf("%p: %s\n", hc, buf);

      if((n = http_tokenize(buf, argv, 3, -1)) != 3) {
        free(buf);
	return -1;
      }

      hc->hc_cmd = str2val(argv[0], HTTP_cmdtab);

      mystrset(&hc->hc_url, argv[1]);
      mystrset(&hc->hc_url_orig, argv[1]);

      if((hc->hc_version = str2val(argv[2], HTTP_versiontab)) == -1) {
        free(buf);
	return -1;
      }

      hc->hc_state = HCS_HEADERS;
      /* FALLTHRU */

      http_headers_free(&hc->hc_req_args);
      http_headers_free(&hc->hc_request_headers);
      http_headers_free(&hc->hc_response_headers);
      free(buf);

    case HCS_HEADERS:

      buf = http_read_line(q);
      if(buf == NULL)
	return 0;

      if(buf == (void *)-1)
	return -1;

      hsprintf("%p: %s\n", hc, buf);

      if(buf[0] == 32 || buf[0] == '\t') {
	// LWS

	http_header_add_lws(&hc->hc_request_headers, buf+1);

      } else if(buf[0] == 0) {

	if(http_handle_request(hc, q)) {
          free(buf);
	  return 1;
        }

	if(TAILQ_FIRST(&hc->hc_output.hq_q) == NULL && !hc->hc_keep_alive) {
          free(buf);
	  return 1;
        }

      } else {

	if((c = strchr(buf, ':')) == NULL) {
          free(buf);
	  return 1;
        }

	*c++ = 0;
	while(*c == 32)
	  c++;
	http_header_add(&hc->hc_request_headers, buf, c, 0);
      }
      free(buf);
      break;

    case HCS_POSTDATA:
      if(!http_read_post(hc, q))
	return 0;
      break;

    case HCS_WEBSOCKET:
      return websocket_parse(q, websocket_input, hc, &hc->hc_ws);
    }
  }
}


/**
 *
 */
static int
http_write(http_connection_t *hc)
{
  asyncio_sendq(hc->hc_afd, &hc->hc_output, 0);
  return 0;
}


/**
 *
 */
static void
http_close(http_connection_t *hc)
{
  hsprintf("%p: ----------------- CLOSED CONNECTION\n", hc);
  htsbuf_queue_flush(&hc->hc_output);
  http_headers_free(&hc->hc_req_args);
  http_headers_free(&hc->hc_request_headers);
  http_headers_free(&hc->hc_response_headers);
  asyncio_del_fd(hc->hc_afd);
  free(hc->hc_url);
  free(hc->hc_url_orig);
  free(hc->hc_post_data);
  free(hc->hc_ws.packet);

  if(hc->hc_path != NULL && hc->hc_path->hp_ws_disconnected != NULL)
    hc->hc_path->hp_ws_disconnected(hc, hc->hc_opaque);

  asyncio_timer_disarm(&hc->hc_ws_timeout);

  LIST_REMOVE(hc, hc_link);
  http_path_release(hc->hc_path);
  free(hc);
}


/**
 *
 */
static void
http_ws_send_ping(void *aux)
{
  http_connection_t *hc = aux;
  hc->hc_ws_missing_ping++;
  if(hc->hc_ws_missing_ping == 2) {
    TRACE(TRACE_DEBUG, "HTTP", "Websocket connection from %s timed out",
          hc->hc_remote_addr);
    http_close(hc);
    return;
  }
  char pingmsg[4] = {0};
  websocket_send(hc, 9, pingmsg, sizeof(pingmsg));
  asyncio_timer_arm_delta_sec(&hc->hc_ws_timeout, 5);
}


/**
 *
 */
static void
http_io_error(void *opaque, const char *error)
{
  http_close(opaque);
}


/**
 *
 */
static void
http_io_read(void *opaque, htsbuf_queue_t *q)
{
  http_connection_t *hc = opaque;
  if(http_handle_input(hc, q)) {
    http_close(hc);
    return;
  }
  http_write(hc);
}


/**
 *
 */
const char *
http_get_my_host(http_connection_t *hc)
{
  if(!hc->hc_my_addr[0])
    net_fmt_host(hc->hc_my_addr, sizeof(hc->hc_my_addr), &hc->hc_local_addr);
  return hc->hc_my_addr;
}


/**
 *
 */
int
http_get_my_port(http_connection_t *hc)
{
  return hc->hc_local_addr.na_port;
}


/**
 *
 */
static void
http_accept(void *opaque, int fd, const net_addr_t *local_addr,
            const net_addr_t *remote_addr)
{
  http_connection_t *hc = calloc(1, sizeof(http_connection_t));

  LIST_INSERT_HEAD(&http_connections, hc, hc_link);

  hc->hc_afd = asyncio_attach("HTTP connection", fd,
                              http_io_error, http_io_read, hc, opaque);
  htsbuf_queue_init(&hc->hc_output, 0);

  hc->hc_local_addr  = *local_addr;
  net_fmt_host(hc->hc_remote_addr, sizeof(hc->hc_remote_addr), remote_addr);
}


/**
 *
 */
static void
http_server_init(void)
{
  http_server_fd = asyncio_listen("http-server", 42000,
                                  http_accept, NULL, 1);

  if(gconf.http_server_ssl_key != NULL && gconf.http_server_ssl_crt != NULL) {
    void *ctx = asyncio_ssl_create_server(gconf.http_server_ssl_key,
                                          gconf.http_server_ssl_crt);
    if(ctx != NULL)
      asyncio_listen("http-server", 42443, http_accept, ctx, 1);
  }

#if STOS
  asyncio_listen("http-server", 80, http_accept, NULL, 1);
#endif

  if(http_server_fd != NULL) {
    http_server_port = asyncio_get_port(http_server_fd);

#if ENABLE_UPNP
    if(!gconf.disable_upnp)
      upnp_init(http_server_port);
#endif

  }
}

/**
 *
 */
static void
http_server_fini(void)
{
  http_connection_t *hc;
  TRACE(TRACE_DEBUG, "HTTPSERVER", "Shutdown");

  rstr_t *msg;

  int wserrcode = 1001;
  switch(gconf.exit_code) {
  case APP_EXIT_STANDBY:
    msg = _("Standby");
    break;
  case APP_EXIT_POWEROFF:
    msg = _("Power Off");
    break;
  case APP_EXIT_RESTART:
    msg = _("Restarting");
    wserrcode = 4000;
    break;
  case APP_EXIT_REBOOT:
    msg = _("Rebooting");
    wserrcode = 4000;
    break;
  default:
    msg = NULL;
  }

  int msglen = msg ? strlen(rstr_get(msg)) : 0;
  uint8_t *closemsg = alloca(msglen + 2);
  wr16_be(closemsg, wserrcode);

  if(msg != NULL)
    memcpy(closemsg + 2, rstr_get(msg), msglen + 2);

  LIST_FOREACH(hc, &http_connections, hc_link) {
    if(hc->hc_state == HCS_WEBSOCKET) {
      websocket_send(hc, 8, closemsg, msglen + 2);
    }
  }
  rstr_release(msg);
}


/**
 *
 */
void
http_req_args_fill_htsmsg(http_connection_t *hc, htsmsg_t *msg)
{
  http_header_t *hh;

  LIST_FOREACH(hh, &hc->hc_req_args, hh_link)
    htsmsg_add_str(msg, hh->hh_key, hh->hh_value);
}


INITME(INIT_GROUP_ASYNCIO, http_server_init, http_server_fini, 0);
