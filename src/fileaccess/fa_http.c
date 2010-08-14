/*
 *  HTTP file access
 *  Copyright (C) 2008 Andreas Öman
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
#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <libavutil/base64.h>
#include <libavutil/avstring.h>
#include <libavutil/common.h>

#include "keyring.h"
#include "fileaccess.h"
#include "networking/net.h"
#include "fa_proto.h"
#include "showtime.h"
#include "htsmsg/htsmsg_xml.h"
#include "misc/string.h"

#if 0
#define HTTP_TRACE(x...) TRACE(TRACE_DEBUG, "HTTP", x)
#else
#define HTTP_TRACE(x...)
#endif

/**
 * If we read more than this in a sequence, we switch to a continous
 * HTTP stream (instead of ranges)
 */
#define STREAMING_LIMIT 1000000


TAILQ_HEAD(http_connection_queue , http_connection);

static struct http_connection_queue http_connections;
static int http_parked_connections;
static hts_mutex_t http_connections_mutex;

typedef struct http_connection {
  char hc_hostname[HOSTNAME_MAX];
  int hc_port;
  tcpcon_t *hc_tc;

  htsbuf_queue_t hc_spill;

  TAILQ_ENTRY(http_connection) hc_link;

  int hc_reused;

} http_connection_t;


/**
 *
 */
static http_connection_t *
http_connection_get(const char *hostname, int port, char *errbuf, int errlen)
{
  http_connection_t *hc;
  tcpcon_t *tc;

  hts_mutex_lock(&http_connections_mutex);

  TAILQ_FOREACH(hc, &http_connections, hc_link) {
    if(!strcmp(hc->hc_hostname, hostname) && hc->hc_port == port) {
      TAILQ_REMOVE(&http_connections, hc, hc_link);
      http_parked_connections--;
      hts_mutex_unlock(&http_connections_mutex);
      HTTP_TRACE("Reusing connection to %s:%d", hc->hc_hostname, hc->hc_port);
      hc->hc_reused = 1;
      return hc;
    }
  }

  hts_mutex_unlock(&http_connections_mutex);

  
  if((tc = tcp_connect(hostname, port, errbuf, errlen, 5000)) < 0) {
    HTTP_TRACE("Connection to %s:%d failed", hostname, port);
    return NULL;
  }
  HTTP_TRACE("Connected to %s:%d", hostname, port);

  hc = malloc(sizeof(http_connection_t));
  snprintf(hc->hc_hostname, sizeof(hc->hc_hostname), "%s", hostname);
  hc->hc_port = port;
  hc->hc_tc = tc;
  htsbuf_queue_init(&hc->hc_spill, 0);
  hc->hc_reused = 0;
  return hc;
}


/**
 *
 */
static void
http_connection_destroy(http_connection_t *hc)
{
  HTTP_TRACE("Disconnected from %s:%d", hc->hc_hostname, hc->hc_port);
  tcp_close(hc->hc_tc);
  htsbuf_queue_flush(&hc->hc_spill);
  free(hc);
}


/**
 *
 */
static void
http_connection_park(http_connection_t *hc)
{
  HTTP_TRACE("Parking connection to %s:%d", hc->hc_hostname, hc->hc_port);
  hts_mutex_lock(&http_connections_mutex);
  TAILQ_INSERT_TAIL(&http_connections, hc, hc_link);

  if(http_parked_connections == 5) {
    hc = TAILQ_FIRST(&http_connections);
    TAILQ_REMOVE(&http_connections, hc, hc_link);
    http_connection_destroy(hc);
  } else {
    http_parked_connections++;
  }

  hts_mutex_unlock(&http_connections_mutex);
}



/**
 *
 */
LIST_HEAD(http_redirect_list, http_redirect);
static struct http_redirect_list http_redirects;
static hts_mutex_t http_redirects_mutex;

/**
 *
 */
typedef struct http_redirect {

  LIST_ENTRY(http_redirect) hr_link;

  char *hr_from;
  char *hr_to;

} http_redirect_t;


static void
add_premanent_redirect(const char *from, const char *to)
{
  http_redirect_t *hr;

  hts_mutex_lock(&http_redirects_mutex);

  LIST_FOREACH(hr, &http_redirects, hr_link) {
    if(!strcmp(from, hr->hr_from))
      break;
  }

  if(hr == NULL) {
    hr = malloc(sizeof(http_redirect_t));
    hr->hr_from = strdup(from);
    LIST_INSERT_HEAD(&http_redirects, hr, hr_link);
  } else {
    free(hr->hr_to);
  }
  hr->hr_to = strdup(to);
  hts_mutex_unlock(&http_redirects_mutex);
}


/**
 *
 */
static void
http_init(void)
{
  TAILQ_INIT(&http_connections);
  hts_mutex_init(&http_connections_mutex);

  LIST_INIT(&http_redirects);
  hts_mutex_init(&http_redirects_mutex);
}


/**
 *
 */
typedef struct http_file {
  fa_handle_t h;

  http_connection_t *hf_connection;

  char *hf_url;
  char *hf_auth;
  char *hf_location;
  char *hf_auth_realm;


  char hf_line[1024];

  char hf_authurl[128];
  char hf_path[URL_MAX];

  int hf_chunked_transfer;

  int64_t hf_rsize; /* Size of reply, if chunked: don't care about this */

  int64_t hf_filesize;
  int64_t hf_pos;

  int64_t hf_consecutive_read;

  int hf_isdir;

  int hf_auth_failed;
  
  char *hf_content_type;

  enum {
    CONNECTION_MODE_PERSISTENT,
    CONNECTION_MODE_CLOSE,
  } hf_connection_mode;

  time_t hf_mtime;

} http_file_t;

