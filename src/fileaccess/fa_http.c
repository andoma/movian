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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <regex.h>
#include <assert.h>
#include <libavutil/base64.h>
#include <libavutil/avstring.h>
#include <htsmsg/htsmsg_xml.h>

#include "keyring.h"
#include "fileaccess.h"
#include "networking/net.h"
#include "fa_proto.h"
#include "showtime.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern char *htsversion;

typedef struct http_file {
  fa_handle_t h;

  char *hf_url;
  char *hf_auth;
  char *hf_location;
  char *hf_auth_realm;

  int hf_fd;

  htsbuf_queue_t hf_spill;

  char hf_line[1024];

  char hf_authurl[128];
  char hf_path[256];
  char hf_hostname[128];

  int hf_chunked_transfer;

  int64_t hf_rsize; /* Size of reply, if chunked: don't care about this */

  int64_t hf_size; /* Full size of file, if known */

  int64_t hf_pos;

  int hf_isdir;
  int hf_port;

  int hf_auth_failed;
  
  char *hf_content_type;

} http_file_t;

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

  if(hf->hf_chunked_transfer) {
    buf = NULL;
    s = 0;

    while(1) {
      if(tcp_read_line(hf->hf_fd, chunkheader, sizeof(chunkheader),
		       &hf->hf_spill) < 0)
	break;
 
      if((csize = strtol(chunkheader, NULL, 16)) == 0)
	return buf;

      buf = realloc(buf, s + csize + 1);
      if(tcp_read_data(hf->hf_fd, buf + s, csize, &hf->hf_spill))
	break;

      s += csize;
      buf[s] = 0;

      if(tcp_read_data(hf->hf_fd, chunkheader, 2, &hf->hf_spill))
	break;
    }
    free(buf);
    return NULL;
  }

  s = hf->hf_rsize;
  buf = malloc(s + 1);
  buf[s] = 0;
  
  if(tcp_read_data(hf->hf_fd, buf, s, &hf->hf_spill)) {
    free(buf);
    return NULL;
  }
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
http_read_respone(http_file_t *hf)
{
  int li;
  char *c, *q, *argv[2];
  int code = -1;
  int64_t i64;

  hf->hf_rsize = -1;
  hf->hf_chunked_transfer = 0;

  for(li = 0; ;li++) {
    if(tcp_read_line(hf->hf_fd, hf->hf_line, sizeof(hf->hf_line),
		     &hf->hf_spill) < 0)
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
      hf->hf_content_type = strdup(argv[1]);
    }
  }
  return code;
}




/**
 *
 */
