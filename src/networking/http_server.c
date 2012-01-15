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

#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#include <netinet/in.h>

#define hsprintf(fmt...) // printf(fmt)

#if ENABLE_POSIX_NETWORKING
#include <netinet/tcp.h>  // for TCP_ defines
#endif

#include "http.h"
#include "http_server.h"

int http_server_port;
static LIST_HEAD(, http_path) http_paths;
LIST_HEAD(http_connection_list, http_connection); 

/**
 *
 */
struct http_connection {
  
  LIST_ENTRY(http_connection) hc_link;
  int hc_fd;
  int hc_events;

  int hc_state;
#define HCS_COMMAND 0
#define HCS_HEADERS 1
#define HCS_POSTDATA 2

  struct http_header_list hc_request_headers;
  struct http_header_list hc_req_args;
  struct http_header_list hc_response_headers;

  htsbuf_queue_t hc_input;
  htsbuf_queue_t hc_output;

  http_cmd_t hc_cmd;

  enum {
    HTTP_VERSION_1_0,
    HTTP_VERSION_1_1,
  } hc_version;

  char *hc_url;
  char *hc_url_orig;
  
  char hc_keep_alive;
  char hc_no_output;


  char *hc_post_data;
  size_t hc_post_len;
  size_t hc_post_offset;

  char hc_myaddr[32];
};


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


/**
 *
 */
typedef struct http_path {
  LIST_ENTRY(http_path) hp_link;
  const char *hp_path;
  void *hp_opaque;
  http_callback_t *hp_callback;
  int hp_len;
  int hp_leaf;
} http_path_t;


/**
 *
 */
typedef struct http_server {
  int hs_numcon;
  int hs_fd;

  int hs_fds_size;
  struct pollfd *hs_fds;

  struct http_connection_list hs_connections;

} http_server_t;


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
 * Add a callback for a given "virtual path" on our HTTP server
 */
void *
http_path_add(const char *path, void *opaque, http_callback_t *callback,
	      int leaf)
{
  http_path_t *hp = malloc(sizeof(http_path_t));

  hp->hp_len = strlen(path);
  hp->hp_path = strdup(path);
  hp->hp_opaque = opaque;
  hp->hp_callback = callback;
  hp->hp_leaf = leaf;
  LIST_INSERT_HEAD(&http_paths, hp, hp_link);
  return hp;
}


/**
 *
 */
