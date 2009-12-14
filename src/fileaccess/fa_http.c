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


#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define HTTP_MAX_PATH_LEN 2048

extern char *htsversion;

TAILQ_HEAD(http_connection_queue , http_connection);

static struct http_connection_queue http_connections;
static int http_parked_connections;
static hts_mutex_t http_connections_mutex;

typedef struct http_connection {
  char hc_hostname[128];
  int hc_port;

  int hc_fd;

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
  int fd;

  hts_mutex_lock(&http_connections_mutex);

  TAILQ_FOREACH(hc, &http_connections, hc_link) {
    if(!strcmp(hc->hc_hostname, hostname) && hc->hc_port == port) {
      TAILQ_REMOVE(&http_connections, hc, hc_link);
      http_parked_connections--;
      hts_mutex_unlock(&http_connections_mutex);
      hc->hc_reused = 1;
      return hc;
    }
  }

  hts_mutex_unlock(&http_connections_mutex);

  
  if((fd = tcp_connect(hostname, port, errbuf, errlen, 3000)) < 0)
    return NULL;

  hc = malloc(sizeof(http_connection_t));
  snprintf(hc->hc_hostname, sizeof(hc->hc_hostname), "%s", hostname);
  hc->hc_port = port;
  hc->hc_fd = fd;
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
  tcp_close(hc->hc_fd);
  htsbuf_queue_flush(&hc->hc_spill);
  free(hc);
}


/**
 *
 */