static void
http_disconnect(http_file_t *hf)
{
  if(hf->hf_fd != -1) {
    tcp_close(hf->hf_fd);
    hf->hf_fd = -1;
  }
  htsbuf_queue_flush(&hf->hf_spill);
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

  free(hf->hf_url);
  hf->hf_url = hf->hf_location;
  hf->hf_location = NULL;
  
  http_disconnect(hf);
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
  snprintf(buf1, sizeof(buf1), "%s @ %s", hf->hf_auth_realm, hf->hf_hostname);

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
http_connect(http_file_t *hf, int probe, char *errbuf, int errlen,
             int ignore_size)
{
  int port, code;
  htsbuf_queue_t q;
  int redircount = 0;

  hf->hf_rsize = 0;

  reconnect:
  hf->hf_fd = -1;
  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

  if(port < 0)
    port = 80;

  /* empty path, default to "/" */ 
  if(!hf->hf_path[0])
    strcpy(hf->hf_path, "/");

  hf->hf_fd = tcp_connect(hf->hf_hostname, port, errbuf, errlen, 3000);
  if(hf->hf_fd < 0) {
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
		 hf->hf_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

  tcp_write_queue(hf->hf_fd, &q);

  code = http_read_respone(hf);

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
  http_disconnect(hf);
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
http_index_parse(http_file_t *hf, nav_dir_t *nd, char *buf)
{
  static regex_t *ahref_preg = NULL;
  regmatch_t matches[3];
  char *p, *n;
  
  if(!ahref_preg) {
    ahref_preg = malloc(sizeof(ahref_preg));
    regcomp(ahref_preg, "<a href=\"(.*)\">(.*)</a>", REG_EXTENDED);
  }
  
  p = buf;
  while((n = strchr(p, '\n'))) {
    *n = '\0';
    
    if(regexec(ahref_preg, p, 3, matches, 0)) {
      /* no match */
    } else {
      char *href, *name, *hrefd;
      int isdir;
      char url[500];
      
      href = &p[matches[1].rm_so];
      p[matches[1].rm_eo] = '\0';
      name = &p[matches[2].rm_so];
      p[matches[2].rm_eo] = '\0';
      
      /* when does this happen? xmbc does it */
      if(href[0] == '/')
        href++;
      
      isdir = http_strip_last(name, '/');
      http_strip_last(href, '/');
      
      hrefd = strdup(href);
      http_deescape(hrefd);
      
      /* skip parent dir links etc */
      if(strcmp(name, hrefd) == 0) {
        snprintf(url, sizeof(url), "http://%s:%d%s%s%s",
                 hf->hf_hostname, hf->hf_port, hf->hf_path,
                 href,
                 isdir ? "/" : "");
        
	http_deescape(url);

        nav_dir_add(nd, url, name,
                    isdir ? CONTENT_DIR : CONTENT_FILE,
                    NULL);
      }
      
      free(hrefd);
    }
    
    p = n + 1; /* skip '\0' */
  }
  
  return 0;
}


/**
 *
 */
static int
http_index_fetch(http_file_t *hf, nav_dir_t *nd, char *errbuf, size_t errlen)
{
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  int redircount = 0;
  
reconnect:
  hf->hf_size = -1;
  hf->hf_fd = -1;
  
  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &hf->hf_port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);
  
  if(hf->hf_port < 0)
    hf->hf_port = 80;
  
  /* empty path, default to "/" */ 
  if(!hf->hf_path[0])
    strcpy(hf->hf_path, "/");
  
  hf->hf_fd = tcp_connect(hf->hf_hostname, hf->hf_port,
			  errbuf, errlen, 3000);
  
  if(hf->hf_fd < 0)
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
		 hf->hf_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");
  
  tcp_write_queue(hf->hf_fd, &q);
  code = http_read_respone(hf);
  
  switch(code) {
      
    case 200: /* 200 OK */
      hf->hf_auth_failed = 0;
      
      if((buf = http_read_content(hf)) == NULL) {
        snprintf(errbuf, errlen, "Connection lost");
        return -1;
      }
      
      retval = http_index_parse(hf, nd, buf);
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
http_scandir(nav_dir_t *nd, const char *url, char *errbuf, size_t errlen)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  
  htsbuf_queue_init(&hf->hf_spill, 0);
  hf->hf_url = strdup(url);
  
  retval = http_index_fetch(hf, nd, errbuf, errlen);
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
  
  htsbuf_queue_init(&hf->hf_spill, 0);

  hf->hf_url = strdup(url);

  if(!http_connect(hf, 1, errbuf, errlen, ignore_size)) {
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
    if(hf->hf_fd == -1 && http_connect(hf, 0, NULL, 0, 0))
      return -1;

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
		     hf->hf_hostname,
		     hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

      if(size > 1024) {
	htsbuf_qprintf(&q, "Range: bytes=%"PRId64"-\r\n\r\n", hf->hf_pos);
      } else {
	htsbuf_qprintf(&q, 
		       "Range: bytes=%"PRId64"-%"PRId64"\r\n\r\n", 
		       hf->hf_pos, hf->hf_pos + size - 1);
      }

      tcp_write_queue(hf->hf_fd, &q);
      code = http_read_respone(hf);

      if(code != 206) {
	http_disconnect(hf);
	continue; // Try again
      }

      if(hf->hf_chunked_transfer)
	return -1; /* Not supported atm */

      if(hf->hf_rsize < size)
	size = hf->hf_rsize;

      if(size == 0)
	return size;
    }

    if(!tcp_read_data(hf->hf_fd, buf, size, &hf->hf_spill)) {
      hf->hf_pos   += size;
      hf->hf_rsize -= size;
      return size;
    }
  }
  http_disconnect(hf);
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
      http_disconnect(hf);
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
parse_propfind(http_file_t *hf, htsmsg_t *xml, nav_dir_t *nd)
{
  htsmsg_t *m, *c, *c2;
  htsmsg_field_t *f;
  const char *href, *d, *q;
  int isdir, i;
  char path[256];
  char fname[128];

  if(nd == NULL) {
    // Single entry stat(2) (ie. not a directory scan)
    // We need to compare paths and to do so, we must deescape the
    // possible URL encoding. Do the searched-for path once
    snprintf(path, sizeof(path), "%s", hf->hf_path);
    http_deescape(path);
  }

  if((m = htsmsg_get_map_multi(xml, "tags", 
			       "DAV:multistatus", "tags", NULL)) == NULL)
    return -1;

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

    if((c = htsmsg_get_map_multi(c, "DAV:propstat", "tags",
				 "DAV:prop", "tags", NULL)) == NULL)
      continue;

    isdir = !!htsmsg_get_map_multi(c, "DAV:resourcetype", "tags",
				   "DAV:collection", NULL);

    if(nd != NULL) {

      if(strcmp(hf->hf_path, href)) {

	snprintf(path, sizeof(path), "webdav://%s:%d%s", 
		 hf->hf_hostname, hf->hf_port, href);
	
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

	    for(i = 0; i < sizeof(fname) - 1 && q[i] != '/'; i++)
	      fname[i] = q[i];
	    fname[i] = 0;

	  } else {
	    snprintf(fname, sizeof(fname), "%s", q);
	  }
	  http_deescape(fname);
	  http_deescape(path);

	  nav_dir_add(nd, path, fname, isdir ? CONTENT_DIR : CONTENT_FILE,
		      NULL);
	}
      }
    } else {
      /* single entry stat(2) */

      snprintf(fname, sizeof(fname), "%s", href);
      http_deescape(fname);

      if(!strcmp(path, fname)) {
	/* This is the path we asked for */

	hf->hf_isdir = isdir;

	if(!isdir) {
	  d = get_cdata_by_tag(c, "DAV:getcontentlength");
	  hf->hf_size = strtoll(d, NULL, 10);
	}
	return 0;
      } 
    }
  }

  if(nd == NULL)
    return -1; /* We should have returned earlier */
  return 0;
}