#define hf_fd(hf) ((hf)->hf_hc->hc_fd)



/**
 *
 */
static void *
http_read_content(http_file_t *hf)
{
  int s, csize;
  char *buf;
  char chunkheader[100];
  http_connection_t *hc = hf->hf_connection;

  if(hf->hf_chunked_transfer) {
    buf = NULL;
    s = 0;

    while(1) {
      if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader),
		       &hc->hc_spill) < 0)
	break;
 
      if((csize = strtol(chunkheader, NULL, 16)) == 0)
	return buf;

      buf = realloc(buf, s + csize + 1);
      if(tcp_read_data(hc->hc_tc, buf + s, csize, &hc->hc_spill))
	break;

      s += csize;
      buf[s] = 0;

      if(tcp_read_data(hc->hc_tc, chunkheader, 2, &hc->hc_spill))
	break;
    }
    free(buf);
    hf->hf_chunked_transfer = 0;
    return NULL;
  }

  s = hf->hf_rsize;
  buf = malloc(s + 1);
  buf[s] = 0;
  
  if(tcp_read_data(hc->hc_tc, buf, s, &hc->hc_spill)) {
    free(buf);
    return NULL;
  }
  hf->hf_rsize = 0;
  return buf;
}


/**
 *
 */
static int
http_drain_content(http_file_t *hf)
{
  char *buf;

  if(hf->hf_chunked_transfer == 0 && hf->hf_rsize < 0)
    return 0;

  if((buf = http_read_content(hf)) == NULL)
    return -1;
  free(buf);
  return 0;
}

/*
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
 *
 */
static int
http_read_response(http_file_t *hf, struct http_header_list *headers)
{
  int li;
  char *c, *q, *argv[2];
  int code = -1;
  int64_t i64;
  http_connection_t *hc = hf->hf_connection;

  http_headers_free(headers);
  
  hf->hf_connection_mode = CONNECTION_MODE_PERSISTENT;
  hf->hf_rsize = -1;
  hf->hf_chunked_transfer = 0;
  free(hf->hf_content_type);
  hf->hf_content_type = NULL;

  HTTP_TRACE("%s: Reponse:", hf->hf_url);

  for(li = 0; ;li++) {
    if(tcp_read_line(hc->hc_tc, hf->hf_line, sizeof(hf->hf_line),
		     &hc->hc_spill) < 0)
      return -1;

    HTTP_TRACE("  %s", hf->hf_line);

    if(hf->hf_line[0] == 0)
      break;

    if(li == 0) {
      q = hf->hf_line;
      while(*q && *q != ' ')
	q++;
      while(*q == ' ')
	q++;
      code = atoi(q);
      continue;
    }
    
    if(http_tokenize(hf->hf_line, argv, 2, -1) != 2)
      continue;

    if((c = strrchr(argv[0], ':')) == NULL)
      continue;
    *c = 0;

    if(headers != NULL)
      http_headers_add(headers, argv[0], argv[1]);

    if(!strcasecmp(argv[0], "Transfer-Encoding")) {

      if(!strcasecmp(argv[1], "chunked"))
	hf->hf_chunked_transfer = 1;

      continue;
    }

    if(!strcasecmp(argv[0], "WWW-Authenticate")) {

      if(http_tokenize(argv[1], argv, 2, -1) != 2)
	continue;
      
      if(strcasecmp(argv[0], "Basic"))
	continue;

      if(strncasecmp(argv[1], "realm=\"", strlen("realm=\"")))
	continue;
      q = c = argv[1] + strlen("realm=\"");
      
      if((q = strrchr(c, '"')) == NULL)
	continue;
      *q = 0;
      
      free(hf->hf_auth_realm);
      hf->hf_auth_realm = strdup(c);
      continue;
    }


    if(!strcasecmp(argv[0], "Location")) {
      free(hf->hf_location);
      hf->hf_location = strdup(argv[1]);
      continue;
    }

    if(!strcasecmp(argv[0], "Content-Length")) {
      i64 = strtoll(argv[1], NULL, 0);
      hf->hf_rsize = i64;

      if(code == 200)
	hf->hf_filesize = i64;
    }
    
    if(!strcasecmp(argv[0], "Content-Type")) {
      free(hf->hf_content_type);
      hf->hf_content_type = strdup(argv[1]);
    }


    if(!strcasecmp(argv[0], "connection")) {

      if(!strcasecmp(argv[1], "close"))
	hf->hf_connection_mode = CONNECTION_MODE_CLOSE;
    }
  }
  return code;
}




/**
 *
 */
static void
http_detach(http_file_t *hf, int reusable)
{
  if(hf->hf_connection == NULL)
    return;

  if(reusable) {
    http_connection_park(hf->hf_connection);
  } else {
    http_connection_destroy(hf->hf_connection);
  }
  hf->hf_connection = NULL;
}


/**
 *
 */