static void
http_connection_park(http_connection_t *hc)
{
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
static void
http_init(void)
{
  TAILQ_INIT(&http_connections);
  hts_mutex_init(&http_connections_mutex);
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
  char hf_path[256];

  int hf_chunked_transfer;

  int64_t hf_rsize; /* Size of reply, if chunked: don't care about this */

  int64_t hf_size; /* Full size of file, if known */

  int64_t hf_pos;

  int hf_isdir;

  int hf_auth_failed;
  
  char *hf_content_type;

  enum {
    CONNECTION_MODE_PERSISTENT,
    CONNECTION_MODE_CLOSE,
  } hf_connection_mode;

} http_file_t;

#define hf_fd(hf) ((hf)->hf_hc->hc_fd)

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

static const char hexchars[16] = "0123456789abcdef";

/**
 *
 */
static void
path_escape(char *dest, int size, const char *src)
{
  unsigned char s;

  while(size > 1) {

    s = *src++;
    if(s == 0)
      break;

    if((s >= '0' && s <= '9') ||
       (s >= 'a' && s <= 'z') ||
       (s >= 'A' && s <= 'Z') ||
       s == '/' ||
       s == '_' ||
       s == '.' ||
       s == '-') {
      *dest++ = s;
      size--;
    } else {
      if(size > 4) {
	*dest++ = '%';
	*dest++ = hexchars[(s >> 4) & 0xf];
	*dest++ = hexchars[s & 0xf];
	size -= 3;
      }
    }
  }
  *dest = 0;
}


void html_entities_decode(char *s);

/* inplace decode html entities, this relies on that no entity has a
 * code point in utf8 that is more bytes then the entity string */
void
html_entities_decode(char *s)
{
  char *e;
  int code;
  char name[10];
  uint8_t tmp;

  for(; *s; s++) {
    if(*s != '&')
      continue;
    
    e = strchr(s, ';');
    if(e == NULL)
      continue;
    
    snprintf(name, sizeof(name), "%.*s", (int)(intptr_t)(e - s - 1), s + 1);
    code = html_entity_lookup(name);
    
    if(code == -1)
      continue;

    PUT_UTF8(code, tmp, *s++ = tmp;);
    
    memmove(s, e + 1, strlen(e + 1) + 1);
    s--;
  }
}


/**
 *
 */
static void 
url_split(char *proto, int proto_size,
	  char *authorization, int authorization_size,
	  char *hostname, int hostname_size,
	  int *port_ptr,
	  char *path, int path_size,
	  const char *url)
{
  const char *p, *ls, *at, *col, *brk;

  if (port_ptr)               *port_ptr = -1;
  if (proto_size > 0)         proto[0] = 0;
  if (authorization_size > 0) authorization[0] = 0;
  if (hostname_size > 0)      hostname[0] = 0;
  if (path_size > 0)          path[0] = 0;

  /* parse protocol */
  if ((p = strchr(url, ':'))) {
    av_strlcpy(proto, url, MIN(proto_size, p + 1 - url));
    p++; /* skip ':' */
    if (*p == '/') p++;
    if (*p == '/') p++;
  } else {
    /* no protocol means plain filename */
    path_escape(path, path_size, url);
    return;
  }

  /* separate path from hostname */
  ls = strchr(p, '/');
  if(!ls)
    ls = strchr(p, '?');
  if(ls)
    path_escape(path, path_size, ls);
  else
    ls = &p[strlen(p)]; // XXX

  /* the rest is hostname, use that to parse auth/port */
  if (ls != p) {
    /* authorization (user[:pass]@hostname) */
    if ((at = strchr(p, '@')) && at < ls) {
      av_strlcpy(authorization, p,
		 MIN(authorization_size, at + 1 - p));
      p = at + 1; /* skip '@' */
    }

    if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
      /* [host]:port */
      av_strlcpy(hostname, p + 1,
		 MIN(hostname_size, brk - p));
      if (brk[1] == ':' && port_ptr)
	*port_ptr = atoi(brk + 2);
    } else if ((col = strchr(p, ':')) && col < ls) {
      av_strlcpy(hostname, p,
		 MIN(col + 1 - p, hostname_size));
      if (port_ptr) *port_ptr = atoi(col + 1);
    } else
      av_strlcpy(hostname, p,
		 MIN(ls + 1 - p, hostname_size));
  }
}


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
      if(tcp_read_line(hc->hc_fd, chunkheader, sizeof(chunkheader),
		       &hc->hc_spill) < 0)
	break;
 
      if((csize = strtol(chunkheader, NULL, 16)) == 0)
	return buf;

      buf = realloc(buf, s + csize + 1);
      if(tcp_read_data(hc->hc_fd, buf + s, csize, &hc->hc_spill))
	break;

      s += csize;
      buf[s] = 0;

      if(tcp_read_data(hc->hc_fd, chunkheader, 2, &hc->hc_spill))
	break;
    }
    free(buf);
    hf->hf_chunked_transfer = 0;
    return NULL;
  }

  s = hf->hf_rsize;
  buf = malloc(s + 1);
  buf[s] = 0;
  
  if(tcp_read_data(hc->hc_fd, buf, s, &hc->hc_spill)) {
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
http_read_response(http_file_t *hf)
{
  int li;
  char *c, *q, *argv[2];
  int code = -1;
  int64_t i64;
  http_connection_t *hc = hf->hf_connection;
  
  hf->hf_connection_mode = CONNECTION_MODE_PERSISTENT;
  hf->hf_rsize = -1;
  hf->hf_chunked_transfer = 0;

  for(li = 0; ;li++) {
    if(tcp_read_line(hc->hc_fd, hf->hf_line, sizeof(hf->hf_line),
		     &hc->hc_spill) < 0)
      return -1;

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
	hf->hf_size = i64;
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
redirect(http_file_t *hf, int *redircount, char *errbuf, size_t errlen)
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
authenticate(http_file_t *hf, char *errbuf, size_t errlen)
{
  char *username;
  char *password;
  char buf1[128];
  char buf2[128];
  int r;

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
http_connect(http_file_t *hf, char *errbuf, int errlen)
{
  char hostname[128];
  int port;

  if(hf->hf_connection != NULL)
    http_detach(hf, 0);

  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hostname, sizeof(hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

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
	   int ignore_size)
{
  int code;
  htsbuf_queue_t q;
  int redircount = 0;

  hf->hf_rsize = 0;

  reconnect:

  if(http_connect(hf, errbuf, errlen)) {
    hf->hf_size = -1;
    return -1;
  }

  if(!probe && hf->hf_size != -1)
    return 0;

  hf->hf_size = -1;

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

  tcp_write_queue(hf->hf_connection->hc_fd, &q);

  code = http_read_response(hf);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto reconnect;
  }

  switch(code) {
  case 200:
    if(!ignore_size && hf->hf_size < 0) {
      snprintf(errbuf, errlen, "Invalid HTTP 200 response");
      return -1;
    }
    hf->hf_auth_failed = 0;
    hf->hf_rsize = 0;
    return 0;
    
  case 301:
  case 302:
  case 303:
  case 307:
    hf->hf_auth_failed = 0;
    if(redirect(hf, &redircount, errbuf, errlen))
      return -1;
    goto reconnect;


  case 401:
    if(authenticate(hf, errbuf, errlen))
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
  char *url = malloc(HTTP_MAX_PATH_LEN);
  
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
      snprintf(url, HTTP_MAX_PATH_LEN, "http://%s:%d%s%s%s",
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
  char hostname[256];

reconnect:
  hf->hf_size = -1;

  if(http_connect(hf, errbuf, errlen))
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
		 hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");
  
  tcp_write_queue(hf->hf_connection->hc_fd, &q);
  code = http_read_response(hf);
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
      if(redirect(hf, &redircount, errbuf, errlen))
        return -1;
      goto reconnect;
      
    case 401:
      if(authenticate(hf, errbuf, errlen))
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
             int ignore_size)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  
  hf->hf_url = strdup(url);

  if(!http_open0(hf, 1, errbuf, errlen, ignore_size)) {
    hf->h.fh_proto = fap;
    return &hf->h;
  }

  http_destroy(hf);
  return NULL;
}

static fa_handle_t *
http_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  return http_open_ex(fap, url, errbuf, errlen, 0);
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
    if(hf->hf_connection == NULL && http_open0(hf, 0, NULL, 0, 0))
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

      if(size > 65536) {
	htsbuf_qprintf(&q, "Range: bytes=%"PRId64"-\r\n\r\n", hf->hf_pos);
      } else {
	htsbuf_qprintf(&q, 
		       "Range: bytes=%"PRId64"-%"PRId64"\r\n\r\n", 
		       hf->hf_pos, hf->hf_pos + size - 1);
      }

      tcp_write_queue(hc->hc_fd, &q);
      code = http_read_response(hf);

      if(code != 206) {
	http_detach(hf, 0);
	continue; // Try again
      }

      if(hf->hf_chunked_transfer) {
	return -1; /* Not supported atm */
      }
      if(hf->hf_rsize < size)
	size = hf->hf_rsize;

      if(size == 0)
	return size;
    }

    if(!tcp_read_data(hc->hc_fd, buf, size, &hc->hc_spill)) {
      hf->hf_pos   += size;
      hf->hf_rsize -= size;
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
  off_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = hf->hf_pos + pos;
    break;

  case SEEK_END:
    np = hf->hf_size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;


  if(hf->hf_pos != np) {
    if(hf->hf_rsize != 0)  // We've data pending on socket, disconnect
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
  return hf->hf_size;
}


/**
 * Standard unix stat
 */
static int
http_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	  char *errbuf, size_t errlen)
{
  fa_handle_t *handle;
  http_file_t *hf;

  if((handle = http_open_ex(fap, url, errbuf, errlen, 1)) == NULL)
    return -1;
 
  hf = (http_file_t *)handle;
  
  /* no content length and text/html, assume "index of" page */
  if(hf->hf_size < 0 &&
     hf->hf_content_type && strstr(hf->hf_content_type, "text/html"))
    buf->st_mode = S_IFDIR;
  else
    buf->st_mode = S_IFREG;
  buf->st_size = hf->hf_size;
  
  http_destroy(hf);
  return 0;
}



/**
 *
 */
fa_protocol_t fa_protocol_http = {
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
};




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
  char *rpath = malloc(HTTP_MAX_PATH_LEN);
  char *path  = malloc(HTTP_MAX_PATH_LEN);
  char *fname = malloc(HTTP_MAX_PATH_LEN);
  char *ehref = malloc(HTTP_MAX_PATH_LEN); // Escaped href

  // We need to compare paths and to do so, we must deescape the
  // possible URL encoding. Do the searched-for path once
  snprintf(rpath, HTTP_MAX_PATH_LEN, "%s", hf->hf_path);
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
      snprintf(ehref, HTTP_MAX_PATH_LEN, "%s", href);
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
	  snprintf(path, HTTP_MAX_PATH_LEN, "webdav://%s:%d%s", 
		   hc->hc_hostname, hc->hc_port, href);
	} else {
	  snprintf(path, HTTP_MAX_PATH_LEN, "webdav://%s%s", 
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

	    for(i = 0; i < HTTP_MAX_PATH_LEN - 1 && q[i] != '/'; i++)
	      fname[i] = q[i];
	    fname[i] = 0;

	  } else {
	    snprintf(fname, HTTP_MAX_PATH_LEN, "%s", q);
	  }
	  http_deescape(fname);
	  http_deescape(path);

	  fa_dir_add(fd, path, fname, isdir ? CONTENT_DIR : CONTENT_FILE);
	}
      }
    } else {
      /* single entry stat(2) */

      snprintf(fname, HTTP_MAX_PATH_LEN, "%s", href);
      http_deescape(fname);

      if(!strcmp(rpath, fname)) {
	/* This is the path we asked for */

	hf->hf_isdir = isdir;

	if(!isdir) {
	  d = get_cdata_by_tag(c, "DAV:getcontentlength");
	  hf->hf_size = strtoll(d, NULL, 10);
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
dav_propfind(http_file_t *hf, fa_dir_t *fd, char *errbuf, size_t errlen)
{
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  htsmsg_t *xml;
  int redircount = 0;
  char err0[128];

 reconnect:
  hf->hf_size = -1;

  if(http_connect(hf, errbuf, errlen))
    return -1;

  htsbuf_queue_init(&q, 0);

 again:
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

  tcp_write_queue(hf->hf_connection->hc_fd, &q);
  code = http_read_response(hf);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto reconnect;
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
     if(redirect(hf, &redircount, errbuf, errlen))
      return -1;
    goto reconnect;

  case 401:
    if(authenticate(hf, errbuf, errlen))
      return -1;
    goto again;

  default:
    http_drain_content(hf);
    snprintf(errbuf, errlen, "Unhandled HTTP response %d", code);
    return -1;
  }
}



/**
 * Standard unix stat
 */
static int
dav_stat(fa_protocol_t *fap, const char *url, struct stat *buf,
	 char *errbuf, size_t errlen)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));

  hf->hf_url = strdup(url);
  
  if(dav_propfind(hf, NULL, errbuf, errlen)) {
    http_destroy(hf);
    return -1;
  }

  memset(buf, 0, sizeof(struct stat));

  buf->st_mode = hf->hf_isdir ? S_IFDIR : S_IFREG;
  buf->st_size = hf->hf_size;
  
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
  
  retval = dav_propfind(hf, fd, errbuf, errlen);
  http_destroy(hf);
  return retval;
}