/**
 * Execute a webdav PROPFIND
 */
static int
dav_propfind(http_file_t *hf, nav_dir_t *nd, char *errbuf, size_t errlen)
{
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  htsmsg_t *xml;
  int redircount = 0;
  char err0[128];

 reconnect:
  hf->hf_size = -1;
  hf->hf_fd = -1;

  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &hf->hf_port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

  if(hf->hf_port < 0)
    hf->hf_port = 80;

  /* empty path, default to "/" */ 
  if(!hf->hf_path[0])
    strcpy(hf->hf_path, "/");

  hf->hf_fd = tcp_connect(hf->hf_hostname, hf->hf_port,
			  errbuf, errlen, 3000);

  if(hf->hf_fd < 0)
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
		 nd != NULL ? 1 : 0,
		 htsversion,
		 hf->hf_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

  tcp_write_queue(hf->hf_fd, &q);
  code = http_read_respone(hf);

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
    retval = parse_propfind(hf, xml, nd);
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
  htsbuf_queue_init(&hf->hf_spill, 0);
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
dav_scandir(nav_dir_t *nd, const char *url, char *errbuf, size_t errlen)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));

  htsbuf_queue_init(&hf->hf_spill, 0);
  hf->hf_url = strdup(url);
  
  retval = dav_propfind(hf, nd, errbuf, errlen);
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