static int
redirect(http_file_t *hf, int *redircount, char *errbuf, size_t errlen,
	 int code)
{
  (*redircount)++;
  if(*redircount == 10) {
    snprintf(errbuf, errlen, "Too many redirects");
    return -1;
  }

  if(hf->hf_location == NULL) {
    snprintf(errbuf, errlen, "Redirect respons without location");
    return -1;
  }

  if(http_drain_content(hf)) {
    snprintf(errbuf, errlen, "Connection lost");
    return -1;
  }

  if(code == 301)
    add_premanent_redirect(hf->hf_url, hf->hf_location);

  HTTP_TRACE("%s: Following redirect to %s%s", hf->hf_url, hf->hf_location,
	     code == 301 ? ", (premanent)" : "");

  free(hf->hf_url);
  hf->hf_url = hf->hf_location;
  http_deescape(hf->hf_url);

  hf->hf_location = NULL;
  
  // Location changed, must detach from connection
  // We might still be able to reuse it if hostname+port is same
  // But that's for some other code to figure out
  http_detach(hf, 1);
  return 0;
}

/**
 *
 */
static int 
authenticate(http_file_t *hf, char *errbuf, size_t errlen, int *non_interactive)
{
  char *username;
  char *password;
  char buf1[128];
  char buf2[128];
  int r;

  if(hf->hf_auth_failed > 0 && non_interactive) {
    *non_interactive = FAP_STAT_NEED_AUTH;
    return -1;
  }

  if(http_drain_content(hf)) {
    snprintf(errbuf, errlen, "Connection lost");
    return -1;
  }
  if(hf->hf_auth_realm == NULL) {
    snprintf(errbuf, errlen, "Authentication without realm");
    return -1;
  }
  snprintf(buf1, sizeof(buf1), "%s @ %s", hf->hf_auth_realm, 
	   hf->hf_connection->hc_hostname);

  r = keyring_lookup(buf1, &username, &password, NULL, 
		     hf->hf_auth_failed > 0,
		     "HTTP Client", "Access denied");

  hf->hf_auth_failed++;

  free(hf->hf_auth);
  hf->hf_auth = NULL;

  if(r == -1) {
    /* Rejected */
    snprintf(errbuf, errlen, "Authentication rejected by user");
    return -1;
  }

  if(r == 0) {
    HTTP_TRACE("%s: Authenticating with %s %s",
	       hf->hf_url, username, password);

    /* Got auth credentials */  
    snprintf(buf1, sizeof(buf1), "%s:%s", username, password);
    av_base64_encode(buf2, sizeof(buf2), (uint8_t *)buf1, strlen(buf1));

    snprintf(buf1, sizeof(buf1), "Authorization: Basic %s", buf2);
    hf->hf_auth = strdup(buf1);

    free(username);
    free(password);

    return 0;
  }

  /* No auth info */
  return 0;
}


/**
 *
 */
static int
http_connect(http_file_t *hf, char *errbuf, int errlen, int escape_path)
{
  char hostname[HOSTNAME_MAX];
  int port;
  http_redirect_t *hr;
  const char *url;

  hf->hf_filesize = -1;
  hf->hf_rsize = 0;

  if(hf->hf_connection != NULL)
    http_detach(hf, 0);

  url = hf->hf_url;

  hts_mutex_lock(&http_redirects_mutex);

  LIST_FOREACH(hr, &http_redirects, hr_link)
    if(!strcmp(url, hr->hr_from)) {
      escape_path = 0;
      url = hr->hr_to;
      break;
    }
  
  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hostname, sizeof(hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), 
	    url, escape_path);

  hts_mutex_unlock(&http_redirects_mutex);

  if(port < 0)
    port = 80;

  /* empty path, default to "/" */ 
  if(!hf->hf_path[0])
    strcpy(hf->hf_path, "/");

  hf->hf_connection = http_connection_get(hostname, port, errbuf, errlen);

  return hf->hf_connection ? 0 : -1;
}


/**
 *
 */
static int
http_open0(http_file_t *hf, int probe, char *errbuf, int errlen,
	   int ignore_size, int *non_interactive)
{
  int code;
  htsbuf_queue_t q;
  int redircount = 0;

  reconnect:

  if(http_connect(hf, errbuf, errlen, 1))
    return -1;

  if(!probe && hf->hf_filesize != -1)
    return 0;

  htsbuf_queue_init(&q, 0);

 again:
  htsbuf_qprintf(&q, 
		 "HEAD %s HTTP/1.1\r\n"
		 "Accept: */*\r\n"
		 "User-Agent: Showtime %s\r\n"
		 "Host: %s\r\n"
		 "%s%s"
		 "\r\n",
		 hf->hf_path,
		 htsversion,
		 hf->hf_connection->hc_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

  tcp_write_queue(hf->hf_connection->hc_tc, &q);

  code = http_read_response(hf, NULL);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto reconnect;
  }

  HTTP_TRACE("%s: HEAD %d", hf->hf_url, code);

  switch(code) {
  case 200:
    if(!ignore_size && hf->hf_filesize < 0) {
      snprintf(errbuf, errlen, "Invalid HTTP 200 response");
      return -1;
    }
    hf->hf_rsize = 0; /* This was just a HEAD request, we don't actually
		       * get any data
		       */
 
    hf->hf_auth_failed = 0;
    return 0;
    
  case 301:
  case 302:
  case 303:
  case 307:
    hf->hf_auth_failed = 0;
    if(redirect(hf, &redircount, errbuf, errlen, code))
      return -1;
    goto reconnect;


  case 401:
    if(authenticate(hf, errbuf, errlen, non_interactive))
      return -1;
    goto again;

  default:
    snprintf(errbuf, errlen, "Unhandled HTTP response %d", code);
    return -1;
  }
}