static http_path_t *
http_resolve(http_connection_t *hc, char **remainp, char **argsp)
{
  http_path_t *hp;
  char *v;

  LIST_FOREACH(hp, &http_paths, hp_link) {
    if(!strncmp(hc->hc_url, hp->hp_path, hp->hp_len)) {
      if(hc->hc_url[hp->hp_len] == 0 || hc->hc_url[hp->hp_len] == '/' ||
	 hc->hc_url[hp->hp_len] == '?')
	break;
    }
  }

  if(hp == NULL)
    return NULL;

  v = hc->hc_url + hp->hp_len;

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

  htsbuf_qprintf(&hdrs, "Server: HTS/Showtime %s\r\n",
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
http_exec(http_connection_t *hc, http_path_t *hp, char *remain,
	  http_cmd_t method)
{
  hsprintf("%p: Dispatching [%s] on thread 0x%lx\n",
	 hc, hp->hp_path, pthread_self());
  int err = hp->hp_callback(hc, remain, hp->hp_opaque, method);
  hsprintf("%p: Returned from fn, err = %d\n", hc, err);

  if(err == HTTP_STATUS_OK) {
    htsbuf_queue_t out;
    htsbuf_queue_init(&out, 0);
    htsbuf_append(&out, "OK\n", 3);
    http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
    return;
  } else if(err > 0)
    http_error(hc, err, NULL);
  else if(err == 0)
    return;
  else
    abort();
}


/**
 * De-escape HTTP URL
 */
static void
http_deescape(char *s)
{
  char v, *d = s;

  while(*s) {
    if(*s == '+') {
      *d++ = ' ';
      s++;
    } else if(*s == '%') {
      s++;
      switch(*s) {
      case '0' ... '9':
	v = (*s - '0') << 4;
	break;
      case 'a' ... 'f':
	v = (*s - 'a' + 10) << 4;
	break;
      case 'A' ... 'F':
	v = (*s - 'A' + 10) << 4;
	break;
      default:
	*d = 0;
	return;
      }
      s++;
      switch(*s) {
      case '0' ... '9':
	v |= (*s - '0');
	break;
      case 'a' ... 'f':
	v |= (*s - 'a' + 10);
	break;
      case 'A' ... 'F':
	v |= (*s - 'A' + 10);
	break;
      default:
	*d = 0;
	return;
      }
      s++;

      *d++ = v;
    } else {
      *d++ = *s++;
    }
  }
  *d = 0;
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
  http_header_add(&hc->hc_response_headers, name, value);
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


/**
 * Parse arguments of a HTTP GET url, not perfect, but works for us
 */
static void
http_parse_get_args(http_connection_t *hc, char *args)
{
  char *k, *v;

  while(args) {
    k = args;
    if((args = strchr(args, '=')) == NULL)
      break;
    *args++ = 0;
    v = args;
    args = strchr(args, '&');

    if(args != NULL)
      *args++ = 0;

    http_deescape(k);
    http_deescape(v);
    http_header_add(&hc->hc_req_args, k, v);
  }
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

  hp = http_resolve(hc, &remain, &args);
  if(hp == NULL || (hp->hp_leaf && remain != NULL)) {
    http_error(hc, HTTP_STATUS_NOT_FOUND, NULL);
    return 0;
  }

  if(args != NULL)
    http_parse_get_args(hc, args);

  http_exec(hc, hp, remain, method);
  return 0;
}


/**
 *
 */
static int
http_read_post(http_connection_t *hc)
{
  http_path_t *hp;
  const char *content_type;
  char *v, *argv[2], *args, *remain;
  int n;
  size_t size = hc->hc_input.hq_size;
  size_t rsize = hc->hc_post_len - hc->hc_post_offset;

  if(size > rsize)
    size = rsize;

  n = htsbuf_read(&hc->hc_input, hc->hc_post_data + hc->hc_post_offset, size);
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
      http_parse_get_args(hc, hc->hc_post_data);
  }

  hp = http_resolve(hc, &remain, &args);
  if(hp == NULL) {
    http_error(hc, HTTP_STATUS_NOT_FOUND, NULL);
    return 0;
  }
  http_exec(hc, hp, remain, HTTP_CMD_POST);
  return 0;
}


/**
 *
 */
static int
http_cmd_post(http_connection_t *hc)
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

  hc->hc_post_data = calloc(1, hc->hc_post_len + 1);
  hc->hc_post_data[hc->hc_post_len] = 0;
  hc->hc_post_offset = 0;

  v = http_header_get(&hc->hc_request_headers, "Expect");
  if(v != NULL) {
    if(!strcasecmp(v, "100-continue")) {
      htsbuf_qprintf(&hc->hc_output, "HTTP/1.1 100 Continue\r\n\r\n");
    }
  }


  hc->hc_state = HCS_POSTDATA;
  return http_read_post(hc);
}

/**
 *
 */
static int
http_handle_request(http_connection_t *hc)
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
  }

  hc->hc_no_output = hc->hc_cmd == HTTP_CMD_HEAD;

  switch(hc->hc_cmd) {
  default:
    http_error(hc, HTTP_NOT_IMPLEMENTED, NULL);
    return 0;
    
  case HTTP_CMD_POST:
    return http_cmd_post(hc);

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
static int
http_read_line(http_connection_t *hc, char *buf, size_t bufsize)
{
  int len;

  len = htsbuf_find(&hc->hc_input, 0xa);
  if(len == -1)
    return 0;

  if(len >= bufsize - 1)
    return -1;

  htsbuf_read(&hc->hc_input, buf, len);
  buf[len] = 0;
  while(len > 0 && buf[len - 1] < 32)
    buf[--len] = 0;
  htsbuf_drop(&hc->hc_input, 1); /* Drop the \n */
  return 1;
}


/**
 *
 */
static int
http_handle_input(http_connection_t *hc)
{
  char buf[1024];
  char *argv[3], *c;
  int r, n;

  while(1) {

    switch(hc->hc_state) {
    case HCS_COMMAND:
      free(hc->hc_post_data);
      hc->hc_post_data = NULL;

      if((r = http_read_line(hc, buf, sizeof(buf))) == -1)
	return 1;

      if(r == 0)
	return 0;

      hsprintf("%p: %s\n", hc, buf);

      if((n = http_tokenize(buf, argv, 3, -1)) != 3)
	return 1;

      hc->hc_cmd = str2val(argv[0], HTTP_cmdtab);

      mystrset(&hc->hc_url, argv[1]);
      mystrset(&hc->hc_url_orig, argv[1]);

      if((hc->hc_version = str2val(argv[2], HTTP_versiontab)) == -1)
	return 1;

      hc->hc_state = HCS_HEADERS;
      /* FALLTHRU */

      http_headers_free(&hc->hc_req_args);
      http_headers_free(&hc->hc_request_headers);
      http_headers_free(&hc->hc_response_headers);

    case HCS_HEADERS:
      if((r = http_read_line(hc, buf, sizeof(buf))) == -1)
	return 1;

      if(r == 0)
	return 0;

      hsprintf("%p: %s\n", hc, buf);

      if(buf[0] == 32 || buf[0] == '\t') {
	// LWS

	http_header_add_lws(&hc->hc_request_headers, buf+1);

      } else if(buf[0] == 0) {
	
	if(http_handle_request(hc))
	  return 1;

	if(TAILQ_FIRST(&hc->hc_output.hq_q) == NULL && !hc->hc_keep_alive)
	  return 1;

      } else {

	if((c = strchr(buf, ':')) == NULL)
	  return 1;
	*c++ = 0;
	while(*c == 32)
	  c++;
	http_header_add(&hc->hc_request_headers, buf, c);
      }
      break;

    case HCS_POSTDATA:
      if(!http_read_post(hc))
	return 0;
    }
  }
}


/**
 *
 */
static int
http_write(http_connection_t *hc)
{
  htsbuf_data_t *hd;
  int l, r = 0;

  htsbuf_queue_t *q = &hc->hc_output;
  
  while((hd = TAILQ_FIRST(&q->hq_q)) != NULL) {

    l = hd->hd_data_len - hd->hd_data_off;
    r = write(hc->hc_fd, hd->hd_data + hd->hd_data_off, l);

    if(r == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
      r = 0;

    if(r == -1)
      return -1;

    q->hq_size -= r;

    if(r != l) {
      // Failed to write it all
      hd->hd_data_off += r;
      hc->hc_events |= POLLOUT;
      return 0;
    }

    TAILQ_REMOVE(&q->hq_q, hd, hd_link);
    free(hd->hd_data);
    free(hd);
  }
  hc->hc_events &= ~POLLOUT;
  return !hc->hc_keep_alive;
}


/**
 *
 */
static int
http_io(http_connection_t *hc, int revents)
{
  int r;
  if(revents & (POLLHUP | POLLERR))
    return 1;

  if(revents & POLLIN) {
    char *mem = malloc(1000);
    
    r = read(hc->hc_fd, mem, 1000);
    if(r > 0) {
      htsbuf_append_prealloc(&hc->hc_input, mem, r);
      if(http_handle_input(hc))
	return 1;
    } else {
      free(mem);
      return 1;
    }
  }
  r = http_write(hc);
  return r;
}


/**
 *
 */
static void
http_close(http_server_t *hs, http_connection_t *hc)
{
  hsprintf("%p: ----------------- CLOSED CONNECTION\n", hc);
  htsbuf_queue_flush(&hc->hc_input);
  htsbuf_queue_flush(&hc->hc_output);
  http_headers_free(&hc->hc_req_args);
  http_headers_free(&hc->hc_request_headers);
  http_headers_free(&hc->hc_response_headers);
  LIST_REMOVE(hc, hc_link);
  hs->hs_numcon--;
  close(hc->hc_fd);
  free(hc->hc_url);
  free(hc->hc_url_orig);
  free(hc->hc_post_data);
  free(hc);
}


/**
 *
 */
const char *
http_get_my_host(http_connection_t *hc)
{
  return hc->hc_myaddr;
}

int
http_get_my_port(http_connection_t *hc)
{
  return http_server_port;
}


/**
 *
 */
static void
http_accept(http_server_t *hs)
{
  struct sockaddr_in si;
  socklen_t sl = sizeof(struct sockaddr_in);
  int fd, val;
  http_connection_t *hc;
  socklen_t slen;
  struct sockaddr_in self;

  fd = accept(hs->hs_fd, (struct sockaddr *)&si, &sl);

  if(fd == -1) {
    TRACE(TRACE_ERROR, "HTTPSRV", "Accept error: %s", strerror(errno));
    sleep(1);
    return;
  }

  hc = calloc(1, sizeof(http_connection_t));
  hc->hc_fd = fd;
  hc->hc_events = POLLIN | POLLHUP | POLLERR;
  LIST_INSERT_HEAD(&hs->hs_connections, hc, hc_link);
  hs->hs_numcon++;
  htsbuf_queue_init(&hc->hc_input, 0);
  htsbuf_queue_init(&hc->hc_output, 0);

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  val = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
  
#ifdef TCP_KEEPIDLE
  val = 30;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val));
#endif

#ifdef TCP_KEEPINVL
  val = 15;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val));
