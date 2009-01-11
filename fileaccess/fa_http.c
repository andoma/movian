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
#include <assert.h>
#include <libavutil/base64.h>
#include <libhts/htsmsg_xml.h>

#include "keyring.h"
#include "fileaccess.h"
#include "networking/net.h"

extern char *htsversion;

/* XXX: From lavf */
extern void url_split(char *proto, int proto_size,
		      char *authorization, int authorization_size,
		      char *hostname, int hostname_size,
		      int *port_ptr,
		      char *path, int path_size,
		      const char *url);


typedef struct http_file {
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
redirect(http_file_t *hf, int *redircount)
{
  (*redircount)++;
  if(*redircount == 10)
    return -1;
  
  if(hf->hf_location == NULL)
    return -1;

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
authenticate(http_file_t *hf)
{
  char *username;
  char *password;
  char buf1[128];
  char buf2[128];
  int r;

  if(http_drain_content(hf))
    return -1;

  if(hf->hf_auth_realm == NULL)
    return -1;

  snprintf(buf1, sizeof(buf1), "%s @ %s", hf->hf_auth_realm, hf->hf_hostname);

  r = keyring_lookup(buf1, &username, &password, NULL, 
		     hf->hf_auth_failed > 0,
		     "HTTP Client", "Access denied");

  hf->hf_auth_failed++;

  free(hf->hf_auth);
  hf->hf_auth = NULL;

  if(r == -1)
    /* Rejected */
    return -1;

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
http_connect(http_file_t *hf)
{
  char errbuf[128];
  int port, code;
  htsbuf_queue_t q;
  int redircount = 0;

 reconnect:
  hf->hf_size = -1;
  hf->hf_fd = -1;

  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

  if(port < 0)
    port = 80;

  hf->hf_fd = tcp_connect(hf->hf_hostname, port, errbuf, sizeof(errbuf), 3000);
  if(hf->hf_fd < 0)
    return -1;

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
    hf->hf_auth_failed = 0;
    return hf->hf_size < 0 ? -1: 0;
    
  case 301:
  case 302:
  case 303:
  case 307:
    hf->hf_auth_failed = 0;
    if(redirect(hf, &redircount))
      return -1;
    goto reconnect;


  case 401:
    if(authenticate(hf))
      return -1;
    goto again;

  default:
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
  free(hf);
}




/**
 * Open file
 */
static void *
http_open(const char *url)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  
  htsbuf_queue_init(&hf->hf_spill, 0);

  hf->hf_url = strdup(url);

  if(!http_connect(hf))
    return hf;

  http_destroy(hf);
  return NULL;
}




/**
 * Close file
 */
static void
http_close(void *handle)
{
  http_file_t *hf = handle;
  http_destroy(hf);
}


/**
 * Read from file
 */
static int
http_read(void *handle, void *buf, size_t size)
{
  http_file_t *hf = handle;
  htsbuf_queue_t q;
  int i, code;

  if(size == 0)
    return 0;

  for(i = 0; i < 5; i++) {

    htsbuf_queue_init(&q, 0);

    htsbuf_qprintf(&q, 
		   "GET %s HTTP/1.1\r\n"
		   "Accept: */*\r\n"
		   "User-Agent: Showtime %s\r\n"
		   "Host: %s\r\n"
		   "Range: bytes=%"PRId64"-%"PRId64"\r\n"
		   "%s%s"
		   "\r\n",
		   hf->hf_path,
		   htsversion,
		   hf->hf_hostname,
		   hf->hf_pos, hf->hf_pos + size - 1,
		   hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

    tcp_write_queue(hf->hf_fd, &q);
    code = http_read_respone(hf);
    if(code == 206) {

      if(hf->hf_chunked_transfer)
	return -1; /* Not supported atm */

      if(hf->hf_rsize < size)
	size = hf->hf_rsize;

      if(size == 0)
	return size;

      if(!tcp_read_data(hf->hf_fd, buf, size, &hf->hf_spill)) {
	hf->hf_pos += size;
	return size;
      }
    }
    http_disconnect(hf);
    
    if(http_connect(hf))
      return -1;
  }
  return -1;
}


/**
 * Seek in file
 */
static int64_t
http_seek(void *handle, int64_t pos, int whence)
{
  http_file_t *hf = handle;
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

  hf->hf_pos = np;
  return np;
}


/**
 * Return size of file
 */
static int64_t
http_fsize(void *handle)
{
  http_file_t *hf = handle;
  return hf->hf_size;
}


/**
 * Standard unix stat
 */
static int
http_stat(const char *url, struct stat *buf)
{
  http_file_t *hf;

  if((hf = http_open(url)) == NULL)
    return -1;
 
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
  if((sub = htsmsg_get_msg(tags, name)) == NULL)
    return NULL;
  return htsmsg_get_str(sub, "cdata");
}


/**
 * Parse WEBDAV PROPFIND results
 */
static int
parse_propfind(http_file_t *hf, htsmsg_t *xml, fa_dir_t *fd)
{
  htsmsg_t *m, *c;
  htsmsg_field_t *f;
  const char *href, *d, *q;
  int isdir, i;
  char path[256];
  char fname[128];

  if((m = htsmsg_get_msg_multi(xml, "tags", 
			       "DAV:multistatus", "tags", NULL)) == NULL)
    return -1;

  HTSMSG_FOREACH(f, m) {
    if(strcmp(f->hmf_name, "DAV:response"))
      continue;
    if((c = htsmsg_get_msg_by_field(f)) == NULL)
      continue;

    if((c = htsmsg_get_msg(c, "tags")) == NULL)
      continue;

    if((href = get_cdata_by_tag(c, "DAV:href")) == NULL)
      continue;

    if((c = htsmsg_get_msg_multi(c, "DAV:propstat", "tags",
				 "DAV:prop", "tags", NULL)) == NULL)
      continue;

    isdir = !!htsmsg_get_msg_multi(c, "DAV:resourcetype", "tags",
				   "DAV:collection", NULL);

    if(fd != NULL) {

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
	  fa_dir_add(fd, path, fname, isdir ? FA_DIR : FA_FILE);
	}
      }
    } else {
      /* single entry stat(2) */
      if(!strcmp(hf->hf_path, href)) {
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

  if(fd == NULL)
    return -1; /* We should have returned earlier */
  return 0;
}


/**
 * Execute a webdav PROPFIND
 */
static int
dav_propfind(http_file_t *hf, fa_dir_t *fd)
{
  char errbuf[128];
  int code, retval;
  htsbuf_queue_t q;
  char *buf;
  htsmsg_t *xml;
  int redircount = 0;

 reconnect:
  hf->hf_size = -1;
  hf->hf_fd = -1;

  url_split(NULL, 0, hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &hf->hf_port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

  if(hf->hf_port < 0)
    hf->hf_port = 80;

  hf->hf_fd = tcp_connect(hf->hf_hostname, hf->hf_port,
			  errbuf, sizeof(errbuf), 3000);

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
		 fd != NULL ? 1 : 0,
		 htsversion,
		 hf->hf_hostname,
		 hf->hf_auth ?: "", hf->hf_auth ? "\r\n" : "");

  tcp_write_queue(hf->hf_fd, &q);
  code = http_read_respone(hf);

  switch(code) {

  case 207: /* 207 Multi-part */
    hf->hf_auth_failed = 0;

    if((buf = http_read_content(hf)) == NULL)
      return -1;

    /* XML parser consumes 'buf' */
    if((xml = htsmsg_xml_deserialize(buf, errbuf, sizeof(errbuf))) == NULL) {
      fprintf(stderr, "webdav/PROPFIND: XML parsing failed -- %s\n", errbuf);
      return -1;
    }
    retval = parse_propfind(hf, xml, fd);
    htsmsg_destroy(xml);
    return retval;

  case 301:
  case 302:
  case 303:
  case 307:
    hf->hf_auth_failed = 0;
     if(redirect(hf, &redircount))
      return -1;
    goto reconnect;

  case 401:
    if(authenticate(hf))
      return -1;
    goto again;

  default:
    return -1;
  }
}



/**
 * Standard unix stat
 */
static int
dav_stat(const char *url, struct stat *buf)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  htsbuf_queue_init(&hf->hf_spill, 0);
  hf->hf_url = strdup(url);
  
  if(dav_propfind(hf, NULL)) {
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
dav_scandir(fa_dir_t *fd, const char *url)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));

  htsbuf_queue_init(&hf->hf_spill, 0);
  hf->hf_url = strdup(url);
  
  retval = dav_propfind(hf, fd);
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