/**
 *
 */
static void
http_destroy(http_file_t *hf)
{
  http_detach(hf, 
	      hf->hf_rsize == 0 &&
	      hf->hf_connection_mode == CONNECTION_MODE_PERSISTENT &&
	      hf->hf_chunked_transfer == 0);
  free(hf->hf_url);
  free(hf->hf_auth);
  free(hf->hf_auth_realm);
  free(hf->hf_location);
  free(hf->hf_content_type);
  free(hf);
}

/* inspired by http://xbmc.org/trac/browser/trunk/XBMC/xbmc/FileSystem/HTTPDirectory.cpp */

static int
http_strip_last(char *s, char c)
{
  int len = strlen(s);
  
  if(s[len - 1 ] == c) {
    s[len - 1] = '\0';
    return 1;
  }
  
  return 0;
}

static int
http_index_parse(http_file_t *hf, fa_dir_t *fd, char *buf)
{
  char *p, *n;
  char *url = malloc(URL_MAX);
  
  p = buf;
  /* n + 1 to skip '\0' */
  for(;(n = strchr(p, '\n')); p = n + 1) {
    char *s, *href, *hrefd, *name;
    int isdir;
    
    /* terminate line */
    *n = '\0';
    
    if(!(href = strstr(p, "<a href=\"")))
      continue;
    href += 9;
    /* when does this happen? xbmc does it */
    if(href[0] == '/')
      href++;
    
    if(!(s = strstr(href, "\">")))
      continue;
    *s++ = '\0'; /* skip " and terminate */
    s++; /* skip > */
    
    name = s;
    if(!(s = strstr(name, "</a>")))
      continue;
    *s = '\0';
    
    isdir = http_strip_last(name, '/');
    http_strip_last(href, '/');
    
    hrefd = strdup(href);
    http_deescape(hrefd);
    
    html_entities_decode(hrefd);
    html_entities_decode(name);
    
    /* skip parent dir links etc */
    if(strcmp(name, hrefd) == 0) {
      snprintf(url, URL_MAX, "http://%s:%d%s%s%s",
               hf->hf_connection->hc_hostname, 
	       hf->hf_connection->hc_port, hf->hf_path,
               hrefd,
               isdir ? "/" : "");
      
      http_deescape(url);
      fa_dir_add(fd, url, name, isdir ? CONTENT_DIR : CONTENT_FILE);
    }
    
    free(hrefd);
  }
  
  free(url);

  return 0;
}


/**
 *
 */
static int
http_index_fetch(http_file_t *hf, fa_dir_t *fd, char *errbuf, size_t errlen)
{
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  int redircount = 0;

reconnect:
  if(http_connect(hf, errbuf, errlen, 1))
    return -1;

  htsbuf_queue_init(&q, 0);
  
again:
  htsbuf_qprintf(&q, 
		 "GET %s HTTP/1.1\r\n"
		 "Accept: */*\r\n"
		 "User-Agent: Showtime %s\r\n"
		 "Host: %s\r\n"
		 "%s%s"
		 "\r\n",
		 hf->hf_path,
		 htsversion,
		 hf->hf_connection->hc_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");
  
  tcp_write_queue(hf->hf_connection->hc_tc, &q);
  code = http_read_response(hf, NULL);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto reconnect;
  }
  
  switch(code) {
      
    case 200: /* 200 OK */
      hf->hf_auth_failed = 0;
      
      if((buf = http_read_content(hf)) == NULL) {
        snprintf(errbuf, errlen, "Connection lost");
        return -1;
      }
      
      retval = http_index_parse(hf, fd, buf);
      free(buf);
      return retval;
      
    case 301:
    case 302:
    case 303:
    case 307:
      hf->hf_auth_failed = 0;
      if(redirect(hf, &redircount, errbuf, errlen, code))
        return -1;
      goto reconnect;
      
    case 401:
      if(authenticate(hf, errbuf, errlen, NULL))
        return -1;
      goto again;
      
    default:
      snprintf(errbuf, errlen, "Unhandled HTTP response %d", code);
      return -1;
  }
}

/**
 *
 */
static int
http_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  
  hf->hf_url = strdup(url);
  
  retval = http_index_fetch(hf, fd, errbuf, errlen);
  http_destroy(hf);
  return retval;
}

/**
 * Open file
 */
static fa_handle_t *
http_open_ex(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
             int ignore_size, int *non_interactive)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  
  hf->hf_url = strdup(url);

  if(!http_open0(hf, 1, errbuf, errlen, ignore_size, non_interactive)) {
    hf->h.fh_proto = fap;
    return &hf->h;
  }

  http_destroy(hf);
  return NULL;
}

static fa_handle_t *
http_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  return http_open_ex(fap, url, errbuf, errlen, 0, 0);
}


/**
 * Close file
 */
static void
http_close(fa_handle_t *handle)
{
  http_file_t *hf = (http_file_t *)handle;
  http_destroy(hf);
}


/**
 * Read from file
 */