#endif

#ifdef TCP_KEEPCNT
  val = 5;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val));
#endif

#ifdef TCP_NODELAY
  val = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#endif

  slen = sizeof(struct sockaddr_in);
  if(!getsockname(fd, (struct sockaddr *)&self, &slen)) {
    uint32_t ip = ntohl(self.sin_addr.s_addr);
    snprintf(hc->hc_myaddr, sizeof(hc->hc_myaddr),
	     "%d.%d.%d.%d",
	     (ip >> 24) & 0xff,
	     (ip >> 16) & 0xff,
	     (ip >> 8)  & 0xff,
	     (ip)       & 0xff);
  } else {
    hc->hc_myaddr[0] = 0;
  }
  hsprintf("%p: ----------------- NEW CONNECTION\n", hc);
}


/**
 *
 */
static void *
http_server(void *aux)
{
  http_server_t *hs = aux;
  int n, r;
  http_connection_t *hc, *nxt;

  while(1) {
    n = hs->hs_numcon + 1;

    if(hs->hs_fds_size < n) {
      hs->hs_fds_size = n + 3;
      hs->hs_fds = realloc(hs->hs_fds, sizeof(struct pollfd) * hs->hs_fds_size);
    }

    n = 0;
    LIST_FOREACH(hc, &hs->hs_connections, hc_link) {
      hs->hs_fds[n].fd = hc->hc_fd;
      hs->hs_fds[n].events = hc->hc_events;
      n++;
    }

    hs->hs_fds[n].fd = hs->hs_fd;
    hs->hs_fds[n].events = POLLIN;
    n++;
    r = poll(hs->hs_fds, n, -1);

    if(r == -1)
      continue;

    n = 0;
    for(hc = LIST_FIRST(&hs->hs_connections); hc != NULL; hc = nxt) {
      nxt = LIST_NEXT(hc, hc_link);

      if(http_io(hc, hs->hs_fds[n].revents)) {
	http_close(hs, hc);
      }
      n++;
    }
    if(hs->hs_fds[n].revents & POLLIN)
      http_accept(hs);
  }
  return NULL;
}


/**
 *
 */
void
http_server_init(void)
{
  int fd;
  struct sockaddr_in si = {0};
  socklen_t sl = sizeof(struct sockaddr_in);
  int one = 1, i;
  http_server_t *hs;

  if((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    return;

  si.sin_family = AF_INET;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

  for(i = 0; i < 100; i++) {
    si.sin_port = htons(42000 + i);
    if(!bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in)))
      break;
  }

  if(i == 100) {
    si.sin_port = 0;
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
      TRACE(TRACE_ERROR, "HTTPSRV", "Unable to bind");
      close(fd);
      return;
    }
    if(getsockname(fd, (struct sockaddr *)&si, &sl) == -1) {
      TRACE(TRACE_ERROR, "HTTPSRV", "Unable to figure local port");
      close(fd);
      return;
    }
    http_server_port = ntohs(si.sin_port);
  } else {
    http_server_port = 42000 + i;
  }

  TRACE(TRACE_INFO, "HTTPSRV", "Listening on port %d", http_server_port);

  listen(fd, 1);
    
  hs = calloc(1, sizeof(http_server_t));
  hs->hs_fd = fd;  
  hts_thread_create_detached("httpsrv", http_server, hs,
			     THREAD_PRIO_NORMAL);
}