/**
 *
 */
fa_protocol_t fa_protocol_webdav = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL,
  .fap_name  = "webdav",
  .fap_scan  = dav_scandir,
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = dav_stat,
};


/**
 *
 */
int
http_request(const char *hostname, int port, const char *path,
	     const char **arguments, char **result, size_t *result_sizep,
	     char *errbuf, size_t errlen)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  htsbuf_queue_t q;
  int code, r;
  char buf[256];
  http_connection_t *hc;

 retry:
  hc = hf->hf_connection = http_connection_get(hostname, port, errbuf, errlen);
  if(hf->hf_connection == NULL)
    return -1;

  htsbuf_queue_init(&q, 0);

  htsbuf_qprintf(&q, "GET %s", path);

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
		 "Host: %s\r\n"
		 "\r\n",
		 htsversion,
		 hostname);

  tcp_write_queue(hf->hf_connection->hc_fd, &q);

  code = http_read_response(hf);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0);
    goto retry;
  }

  if(code != 200) {
    snprintf(errbuf, errlen, "HTTP error: %d", code);
    http_destroy(hf);
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

      r = tcp_read_data2(hc->hc_fd, mem + size, capacity - size, &hc->hc_spill);
      if(r < 0)
	break;

      size += r;
    }

    mem[size] = 0;

    *result = mem;
    *result_sizep = size;
    
  } else {

    char *mem = malloc(hf->hf_size + 1);

    r = tcp_read_data(hc->hc_fd, mem, hf->hf_size, &hc->hc_spill);

    if(r == -1) {
      snprintf(errbuf, errlen, "HTTP read error");
      free(mem);
      http_destroy(hf);
      return -1;    
    }

    mem[hf->hf_size] = 0;
    *result = mem;
    *result_sizep = hf->hf_size;
  }

  http_destroy(hf);
  return 0;
}