static int
http_read(fa_handle_t *handle, void *buf, size_t size)
{
  http_file_t *hf = (http_file_t *)handle;
  htsbuf_queue_t q;
  int i, code;

  if(size == 0)
    return 0;

  /* Max 5 retries */
  for(i = 0; i < 5; i++) {
    /* If not connected, try to (re-)connect */
    if(hf->hf_connection == NULL && http_open0(hf, 0, NULL, 0, 0, 0))
      return -1;

    http_connection_t *hc = hf->hf_connection;

    if(hf->hf_rsize > 0) {
      /* We have pending data input on the socket */

      if(hf->hf_rsize < size)
	/* We can not read more data than is available */
	size = hf->hf_rsize;

    } else {

      /* Must send a new request */

      htsbuf_queue_init(&q, 0);

      htsbuf_qprintf(&q, 
		     "GET %s HTTP/1.1\r\n"
		     "Accept: */*\r\n"
		     "User-Agent: Showtime %s\r\n"
		     "Host: %s\r\n"
		     "%s%s",
		     hf->hf_path,
		     htsversion,
		     hc->hc_hostname,
		     hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

      if(hf->hf_consecutive_read > STREAMING_LIMIT) {
	TRACE(TRACE_DEBUG, "HTTP", "%s: switching to streaming mode",
	      hf->hf_url);

	htsbuf_qprintf(&q, "Range: bytes=%"PRId64"-\r\n\r\n", hf->hf_pos);
      } else {
	htsbuf_qprintf(&q, 
		       "Range: bytes=%"PRId64"-%"PRId64"\r\n\r\n", 
		       hf->hf_pos, hf->hf_pos + size - 1);
      }

      tcp_write_queue(hc->hc_tc, &q);
      code = http_read_response(hf, NULL);
      switch(code) {
      case 206:
	// Range transfer OK
	break;

      case 200:
	if(hf->hf_pos != 0) {
	  TRACE(TRACE_DEBUG, "HTTP", 
		"Server responds with 200 for request starting at %"PRId64,
		hf->hf_pos);
	  http_detach(hf, 0);
	  return -1;
	}
	break;

      default:
	TRACE(TRACE_DEBUG, "HTTP", 
	      "Read error. HTTP code %d", code);
	http_detach(hf, 0);
	continue;
      }

      if(hf->hf_chunked_transfer) {
	return -1; /* Not supported atm */
      }
      if(hf->hf_rsize < size)
	size = hf->hf_rsize;

      if(size == 0)
	return size;
    }

    if(!tcp_read_data(hc->hc_tc, buf, size, &hc->hc_spill)) {
      hf->hf_pos   += size;
      hf->hf_rsize -= size;

      hf->hf_consecutive_read += size;
      return size;
    } else {
      http_detach(hf, 0);
    }
  }
  http_detach(hf, 0);
  return -1;
}


/**
 * Seek in file
 */
static int64_t
http_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  http_file_t *hf = (http_file_t *)handle;
  http_connection_t *hc = hf->hf_connection;
  off_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = hf->hf_pos + pos;
    break;

  case SEEK_END:
    np = hf->hf_filesize + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  if(hf->hf_pos != np) {
    hf->hf_consecutive_read = 0;

    if(hf->hf_rsize != 0) {
      // We've data pending on socket
      int64_t d = np - hf->hf_pos;

      // We allow seek by reading if delta offset is <8k

      if(hc != NULL && d > 0 && d < 8192 && d < hf->hf_rsize) {
	void *j = malloc(d);
	int n = tcp_read_data(hc->hc_tc, j, d, &hc->hc_spill);
	free(j);
	if(!n) {
	  hf->hf_pos = np;
	  return np;
	}
      }
    }
    http_detach(hf, 0);
    hf->hf_pos = np;
  }

  return np;
}


/**
 * Return size of file
 */
static int64_t
http_fsize(fa_handle_t *handle)
{
  http_file_t *hf = (http_file_t *)handle;
  return hf->hf_filesize;
}


/**
 * Standard unix stat
 */
static int
http_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	  char *errbuf, size_t errlen, int non_interactive)
{
  fa_handle_t *handle;
  http_file_t *hf;
  int statcode = -1;

  if((handle = http_open_ex(fap, url, errbuf, errlen, 1, 
			    non_interactive ? &statcode : NULL)) == NULL)
    return statcode;
 
  memset(buf, 0, sizeof(struct stat));
  hf = (http_file_t *)handle;
  
  /* no content length and text/html, assume "index of" page */
  if(hf->hf_filesize < 0 &&
     hf->hf_content_type && strstr(hf->hf_content_type, "text/html"))
    buf->st_mode = S_IFDIR;
  else
    buf->st_mode = S_IFREG;
  buf->st_size = hf->hf_filesize;
  
  http_destroy(hf);
  return 0;
}


/**
 *
 */
static void *
http_quickload(struct fa_protocol *fap, const char *url,
	       size_t *sizeptr, char *errbuf, size_t errlen)
{
  char *res;

  if(http_request(url, NULL, &res, sizeptr, errbuf, errlen, NULL, 0,
		  HTTP_REQUEST_ESCAPE_PATH, NULL))
    return NULL;
  return res;
}



/**
 *
 */
static fa_protocol_t fa_protocol_http = {
  .fap_init  = http_init,
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL,
  .fap_name  = "http",
  .fap_scan  = http_scandir,
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = http_stat,
  .fap_quickload = http_quickload,
};

FAP_REGISTER(http);



/**
 * XXX: Move to libhts?
 */
