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
#include <stdio.h>
#include <string.h>
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

  int hf_fd;

  htsbuf_queue_t hf_spill;

  char hf_line[1024];

  char hf_auth[128];
  char hf_path[256];
  char hf_hostname[128];

  int64_t hf_csize; /* Size of chunk */

  int64_t hf_size; /* Full size of file, if known */

  int64_t hf_pos;

} http_file_t;


static int
http_read_respone(http_file_t *hf)
{
  int li;
  char *q;
  int code = -1;
  int64_t i64;
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
    } else if(!strncmp(hf->hf_line, 
		       "Content-Length: ",
		       strlen("Content-Length: "))) {
      
      i64 = strtoll(hf->hf_line + strlen("Content-Length: "), NULL, 0);
      hf->hf_csize = i64;

      if(code == 200)
	hf->hf_size = i64;
    }
  }
  return code;
}




static int
http_connect(http_file_t *hf)
{
  char errbuf[128];
  int port, code;
  htsbuf_queue_t q;

  hf->hf_size = -1;
  hf->hf_fd = -1;

  url_split(NULL, 0, hf->hf_auth, sizeof(hf->hf_auth), 
	    hf->hf_hostname, sizeof(hf->hf_hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), hf->hf_url);

  if(port < 0)
    port = 80;

  hf->hf_fd = tcp_connect(hf->hf_hostname, port, errbuf, sizeof(errbuf), 3000);
  if(hf->hf_fd < 0)
    return -1;

  htsbuf_queue_init(&q, 0);

  htsbuf_qprintf(&q, 
		 "HEAD %s HTTP/1.1\r\n"
		 "Accept: */*\r\n"
		 "User-Agent: Showtime %s\r\n"
		 "Host: %s\r\n"
		 "\r\n",
		 hf->hf_path,
		 htsversion,
		 hf->hf_hostname);

  tcp_write_queue(hf->hf_fd, &q);

  code = http_read_respone(hf);

  switch(code) {

  case 200:
    return hf->hf_size < 0 ? -1: 0;
    
    /* TODO: Redirects */

  default:
    return -1;
  }
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
static void
http_destroy(http_file_t *hf)
{
  http_disconnect(hf);
  free(hf->hf_url);
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
  int i;

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
		   "\r\n",
		   hf->hf_path,
		   htsversion,
		   hf->hf_hostname,
		   hf->hf_pos, hf->hf_pos + size - 1);

    tcp_write_queue(hf->hf_fd, &q);
    http_read_respone(hf);

    if(hf->hf_csize < size)
      size = hf->hf_csize;

    if(!tcp_read_data(hf->hf_fd, buf, size, &hf->hf_spill)) {
      hf->hf_pos += size;
      return size;
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