static const char *
get_cdata_by_tag(htsmsg_t *tags, const char *name)
{
  htsmsg_t *sub;
  if((sub = htsmsg_get_map(tags, name)) == NULL)
    return NULL;
  return htsmsg_get_str(sub, "cdata");
}


/**
 *
 */
static int 
dav_ctime(time_t *tp, const char *d)
{
  struct tm tm = {0};
  char wday[4];
  char month[4];
  int i;
  static const char *months[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

  if(sscanf(d, "%3s, %d %s %d %d:%d:%d",
	    wday, &tm.tm_mday, month, &tm.tm_year,
	    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 7)
    return -1;

  tm.tm_year -= 1900;;
  tm.tm_isdst = -1;
	      
  for(i = 0; i < 12; i++)
    if(!strcasecmp(months[i], month)) {
      tm.tm_mon = i;
      break;
    }
  
#ifdef WII
  *tp = mktime(&tm);
#else
  *tp = timegm(&tm);
#endif
  return 0;
}


/**
 * Parse WEBDAV PROPFIND results
 */
static int
parse_propfind(http_file_t *hf, htsmsg_t *xml, fa_dir_t *fd,
	       char *errbuf, size_t errlen)
{
  htsmsg_t *m, *c, *c2;
  htsmsg_field_t *f;
  const char *href, *d, *q;
  int isdir, i, r;
  char *rpath = malloc(URL_MAX);
  char *path  = malloc(URL_MAX);
  char *fname = malloc(URL_MAX);
  char *ehref = malloc(URL_MAX); // Escaped href
  fa_dir_entry_t *fde;

  // We need to compare paths and to do so, we must deescape the
  // possible URL encoding. Do the searched-for path once
  snprintf(rpath, URL_MAX, "%s", hf->hf_path);
  http_deescape(rpath);

  if((m = htsmsg_get_map_multi(xml, "tags", 
			       "DAV:multistatus", "tags", NULL)) == NULL) {
    snprintf(errbuf, errlen, "WEBDAV: DAV:multistatus not found in XML");
    goto err;
  }

  HTSMSG_FOREACH(f, m) {
    if(strcmp(f->hmf_name, "DAV:response"))
      continue;
    if((c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    if((c = htsmsg_get_map(c, "tags")) == NULL)
      continue;
    
    if((c2 = htsmsg_get_map(c, "DAV:href")) == NULL)
      continue;

    /* Some DAV servers seams to send an empty href tag for root path "/" */
    if((href = htsmsg_get_str(c2, "cdata")) == NULL)
      href = "/";
    else {
      snprintf(ehref, URL_MAX, "%s", href);
      http_deescape(ehref);
    }

    if((c = htsmsg_get_map_multi(c, "DAV:propstat", "tags",
				 "DAV:prop", "tags", NULL)) == NULL)
      continue;

    isdir = !!htsmsg_get_map_multi(c, "DAV:resourcetype", "tags",
				   "DAV:collection", NULL);

    if(fd != NULL) {

      if(strcmp(rpath, ehref)) {
	http_connection_t *hc = hf->hf_connection;

	if(hc->hc_port != 80) {
	  snprintf(path, URL_MAX, "webdav://%s:%d%s", 
		   hc->hc_hostname, hc->hc_port, href);
	} else {
	  snprintf(path, URL_MAX, "webdav://%s%s", 
		   hc->hc_hostname, href);
	}

	if((q = strrchr(path, '/')) != NULL) {
	  q++;

	  if(*q == 0) {
	    /* We have a trailing slash, can't piggy back filename
	       on path (we want to keep the trailing '/' in the URL
	       since some webdav servers require it and will force us
	       to 301/redirect if we don't come back with it */
	    q--;
	    while(q != path && q[-1] != '/')
	      q--;

	    for(i = 0; i < URL_MAX - 1 && q[i] != '/'; i++)
	      fname[i] = q[i];
	    fname[i] = 0;

	  } else {
	    snprintf(fname, URL_MAX, "%s", q);
	  }
	  http_deescape(fname);
	  http_deescape(path);
	  
	  fde = fa_dir_add(fd, path, fname, 
			   isdir ? CONTENT_DIR : CONTENT_FILE);

	  if(fde != NULL && !isdir) {

	    fde->fde_statdone = 1;

	    if((d = get_cdata_by_tag(c, "DAV:getcontentlength")) != NULL)
	      fde->fde_stat.st_size = strtoll(d, NULL, 10);
	    else
	      fde->fde_statdone = 0;
	  
	    if((d = get_cdata_by_tag(c, "DAV:getlastmodified")) == NULL ||
	       dav_ctime(&fde->fde_stat.st_mtime, d))
	      fde->fde_statdone = 1;

	  }
	}
      }
    } else {
      /* single entry stat(2) */

      snprintf(fname, URL_MAX, "%s", href);
      http_deescape(fname);

      if(!strcmp(rpath, fname)) {
	/* This is the path we asked for */

	hf->hf_isdir = isdir;

	if(!isdir) {
	  if((d = get_cdata_by_tag(c, "DAV:getcontentlength")) != NULL)
	    hf->hf_filesize = strtoll(d, NULL, 10);

	  hf->hf_mtime = 0;
	  if((d = get_cdata_by_tag(c, "DAV:getlastmodified")) != NULL)
	    dav_ctime(&hf->hf_mtime, d);
	}
	goto ok;
      } 
    }
  }

  if(fd == NULL) {
    /* We should have returned earlier, server did not include the file 
       we asked for in its reply. The server is probably broken. 
       (It should respond with a 404 or something) */
    snprintf(errbuf, errlen, "WEBDAV: File not found in XML reply");
  err:
    r = -1;
  } else {
  ok:
    r = 0;
  }
  free(rpath);
  free(path);
  free(fname);
  free(ehref);
  return r;
}


/**
 * Execute a webdav PROPFIND
 */
static int
dav_propfind(http_file_t *hf, fa_dir_t *fd, char *errbuf, size_t errlen,
	     int *non_interactive)
{
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  htsmsg_t *xml;
  int redircount = 0;
  char err0[128];
  int i;

  for(i = 0; i < 5; i++) {
    if(http_connect(hf, errbuf, errlen, 1))
      return -1;


  again:
    htsbuf_queue_init(&q, 0);
    htsbuf_qprintf(&q, 
		   "PROPFIND %s HTTP/1.1\r\n"
		   "Depth: %d\r\n"
		   "Accept: */*\r\n"
		   "User-Agent: Showtime %s\r\n"
		   "Host: %s\r\n"
		   "%s%s"
		   "\r\n",
		   hf->hf_path,
		   fd != NULL ? 1 : 0,
		   htsversion,
		   hf->hf_connection->hc_hostname,
		   hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

    tcp_write_queue(hf->hf_connection->hc_tc, &q);
    code = http_read_response(hf, NULL);

    HTTP_TRACE("%s: PROPFIND %d", hf->hf_url, code);

    if(code == -1) {
      http_detach(hf, 0);
      continue;
    }

    switch(code) {
      
    case 207: /* 207 Multi-part */
      hf->hf_auth_failed = 0;
      
      if((buf = http_read_content(hf)) == NULL) {
	snprintf(errbuf, errlen, "Connection lost");
	return -1;
      }

      /* XML parser consumes 'buf' */
      if((xml = htsmsg_xml_deserialize(buf, err0, sizeof(err0))) == NULL) {
	snprintf(errbuf, errlen,
		 "WEBDAV/PROPFIND: XML parsing failed:\n%s", err0);
	return -1;
      }
      retval = parse_propfind(hf, xml, fd, errbuf, errlen);
      htsmsg_destroy(xml);
      return retval;

    case 301:
    case 302:
    case 303:
    case 307:
      hf->hf_auth_failed = 0;
      if(redirect(hf, &redircount, errbuf, errlen, code))
	return -1;
      continue;

    case 401:
      if(authenticate(hf, errbuf, errlen, non_interactive))
	return -1;
      goto again;

    case 405:
    case 501:
      snprintf(errbuf, errlen, "Not a WEBDAV share");
      return -1;

    default:
      http_drain_content(hf);
      snprintf(errbuf, errlen, "Unhandled HTTP response %d", code);
      return -1;
    }
  }
  snprintf(errbuf, errlen, "All attempts failed");
  return -1;
}



/**
 * Standard unix stat
 */
static int
dav_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	 char *errbuf, size_t errlen, int non_interactive)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  int statcode = -1;

  hf->hf_url = strdup(url);
  
  if(dav_propfind(hf, NULL, errbuf, errlen, 
		  non_interactive ? &statcode : NULL)) {
    http_destroy(hf);
    return statcode;
  }

  memset(buf, 0, sizeof(struct stat));

  buf->st_mode = hf->hf_isdir ? S_IFDIR : S_IFREG;
  buf->st_size = hf->hf_filesize;
  buf->st_mtime = hf->hf_mtime;

  http_destroy(hf);
  return 0;
}


/**
 *
 */
static int
dav_scandir(fa_dir_t *fd, const char *url, char *errbuf, size_t errlen)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));

  hf->hf_url = strdup(url);
  
  retval = dav_propfind(hf, fd, errbuf, errlen, NULL);
  http_destroy(hf);
  return retval;
}



/**
 *
 */
static fa_protocol_t fa_protocol_webdav = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL,
  .fap_name  = "webdav",
  .fap_scan  = dav_scandir,
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = dav_stat,
  .fap_quickload = http_quickload,
};
FAP_REGISTER(webdav);


/**
 *
 */
int
http_request(const char *url, const char **arguments, 
	     char **result, size_t *result_sizep,
	     char *errbuf, size_t errlen,
	     htsbuf_queue_t *postdata, const char *postcontenttype,
	     int flags, struct http_header_list *headers_out)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  htsbuf_queue_t q;
  int code, r, port;
  char buf[URL_MAX], hostname[HOSTNAME_MAX];
  http_connection_t *hc;
  int redircount = 0, escape_path;
  http_redirect_t *hr;
  const char *url0;

  if(headers_out != NULL)
    LIST_INIT(headers_out);

  hf->hf_url = strdup(url);

 retry:

  url0 = hf->hf_url;

  escape_path = !!(flags & HTTP_REQUEST_ESCAPE_PATH);

  hts_mutex_lock(&http_redirects_mutex);

  LIST_FOREACH(hr, &http_redirects, hr_link)
    if(!strcmp(url, hr->hr_from)) {
      escape_path = 0;
      url0 = hr->hr_to;
      break;
    }

  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hostname, sizeof(hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path),
	    url0, escape_path);

  hts_mutex_unlock(&http_redirects_mutex);

  if(port < 0)
    port = 80;

  hc = hf->hf_connection = http_connection_get(hostname, port, errbuf, errlen);
  if(hf->hf_connection == NULL) {
    http_destroy(hf);
    http_headers_free(headers_out);
    return -1;
  }

  htsbuf_queue_init(&q, 0);

  htsbuf_qprintf(&q, "%s %s", postdata ? "POST" : "GET", hf->hf_path);

  if(arguments != NULL) {
    char prefix = '?';

    while(arguments[0] != NULL) {
      path_escape(buf, sizeof(buf), arguments[0]);
      htsbuf_qprintf(&q, "%c%s", prefix, buf);
      path_escape(buf, sizeof(buf), arguments[1]);
      htsbuf_qprintf(&q, "=%s", buf);
      arguments += 2;
      prefix = '&';
    }
  }

  htsbuf_qprintf(&q, 
		 " HTTP/1.1\r\n"
		 "Accept: */*\r\n"
		 "User-Agent: Showtime %s\r\n"
		 "Host: %s\r\n",
		 htsversion,
		 hostname);


  if(postdata != NULL) 
    htsbuf_qprintf(&q, "Content-Length: %d\r\n", postdata->hq_size);

  if(postcontenttype != NULL) 
    htsbuf_qprintf(&q, "Content-Type: %s\r\n", postcontenttype);

  if(hf->hf_auth != NULL)
    htsbuf_qprintf(&q, "%s\r\n", hf->hf_auth);


  htsbuf_qprintf(&q, "\r\n");

  tcp_write_queue(hf->hf_connection->hc_tc, &q);

  if(postdata != NULL)
    tcp_write_queue_dontfree(hf->hf_connection->hc_tc, postdata);

  code = http_read_response(hf, headers_out);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto retry;
  }

  switch(code) {
  case 200:
    break;

  case 301:
  case 302:
  case 303:
  case 307:
    if(redirect(hf, &redircount, errbuf, errlen, code)) {
      http_destroy(hf);
      http_headers_free(headers_out);
      return -1;
    }
    goto retry;

  case 401:
    if(authenticate(hf, errbuf, errlen, NULL)) {
      http_destroy(hf);
      http_headers_free(headers_out);
      return -1;
    }
    goto retry;

  default:
    snprintf(errbuf, errlen, "HTTP error: %d", code);
    http_destroy(hf);
    http_headers_free(headers_out);
    return -1;
  }
  

  if(hf->hf_connection_mode == CONNECTION_MODE_CLOSE) {
    int capacity = 16384;
    int size = 0;
    char *mem = malloc(capacity + 1);

    while(1) {

      if(size == capacity) {
	capacity *= 2;
	mem = realloc(mem, capacity + 1);
      }

      r = tcp_read_data_nowait(hc->hc_tc, mem + size,
			       capacity - size, &hc->hc_spill);
      if(r < 0)
	break;

      size += r;
    }

    mem[size] = 0;

    *result = mem;
    *result_sizep = size;
    
  } else {

    char *buf = NULL;
    size_t size = 0;

    if(hf->hf_chunked_transfer) {
      char chunkheader[100];

      while(1) {
	int csize;
	if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader),
			 &hc->hc_spill) < 0)
	  break;
 
	if((csize = strtol(chunkheader, NULL, 16)) == 0)
	  goto done;

	buf = realloc(buf, size + csize + 1);
	if(tcp_read_data(hc->hc_tc, buf + size, csize, &hc->hc_spill))
	  break;

	size += csize;
	
	if(tcp_read_data(hc->hc_tc, chunkheader, 2, &hc->hc_spill))
	  break;
      }
      free(buf);
      snprintf(errbuf, errlen, "Chunked transfer error");
      http_destroy(hf);
      http_headers_free(headers_out);
      return -1;

    } else {

      size = hf->hf_filesize;
      buf = malloc(hf->hf_filesize + 1);

      r = tcp_read_data(hc->hc_tc, buf, hf->hf_filesize, &hc->hc_spill);
      
      if(r == -1) {
	snprintf(errbuf, errlen, "HTTP read error");
	free(buf);
	http_destroy(hf);
	http_headers_free(headers_out);
	return -1;    
      }
    }
  done:
    buf[size] = 0;
    *result = buf;
    *result_sizep = size;
  }

  http_destroy(hf);
  return 0;
}


/**
 *
 */
void
http_headers_free(struct http_header_list *headers)
{
  http_header_t *hh;

  if(headers == NULL)
    return;

  while((hh = LIST_FIRST(headers)) != NULL) {
    LIST_REMOVE(hh, hh_link);
    free(hh->hh_key);
    free(hh->hh_value);
    free(hh);
  }
}


/**
 *
 */
void
http_headers_add(struct http_header_list *headers, const char *key,
		 const char *value)
{
  http_header_t *hh = malloc(sizeof(http_header_t));

  hh->hh_key   = strdup(key);
  hh->hh_value = strdup(value);
  LIST_INSERT_HEAD(headers, hh, hh_link);
}


/**
 *
 */
const char *
http_headers_find(struct http_header_list *headers, const char *key)
{
  http_header_t *hh;

  LIST_FOREACH(hh, headers, hh_link)
    if(!strcasecmp(hh->hh_key, key))
      return hh->hh_value;
  return NULL;
}
