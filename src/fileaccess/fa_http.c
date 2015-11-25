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
#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#if ENABLE_LIBAV
#include <libavutil/base64.h>
#endif
#include <assert.h>
#include <zlib.h>

#include "main.h"

#include "keyring.h"
#include "fileaccess.h"
#include "http_client.h"
#include "networking/net.h"
#include "fa_proto.h"
#include "task.h"
#include "htsmsg/htsmsg_xml.h"
#include "htsmsg/htsmsg_store.h"
#include "misc/str.h"
#include "misc/sha.h"
#include "misc/callout.h"
#include "misc/average.h"
#include "misc/minmax.h"

#include "usage.h"

LIST_HEAD(http_request_inspector_list, http_request_inspector);
static struct http_request_inspector_list http_request_inspectors;

/**
 * If we are reading data as a constant pushed stream and we get a
 * seek request forward in the file it might be better to just
 * continue to read and drop the data if the seek offset is below a
 * certain limit. SEEK_BY_READ_THRES is this limit.
 */
#define SEEK_BY_READ_THRES (256*1024)


/**
 * If we read more than this in a sequence, we switch to a continous
 * HTTP stream (instead of ranges)
 */
#define STREAMING_LIMIT 128000



static int http_tokenize(char *buf, char **vec, int vecsize, int delimiter);


#define HTTP_TRACE(dbg, x, ...) do {                    \
    if(dbg)                                             \
      TRACE(TRACE_DEBUG, "HTTP", x, ##__VA_ARGS__);     \
  } while(0)

#define HF_TRACE(hf, x, ...) do {                               \
    if((hf)->hf_debug)                                          \
      hf_trace0(hf, x, ##__VA_ARGS__);                           \
  } while(0)



/**
 * Connection parking
 */
TAILQ_HEAD(http_connection_queue , http_connection);

static struct http_connection_queue http_connections;
static int http_parked_connections;
static hts_mutex_t http_connections_mutex;
static atomic_t http_connection_tally;
static atomic_t http_file_tally;

typedef struct http_connection {
  char hc_hostname[HOSTNAME_MAX];
  int hc_port;
  int hc_id;
  tcpcon_t *hc_tc;

  TAILQ_ENTRY(http_connection) hc_link;

  char hc_ssl;
  char hc_reused;

  time_t hc_reuse_before;

} http_connection_t;



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

  char **hf_ret_location; // Return final location to caller

  char hf_authurl[128];
  char hf_path[URL_MAX];

  int hf_status_code;

  int hf_chunk_size;

  int64_t hf_rsize; /* Size of reply, if chunked: don't care about this */

  int64_t hf_filesize; /* -1 if filesize can not be determined */
  int64_t hf_pos;

  int64_t hf_consecutive_read;

  char *hf_content_type;

  /* The negotiated connection mode (ie, what the server replied with) */
  enum {
    CONNECTION_MODE_PERSISTENT,
    CONNECTION_MODE_CLOSE,
  } hf_connection_mode;

  time_t hf_mtime;

  char hf_chunked_transfer;
  char hf_isdir;

  char hf_auth_failed;
  

  char hf_debug;

  char hf_no_ranges; // Server does not accept range queries

  char hf_want_close;

  char hf_accept_ranges;

  char hf_version;

  char hf_streaming; /* Optimize for streaming from start to end
		      * rather than random seeking
		      */

  char hf_no_retries;

  char hf_req_compression;

  char *hf_original_url;

  // Set if the filesize is known to never change
  char hf_filesize_is_final;

  char hf_content_encoding;
#define HTTP_CE_IDENTITY 0
#define HTTP_CE_GZIP 1

  int hf_max_age;

  int hf_connect_timeout;
  int hf_read_timeout;

  prop_t *hf_stats_speed;

  const struct http_header_list *hf_user_request_headers;
  struct http_header_list *hf_user_response_headers;

  cancellable_t *hf_cancellable;

  uint64_t hf_bytes_downloaded;

  average_t hf_download_rate;

  int hf_id;

  char hf_line[4096];

} http_file_t;


/**
 *
 */
static void
hf_trace0(http_file_t *hf, const char *fmt, ...)
{
  va_list ap;
  char subsys[64];
  snprintf(subsys, sizeof(subsys), "HTTP-%d", hf->hf_id);
  va_start(ap, fmt);
  tracev(0, TRACE_DEBUG, subsys, fmt, ap);
  va_end(ap);
}


/**
 *
 */
static void
trace_request(htsbuf_queue_t *hq, http_file_t *hf)
{
  char subsys[64];
  snprintf(subsys, sizeof(subsys), "HTTP-%d", hf->hf_id);

  char *r = malloc(hq->hq_size + 1);
  htsbuf_peek(hq, r, hq->hq_size);
  r[hq->hq_size] = 0;
  LINEPARSE(s, r)
    TRACE(TRACE_DEBUG, subsys, "> %s", s);
  free(r);
}


/**
 *
 */
static void
http_connection_destroy(http_connection_t *hc, int dbg, const char *reason)
{
  HTTP_TRACE(dbg, "Disconnected from %s:%d (cid=%d) %s",
	     hc->hc_hostname, hc->hc_port, hc->hc_id, reason);
  tcp_close(hc->hc_tc);
  free(hc);
}



/**
 *
 */
static http_connection_t *
http_connection_get(const char *hostname, int port, int ssl,
		    char *errbuf, int errlen, int dbg, int timeout,
                    cancellable_t *c, int allow_reuse)
{
  http_connection_t *hc, *next;
  tcpcon_t *tc;

  if(allow_reuse) {

    time_t now;

    time(&now);

    hts_mutex_lock(&http_connections_mutex);

    for(hc = TAILQ_FIRST(&http_connections); hc != NULL; hc = next) {
      next = TAILQ_NEXT(hc, hc_link);

      if(now >= hc->hc_reuse_before) {
        TAILQ_REMOVE(&http_connections, hc, hc_link);
        http_parked_connections--;
        http_connection_destroy(hc, dbg, "Keep alive expired");
        continue;
      }

      if(!strcmp(hc->hc_hostname, hostname) && hc->hc_port == port &&
         hc->hc_ssl == ssl) {
        TAILQ_REMOVE(&http_connections, hc, hc_link);
        http_parked_connections--;
        hts_mutex_unlock(&http_connections_mutex);
        HTTP_TRACE(dbg, "Reusing connection to %s:%d (cid=%d)",
                   hc->hc_hostname, hc->hc_port, hc->hc_id);
        hc->hc_reused = 1;
        tcp_set_cancellable(hc->hc_tc, c);
        return hc;
      }
    }
    hts_mutex_unlock(&http_connections_mutex);
  }

  const int id = atomic_add_and_fetch(&http_connection_tally, 1);

  if(errbuf == NULL || errlen == 0) {
    char xerrbuf[256];
    errbuf = xerrbuf;
    errlen = sizeof(xerrbuf);
  }


  int tcp_connect_flags = 0;
  if(dbg)
    tcp_connect_flags |= TCP_DEBUG;
  if(ssl)
    tcp_connect_flags |= TCP_SSL;

  HTTP_TRACE(dbg, "Connecting to %s:%d", hostname, port);

  if((tc = tcp_connect(hostname, port, errbuf, errlen,
                       timeout, tcp_connect_flags, c)) == NULL) {
    HTTP_TRACE(dbg, "Connection to %s:%d failed -- %s%s",
               hostname, port, errbuf,
               cancellable_is_cancelled(c) ? ", Cancelled by user" : "");
    return NULL;
  }
  if(cancellable_is_cancelled(c)) {
    snprintf(errbuf, errlen, "Cancelled");
    tcp_close(tc);
    return NULL;
  }

  HTTP_TRACE(dbg, "Connected to %s:%d (cid=%d)", hostname, port, id);

  hc = malloc(sizeof(http_connection_t));
  snprintf(hc->hc_hostname, sizeof(hc->hc_hostname), "%s", hostname);
  hc->hc_port = port;
  hc->hc_ssl = ssl;
  hc->hc_tc = tc;
  hc->hc_reused = 0;
  hc->hc_id = id;
  return hc;
}


/**
 *
 */
static void
http_connection_park(http_connection_t *hc, int dbg, int max_age, const char *reason)
{
  time_t now;
  http_connection_t *next;

  time(&now);

  tcp_set_read_timeout(hc->hc_tc, 0);
  tcp_set_cancellable(hc->hc_tc, NULL);

  HTTP_TRACE(dbg, "Parking connection to %s:%d (cid=%d) -- %s",
	     hc->hc_hostname, hc->hc_port, hc->hc_id, reason);

  hc->hc_reuse_before = now + max_age;

  hts_mutex_lock(&http_connections_mutex);
  TAILQ_INSERT_TAIL(&http_connections, hc, hc_link);
  http_parked_connections++;

  if(http_parked_connections > 5) {
    for(hc = TAILQ_FIRST(&http_connections); hc != NULL; hc = next) {
      next = TAILQ_NEXT(hc, hc_link);

      if(now >= hc->hc_reuse_before) {
	TAILQ_REMOVE(&http_connections, hc, hc_link);
	http_parked_connections--;
	http_connection_destroy(hc, dbg, "Keep alive expired");
      }
    }
  }

  while(http_parked_connections > 5) {
    hc = TAILQ_FIRST(&http_connections);
    assert(hc != NULL);
    TAILQ_REMOVE(&http_connections, hc, hc_link);
    http_connection_destroy(hc, dbg, "Too many idle connections");
    http_parked_connections--;
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
LIST_HEAD(http_cookie_list, http_cookie);
static struct http_cookie_list http_cookies;
static hts_mutex_t http_cookies_mutex;

/**
 *
 */
typedef struct http_cookie {

  LIST_ENTRY(http_cookie) hc_link;

  char *hc_name;
  char *hc_path;
  char *hc_domain;

  char *hc_value;
  time_t hc_expire;

} http_cookie_t;

/**
 * RFC 2109 : 4.3.2 Rejecting Cookies
 */
static int
validate_cookie(const char *req_host, const char *req_path,
		const char *domain, const char *path)
{
  const char *x;
  /*
   * The value for the Path attribute is not a prefix of the request-
   * URI.
   */
   if(strncmp(req_path, path, strlen(path)))
    return 1;

  /*
   * The value for the Domain attribute contains no embedded dots or
   * does not start with a dot.
   * Unless it matches the req_host perfectly
   */

   if(strcmp(domain, req_host)) {
     if(*domain == '.')
       domain++;
     if(strchr(domain + 1, '.') == NULL)
       return 2;
   }

  /*
   * The value for the request-host does not domain-match the Domain
   * attribute.
   */
  const char *s = strstr(req_host, domain);

  if(s == NULL && *domain == '.' && !strcmp(req_host, domain + 1))
    s = req_host;
  else if(s == NULL || s[strlen(domain)] != 0)
    return 3;

  /*
   * The request-host is a FQDN (not IP address) and has the form HD,
   * where D is the value of the Domain attribute, and H is a string
   * that contains one or more dots.
   */ 

  for(x = req_host; x != s; x++) {
    if(*x == '.' && x + 1 != s)
      return 4;
  }
  return 0;
}


/**
 *
 */
static void
cookie_persist(struct callout *c, void *aux)
{
  http_cookie_t *hc;
  time_t now;
  time(&now);

  htsmsg_t *m = htsmsg_create_list();

  hts_mutex_lock(&http_cookies_mutex);
  LIST_FOREACH(hc, &http_cookies, hc_link) {
    if(hc->hc_expire == -1 || now > hc->hc_expire)
      continue;

    htsmsg_t *c = htsmsg_create_map();
    htsmsg_add_str(c, "name", hc->hc_name);
    htsmsg_add_str(c, "path", hc->hc_path);
    htsmsg_add_str(c, "domain", hc->hc_domain);
    htsmsg_add_str(c, "value", hc->hc_value);
    htsmsg_add_s32(c, "expire", hc->hc_expire);
    htsmsg_add_msg(m, NULL, c);
  }
  hts_mutex_unlock(&http_cookies_mutex);
  htsmsg_store_save(m, "httpcookies", NULL);
  htsmsg_release(m);
}


/**
 *
 */
static void
load_cookies(void)
{
  htsmsg_t *m = htsmsg_store_load("httpcookies");
  if(m == NULL)
    return;
  htsmsg_field_t *f;
  time_t now;
  time(&now);
  HTSMSG_FOREACH(f, m) {
    htsmsg_t *o;
    if((o = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *name   = htsmsg_get_str(o, "name");
    const char *path   = htsmsg_get_str(o, "path");
    const char *domain = htsmsg_get_str(o, "domain");
    const char *value  = htsmsg_get_str(o, "value");
    time_t expire      = htsmsg_get_s32_or_default(o, "expire", -1);

    if(name == NULL || path == NULL || domain == NULL ||
       value == NULL || expire < now)
      continue;

    http_cookie_t *hc = calloc(1, sizeof(http_cookie_t));
    LIST_INSERT_HEAD(&http_cookies, hc, hc_link);
    hc->hc_name   = strdup(name);
    hc->hc_path   = strdup(path);
    hc->hc_domain = strdup(domain);
    hc->hc_value  = strdup(value);
    hc->hc_expire = expire;
  }
  htsmsg_release(m);
}


/**
 *
 */
static void
http_cookie_set(char *cookie, http_file_t *hf)
{
  static callout_t cookie_persist_timer;

  const char *req_host = hf->hf_connection->hc_hostname;
  const char *req_path = hf->hf_path;
  char *argv[20];
  int argc, i;
  const char *domain = req_host;
  const char *path = req_path;
  char *name;
  char *value;
  time_t expire = -1;
  http_cookie_t *hc;

  argc = http_tokenize(cookie, argv, 20, ';');
  if(argc == 0) {
    HF_TRACE(hf, "Ignoring malformed cookie");
    return;
  }

  name = argv[0];
  value = strchr(name, '=');
  if(value == NULL) {
    HF_TRACE(hf, "Ignoring malformed cookie, no value");
    return;
  }
  *value = 0;
  value++;

  for(i = 1; i < argc; i++) {
    if(!strncasecmp(argv[i], "domain=", strlen("domain=")))
      domain = argv[i] + strlen("domain=");
    
    if(!strncasecmp(argv[i], "path=", strlen("path=")))
      path = argv[i] + strlen("path=");
    
    if(!strncasecmp(argv[i], "expires=", strlen("expires="))) {
      http_ctime(&expire, argv[i] + strlen("expires="));
    }
  }

  int r;
  if((r = validate_cookie(req_host, req_path, domain, path))) {
    HF_TRACE(hf, "Rejected cookie name=%s path=%s domain=%s value=%s error=%d",
             name, path, domain, value, r);
    return;
  }

  HF_TRACE(hf, "Updating cookie name=%s path=%s domain=%s value=%s expires in %d seconds",
           name, path, domain, value,
           expire > 0 ? ((int)expire - time(NULL)) : -1);

  hts_mutex_lock(&http_cookies_mutex);

  LIST_FOREACH(hc, &http_cookies, hc_link) {
    if(!strcmp(hc->hc_name, name) &&
       !strcmp(hc->hc_path, path) &&
       !strcmp(hc->hc_domain, domain))
      break;
  }

  if(hc == NULL) {
    hc = calloc(1, sizeof(http_cookie_t));
    LIST_INSERT_HEAD(&http_cookies, hc, hc_link);
    hc->hc_name = strdup(name);
    hc->hc_path = strdup(path);
    hc->hc_domain = strdup(domain);
  }

  if(expire != -1)
    callout_arm(&cookie_persist_timer, cookie_persist, NULL, 5);

  mystrset(&hc->hc_value, value);
  hc->hc_expire = expire;

  hts_mutex_unlock(&http_cookies_mutex);
}


/**
 *
 */
static void
http_cookie_append(const char *req_host, const char *req_path,
		   struct http_header_list *headers,
		   struct http_header_list *extra_cookies)
{
  http_header_t *hh;
  http_cookie_t *hc;
  htsbuf_queue_t hq;
  const char *s = "";
  time_t now;
  time(&now);

  htsbuf_queue_init(&hq, 0);

  hts_mutex_lock(&http_cookies_mutex);
  LIST_FOREACH(hc, &http_cookies, hc_link) {
    if(hc->hc_expire != -1 && now > hc->hc_expire)
      continue;

    if(validate_cookie(req_host, req_path, hc->hc_domain, hc->hc_path))
      continue;

    if(http_header_get(extra_cookies, hc->hc_name))
      continue;

    // Skip overridden headers
    LIST_FOREACH(hh, extra_cookies, hh_link) {
      if(!strcmp(hh->hh_key, hc->hc_name))
        break;
    }
    if(hh != NULL)
      continue;

    htsbuf_append(&hq, s, strlen(s));
    htsbuf_append(&hq, hc->hc_name, strlen(hc->hc_name));
    htsbuf_append(&hq, "=", 1);
    htsbuf_append(&hq, hc->hc_value, strlen(hc->hc_value));
    s="; ";
  }

  hts_mutex_unlock(&http_cookies_mutex);

  LIST_FOREACH(hh, extra_cookies, hh_link) {
    if(hh->hh_value == NULL)
      continue;
    htsbuf_append(&hq, s, strlen(s));
    htsbuf_append(&hq, hh->hh_key, strlen(hh->hh_key));
    htsbuf_append(&hq, "=", 1);
    htsbuf_append(&hq, hh->hh_value, strlen(hh->hh_value));
    s="; ";
  }

  if(hq.hq_size == 0)
    return;

  http_header_add_alloced(headers, "Cookie", htsbuf_to_string(&hq), 0);
}




static void http_detach(http_file_t *hf, int reusable, const char *reason);



/**
 *
 */
LIST_HEAD(http_auth_cache_list, http_auth_cache);
static struct http_auth_cache_list http_auth_caches;
static hts_mutex_t http_auth_caches_mutex;

/**
 *
 */
typedef struct http_auth_cache {

  LIST_ENTRY(http_auth_cache) hac_link;

  char *hac_hostname;
  int hac_port;
  char *hac_credentials;

} http_auth_cache_t;


static void
http_auth_cache_set(http_file_t *hf)
{
  http_auth_cache_t *hac;
  const char *hostname = hf->hf_connection->hc_hostname;
  int port = hf->hf_connection->hc_port;
  const char *credentials = hf->hf_auth;

  hts_mutex_lock(&http_auth_caches_mutex);

  LIST_FOREACH(hac, &http_auth_caches, hac_link) {
    if(!strcmp(hostname, hac->hac_hostname) && port == hac->hac_port)
      break;
  }

  if(credentials == NULL) {
    if(hac != NULL) {

      free(hac->hac_hostname);
      free(hac->hac_credentials);
      LIST_REMOVE(hac, hac_link);
      free(hac);
    }
  } else {

    if(hac == NULL) {
      hac = calloc(1, sizeof(http_auth_cache_t));
      hac->hac_hostname = strdup(hostname);
      hac->hac_port = port;
      
      LIST_INSERT_HEAD(&http_auth_caches, hac, hac_link);
    }
    mystrset(&hac->hac_credentials, credentials);
  }
  hts_mutex_unlock(&http_auth_caches_mutex);
}


/**
 *
 */
void
http_request_inspector_register(http_request_inspector_t *hri)
{
  LIST_INSERT_HEAD(&http_request_inspectors, hri, link);
}

/**
 *
 */
int
http_client_rawauth(http_request_inspection_t *hri, const char *str)
{
  http_header_add(hri->hri_headers, "Authorization", str, 0);
  return 0;
}


/**
 *
 */
void
http_client_set_header(http_request_inspection_t *hri, const char *key,
		       const char *value)
{
  http_header_add(hri->hri_headers, key, value, 0);
}


/**
 *
 */
void
http_client_set_cookie(http_request_inspection_t *hri, const char *key,
		       const char *value)
{
  http_header_add(hri->hri_cookies, key, value, 0);
}


/**
 *
 */
void
http_client_fail_req(http_request_inspection_t *hri, const char *reason)
{
  snprintf(hri->hri_errbuf, hri->hri_errlen, "%s", reason);
  hri->hri_force_fail = 1;
}


/**
 *
 */
static void
http_send_verb(htsbuf_queue_t *q, http_file_t *hf, const char *method)
{
  char *r, *path = hf->hf_path;
  if(strchr(path, ' ')) {
    path = strdup(hf->hf_path);
    for(r = path; *r; r++) {
      if(*r == ' ')
	*r = '+';
    }
  }

  htsbuf_qprintf(q, "%s %s HTTP/1.%d\r\n", method, 
		 path, hf->hf_version);

  if(path != hf->hf_path)
    free(path);
}

/**
 *
 */
static void
http_headers_init(struct http_header_list *l, const http_file_t *hf)
{
  char str[200];
  const http_connection_t *hc = hf->hf_connection;

  LIST_INIT(l);

  if(hc->hc_port != 80 && hc->hc_port != 443) {
    snprintf(str, sizeof(str), "%s:%d", hc->hc_hostname, hc->hc_port);
    http_header_add(l, "Host", str, 0);
  } else {
    http_header_add(l, "Host", hf->hf_connection->hc_hostname, 0);
  }
  if(hf->hf_req_compression)
    http_header_add(l, "Accept-Encoding", "gzip", 0);
  else
    http_header_add(l, "Accept-Encoding", "identity", 0);

  http_header_add(l, "Accept", "*/*", 0);
  
  http_header_add(l, "Connection",
		  hf->hf_want_close ? "close" : "keep-alive", 0);
  snprintf(str, sizeof(str), APPNAMEUSER" %s %s",
	   arch_get_system_type(), htsversion);
  http_header_add(l, "User-Agent", str, 0);
}


/**
 *
 */
static void
http_headers_send(htsbuf_queue_t *q, struct http_header_list *def,
		  const struct http_header_list *user1)
{
  http_header_t *hh;

  if(user1 != NULL)
    http_header_merge(def, user1);

  LIST_FOREACH(hh, def, hh_link)
    htsbuf_qprintf(q, "%s: %s\r\n", hh->hh_key, hh->hh_value);

  http_headers_free(def);
  htsbuf_qprintf(q, "\r\n");
}


/**
 *
 */
static int attribute_unused_result
http_request_inspect(struct http_header_list *headers,
                     struct http_header_list *cookies,
                     http_file_t *hf,
                     const char *method, const char **parameters,
                     char *errbuf, size_t errlen)
{
  http_auth_cache_t *hac;
  const char *hostname = hf->hf_connection->hc_hostname;
  int port = hf->hf_connection->hc_port;

  http_request_inspector_t *x;
  http_request_inspection_t hri;

  hri.hri_method = method;
  hri.hri_parameters = parameters;
  hri.hri_headers = headers;
  hri.hri_cookies = cookies;
  hri.hri_hf = hf;
  hri.hri_errbuf = errbuf;
  hri.hri_errlen = errlen;
  hri.hri_force_fail = 0;

  LIST_FOREACH(x, &http_request_inspectors, link) {
    if(!x->check(hf->hf_url, &hri)) {
      if(hri.hri_force_fail) {
        http_headers_free(cookies);
        return 1;
      }
      return 0;
    }
  }


  if(hf->hf_auth != NULL) {
    http_header_add(headers, "Authorization", hf->hf_auth, 0);
    return 0;
  }

  hts_mutex_lock(&http_auth_caches_mutex);
  LIST_FOREACH(hac, &http_auth_caches, hac_link) {
    if(!strcmp(hostname, hac->hac_hostname) && port == hac->hac_port) {
      hf->hf_auth = strdup(hac->hac_credentials);
      http_header_add(headers, "Authorization", hac->hac_credentials, 0);
      break;
    }
  }
  hts_mutex_unlock(&http_auth_caches_mutex);
  return 0;
}


/**
 *
 */
static buf_t *
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
      if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader)) < 0)
	break;
 
      csize = strtol(chunkheader, NULL, 16);

      if(csize > 0) {
	buf = myreallocf(buf, s + csize + 1);
        if(buf == NULL)
          return NULL;

	if(tcp_read_data(hc->hc_tc, buf + s, csize, NULL, 0))
	  break;

	s += csize;
	buf[s] = 0;
      }

      if(tcp_read_data(hc->hc_tc, chunkheader, 2, NULL, 0))
	break;

      if(csize == 0) {
	hf->hf_rsize = 0;
	return buf_create_from_malloced(s, buf);
      }
    }
    free(buf);
    hf->hf_chunked_transfer = 0;
    return NULL;
  }

  s = hf->hf_rsize;
  buf = mymalloc(s + 1);
  if(buf == NULL)
    return NULL;
  buf[s] = 0;
  
  if(tcp_read_data(hc->hc_tc, buf, s, NULL, 0)) {
    free(buf);
    return NULL;
  }
  hf->hf_rsize = 0;
  return buf_create_from_malloced(s, buf);
}


/**
 *
 */
static int
http_drain_content(http_file_t *hf)
{
  buf_t *buf;

  if(hf->hf_chunked_transfer == 0 && hf->hf_rsize < 0) {
    hf->hf_rsize = 0;
    return 0;
  }

  if((buf = http_read_content(hf)) == NULL)
    return -1;

  if(hf->hf_debug) {
    char subsys[64];
    snprintf(subsys, sizeof(subsys), "HTTP-%d", hf->hf_id);
    hexdump(subsys, buf_cstr(buf), buf_size(buf));
  }

  buf_release(buf);

  if(hf->hf_connection_mode == CONNECTION_MODE_CLOSE)
    http_detach(hf, 0, "Connection-mode = close");

  return 0;
}


/**
 *
 */
static int
hf_drain_bytes(http_file_t *hf, int64_t bytes)
{
  char chunkheader[100];
  http_connection_t *hc = hf->hf_connection;

  if(!hf->hf_chunked_transfer) {
    hf->hf_rsize -= bytes;
    return tcp_read_data(hc->hc_tc, NULL, bytes, NULL, NULL);
  }

  while(bytes > 0) {
    if(hf->hf_chunk_size == 0) {
      if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader)) < 0) {
	return -1;
      }
      hf->hf_chunk_size = strtol(chunkheader, NULL, 16);
    }

    size_t read_size = MIN(bytes, hf->hf_chunk_size);
    if(read_size > 0)
      if(tcp_read_data(hc->hc_tc, NULL, read_size, NULL, NULL))
	return -1;

    bytes -= read_size;
    hf->hf_chunk_size -= read_size;
    hf->hf_rsize -= read_size;

    if(hf->hf_chunk_size == 0) {
      if(tcp_read_data(hc->hc_tc, chunkheader, 2, NULL, NULL))
	return -1;
    }
  }
  return 0;
}



/*
 * Split a string in components delimited by 'delimiter'
 */
static int
isdelimited(char c, int delimiter)
{
  return delimiter == -1 ? c < 33 : c == delimiter;
}


static int
http_tokenize(char *buf, char **vec, int vecsize, int delimiter)
{
  int n = 0;

  while(1) {
    while(*buf && (isdelimited(*buf, delimiter) || *buf == 32))
      buf++;
    if(*buf == 0)
      break;
    vec[n++] = buf;
    if(n == vecsize)
      break;
    while(*buf && !isdelimited(*buf, delimiter))
      buf++;
    if(*buf == 0)
      break;
    *buf = 0;
    buf++;
  }
  return n;
}


/**
 * Work around broken HTTP servers that send Location: without
 * proper URL escaping. We only catch spaces atm.
 */
static void
hf_set_location(http_file_t *hf, const char *str)
{
  free(hf->hf_location);

  size_t len = url_escape(NULL, 0, str, URL_ESCAPE_SPACE_ONLY);
  char *r = malloc(len);
  url_escape(r, len, str, URL_ESCAPE_SPACE_ONLY);
  hf->hf_location = r;
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

  hf->hf_content_encoding = HTTP_CE_IDENTITY;
  hf->hf_connection_mode = CONNECTION_MODE_PERSISTENT;
  hf->hf_rsize = -1;
  hf->hf_chunked_transfer = 0;
  free(hf->hf_content_type);
  hf->hf_content_type = NULL;
  hf->hf_max_age = 30;

  int first_line = 1;

  for(li = 0; ;li++) {
    if(tcp_read_line(hc->hc_tc, hf->hf_line, sizeof(hf->hf_line)) < 0)
      return -1;

    if(first_line) {
      HF_TRACE(hf, "%s: Response:", hf->hf_url);
      first_line = 0;
    }

    HF_TRACE(hf, "< %s", hf->hf_line);

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

    if((c = strchr(hf->hf_line, ':')) == NULL)
      continue;

    if(http_tokenize(hf->hf_line, argv, 2, ':') != 2)
      continue;
    *c = 0;

    if(headers != NULL)
      http_header_add(headers, argv[0], argv[1], 1);

    if(!strcasecmp(argv[0], "Transfer-Encoding")) {

      if(!strcasecmp(argv[1], "chunked")) {
	hf->hf_chunked_transfer = 1;
	hf->hf_chunk_size = 0;
      }
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
      hf_set_location(hf, argv[1]);
      continue;
    }

    if(!strcasecmp(argv[0], "Server")) {
      // CDN network typically never change the size of a file
      if(!strcasecmp(argv[1], "AkamaiGHost"))
        hf->hf_filesize_is_final = 1;

      continue;
    }

    if(!strcasecmp(argv[0], "Keep-Alive")) {
      const char *x = strstr(argv[1], "timeout=");
      if(x != NULL)
	hf->hf_max_age = atoi(x + strlen("timeout="));
      continue;
    }

    if(!strcasecmp(argv[0], "Content-Encoding")) {
      if(!strcasecmp(argv[1], "gzip"))
	hf->hf_content_encoding = HTTP_CE_GZIP;
      else
	hf->hf_content_encoding = HTTP_CE_IDENTITY;
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

    if(code == 206 && !strcasecmp(argv[0], "Content-Range") &&
       hf->hf_filesize == -1) {

      if(!strncasecmp(argv[1], "bytes", 5)) {
	const char *slash = strchr(argv[1], '/');
	if(slash != NULL) {
	  slash++;
	  hf->hf_filesize = strtoll(slash, NULL, 0);
	}
      }
    }

    if(!strcasecmp(argv[0], "connection")) {

      if(!strcasecmp(argv[1], "close"))
	hf->hf_connection_mode = CONNECTION_MODE_CLOSE;
    }

    if(!strcasecmp(argv[0], "Set-Cookie"))
      http_cookie_set(argv[1], hf);
  }

  if(code >= 200 && code < 400) {
    hf->hf_auth_failed = 0;
    http_auth_cache_set(hf);
  }

  return code;
}




/**
 *
 */
static void
http_detach(http_file_t *hf, int reusable, const char *reason)
{
  if(hf->hf_connection == NULL)
    return;

  if(reusable && !gconf.disable_http_reuse &&
     !cancellable_is_cancelled(hf->hf_cancellable)) {
    http_connection_park(hf->hf_connection, hf->hf_debug, hf->hf_max_age, reason);
  } else {
    http_connection_destroy(hf->hf_connection, hf->hf_debug, reason);
  }
  hf->hf_connection = NULL;
}


/**
 *
 */
static int
redirect(http_file_t *hf, int *redircount, char *errbuf, size_t errlen,
	 int code, int expect_content)
{
  (*redircount)++;
  if(*redircount == 10) {
    snprintf(errbuf, errlen, "Too many redirects");
    return -1;
  }

  if(hf->hf_location == NULL) {
    snprintf(errbuf, errlen, "Redirect response without location");
    return -1;
  }

  HF_TRACE(hf, "%s: Following redirect to %s%s", hf->hf_url, hf->hf_location,
	   code == 301 ? ", (premanent)" : "");

  
  const http_connection_t *hc = hf->hf_connection;

  char *newurl =
    url_resolve_relative(hc->hc_ssl ? "https" : "http",
			 hc->hc_hostname, hc->hc_port,
			 hf->hf_path, hf->hf_location);

  if(code == 301) {
    add_premanent_redirect(hf->hf_url, newurl);
  } else {

    if(hf->hf_original_url == NULL) {
      hf->hf_original_url = hf->hf_url;
      hf->hf_url = NULL;
    }
  }
  free(hf->hf_url);
  hf->hf_url = newurl;

  free(hf->hf_location);
  hf->hf_location = NULL;
  
  if(expect_content && http_drain_content(hf))
    hf->hf_connection_mode = CONNECTION_MODE_CLOSE;

  // Location changed, must detach from connection
  // We might still be able to reuse it if hostname+port is same
  // But that's for some other code to figure out
  http_detach(hf, hf->hf_connection_mode == CONNECTION_MODE_PERSISTENT,
	      "Location changed");
  return 0;
}

/**
 *
 */
static int 
authenticate(http_file_t *hf, char *errbuf, size_t errlen, int *non_interactive,
	     int expect_content)
{
  char *username;
  char *password;
  char buf1[128];
  char buf2[128];
  int r;

  if(hf->hf_auth_failed > 0 && non_interactive) {
    *non_interactive = FAP_NEED_AUTH;
    return -1;
  }
  
  snprintf(buf1, sizeof(buf1), "%s @ %s", hf->hf_auth_realm, 
	   hf->hf_connection->hc_hostname);

  if(expect_content && http_drain_content(hf))
    hf->hf_connection_mode = CONNECTION_MODE_CLOSE;

  if(hf->hf_auth_realm == NULL) {
    snprintf(errbuf, errlen, "Authentication without realm");
    return -1;
  }

  r = keyring_lookup(buf1, &username, &password, NULL, NULL,
		     "HTTP Client", "Access denied",
		     (hf->hf_auth_failed > 0 ? KEYRING_QUERY_USER : 0) |
		     KEYRING_SHOW_REMEMBER_ME | KEYRING_REMEMBER_ME_SET);

  hf->hf_auth_failed++;

  free(hf->hf_auth);
  hf->hf_auth = NULL;

  if(r == -1) {
    /* Rejected */
    snprintf(errbuf, errlen, "Authentication rejected by user");
    return -1;
  }

  if(r == 0) {
    HF_TRACE(hf, "%s: Authenticating with %s %s",
	     hf->hf_url, username, password);

    /* Got auth credentials */  
    snprintf(buf1, sizeof(buf1), "%s:%s", username, password);
#if ENABLE_LIBAV
    av_base64_encode(buf2, sizeof(buf2), (uint8_t *)buf1, strlen(buf1));
#else
    abort();
#endif

    snprintf(buf1, sizeof(buf1), "Basic %s", buf2);
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
static void
http_set_read_timeout(fa_handle_t *fh, int ms)
{
  http_file_t *hf = (http_file_t *)fh;

  hf->hf_read_timeout = ms;
  hf->hf_connect_timeout = ms;

  if(hf->hf_connection != NULL)
    tcp_set_read_timeout(hf->hf_connection->hc_tc, ms);
}


/**
 *
 */
static int
http_connect(http_file_t *hf, char *errbuf, int errlen, int allow_reuse)
{
  char hostname[HOSTNAME_MAX];
  char proto[16];
  int port, ssl;
  http_redirect_t *hr;
  const char *url;

  hf->hf_rsize = 0;

  if(hf->hf_connection != NULL)
    http_detach(hf, 0, "Reconnect");

  url = hf->hf_url;

  hts_mutex_lock(&http_redirects_mutex);

  LIST_FOREACH(hr, &http_redirects, hr_link)
    if(!strcmp(url, hr->hr_from)) {
      url = hr->hr_to;
      break;
    }

  if(hf->hf_ret_location != NULL) {
    free(*hf->hf_ret_location);
    *hf->hf_ret_location = strdup(url);
  }

  url_split(proto, sizeof(proto), hf->hf_authurl, sizeof(hf->hf_authurl), 
	    hostname, sizeof(hostname), &port,
	    hf->hf_path, sizeof(hf->hf_path), 
	    url);

  hts_mutex_unlock(&http_redirects_mutex);

  ssl = !strcmp(proto, "https") || !strcmp(proto, "webdavs");
  if(port < 0)
    port = ssl ? 443 : 80;

  /* empty path, default to "/" */ 
  if(!hf->hf_path[0])
    strcpy(hf->hf_path, "/");

  const int timeout = hf->hf_connect_timeout ?: 30000;

  hf->hf_connection = http_connection_get(hostname, port, ssl, errbuf, errlen,
					  hf->hf_debug, timeout,
                                          hf->hf_cancellable, allow_reuse);

  if(hf->hf_read_timeout != 0 && hf->hf_connection != NULL)
    tcp_set_read_timeout(hf->hf_connection->hc_tc, hf->hf_read_timeout);

  return hf->hf_connection ? 0 : -1;
}


/**
 *
 */
static int
http_open0(http_file_t *hf, int probe, char *errbuf, int errlen,
	   int *non_interactive)
{
  int code;
  htsbuf_queue_t q;
  int redircount = 0;
  struct http_header_list headers;
  struct http_header_list cookies;

  reconnect:

  hf->hf_filesize = -1;

  if(http_connect(hf, errbuf, errlen, 1))
    return -1;

  if(!probe && hf->hf_filesize != -1)
    return 0;

  htsbuf_queue_init(&q, 0);

 again:

  http_headers_init(&headers, hf);
  LIST_INIT(&cookies);

  if(hf->hf_streaming) {
    http_send_verb(&q, hf, "GET");
    tcp_huge_buffer(hf->hf_connection->hc_tc);
  } else {
    http_send_verb(&q, hf, "GET");
    htsbuf_qprintf(&q, "Range: bytes=0-4095\r\n");
  }

  if(http_request_inspect(&headers, &cookies, hf, "GET", NULL, errbuf, errlen))
    return -1;

  http_cookie_append(hf->hf_connection->hc_hostname, hf->hf_path, &headers,
		     &cookies);
  http_headers_free(&cookies);

  HF_TRACE(hf, "Sending open request for %s (cid=%d)",
           hf->hf_url, hf->hf_connection->hc_id);

  http_headers_send(&q, &headers, hf->hf_user_request_headers);


  if(hf->hf_debug)
    trace_request(&q, hf);

  tcp_write_queue(hf->hf_connection->hc_tc, &q);

  code = http_read_response(hf, hf->hf_user_response_headers);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0, "Read error on reused connection, retrying");
    goto reconnect;
  }

  hf->hf_status_code = code;

  switch(code) {
  case 200:
    if(hf->hf_filesize == -1)
      hf->hf_rsize = INT64_MAX;

    hf->hf_no_ranges = 1;

    HF_TRACE(hf, "Opened in streaming mode");
    return 0;

  case 206:
    if(hf->hf_filesize == -1)
      HF_TRACE(hf, "%s: No known filesize, seeking may be slower", hf->hf_url);
    return 0;

  case 301:
  case 302:
  case 303:
  case 307:
    if(redirect(hf, &redircount, errbuf, errlen, code, 1))
      return -1;

    if(hf->hf_connection_mode == CONNECTION_MODE_CLOSE)
      http_detach(hf, 0, "Redirect");

    goto reconnect;


  case 401:
    if(authenticate(hf, errbuf, errlen, non_interactive, 1))
      return -1;

    if(hf->hf_connection_mode == CONNECTION_MODE_CLOSE) {
      http_detach(hf, 0, "Connection-mode = close");
      goto reconnect;
    }

    goto again;

  case 405:
    snprintf(errbuf, errlen, "Unsupported method");
    return -1;

  case -1:
    snprintf(errbuf, errlen, "Server reset connection");
    return -1;

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
	      hf->hf_connection_mode == CONNECTION_MODE_PERSISTENT,
	      "Request destroyed");
  free(hf->hf_url);
  free(hf->hf_original_url);
  free(hf->hf_auth);
  free(hf->hf_auth_realm);
  free(hf->hf_location);
  free(hf->hf_content_type);
  prop_ref_dec(hf->hf_stats_speed);
  cancellable_release(hf->hf_cancellable);
  free(hf);
}


/**
 * Open file
 */
static fa_handle_t *
http_open_ex(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
             int *non_interactive, int flags, struct fa_open_extra *foe)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  hf->hf_id = atomic_add_and_fetch(&http_file_tally, 1);
  hf->hf_version = 1;
  hf->hf_url = strdup(url);
  if(!(flags & FA_NO_DEBUG))
    hf->hf_debug = !!(flags & FA_DEBUG) || gconf.enable_http_debug;
  hf->hf_streaming = !!(flags & FA_STREAMING);
  hf->hf_no_retries = !!(flags & FA_NO_RETRIES);
  if(foe != NULL) {
    if(foe->foe_stats != NULL) {
      hf->hf_stats_speed = prop_create_r(foe->foe_stats, "bitrate");
      prop_set(foe->foe_stats, "bitrateValid", PROP_SET_INT, 1);
    }
    hf->hf_user_request_headers  = foe->foe_request_headers;
    hf->hf_user_response_headers = foe->foe_response_headers;
    hf->hf_cancellable = cancellable_retain(foe->foe_cancellable);
    hf->hf_connect_timeout = foe->foe_open_timeout;
  }

  if(!http_open0(hf, 1, errbuf, errlen, non_interactive)) {
    hf->h.fh_proto = fap;
    return &hf->h;
  }
  if(foe != NULL)
    foe->foe_protocol_error = hf->hf_status_code;

  http_destroy(hf);
  return NULL;
}

static fa_handle_t *
http_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	  int flags, struct fa_open_extra *foe)
{
  return http_open_ex(fap, url, errbuf, errlen, NULL, flags, foe);
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
 * If we followed a redirect and it was temporary we need to
 * reset the URL to the original URL. We might be reading on
 * a temporary generated URL (see issue #2591)
 */
static void
http_reset_url(http_file_t *hf)
{
  if(hf->hf_original_url != NULL) {
    mystrset(&hf->hf_url, hf->hf_original_url);
    HF_TRACE(hf, "read() reverting to original URL %s", hf->hf_url);
  }
}


/**
 * Read from file
 */
static int
http_read_i(http_file_t *hf, void *buf, const size_t size)
{
  htsbuf_queue_t q;
  int i, code;
  http_connection_t *hc;
  char chunkheader[100];
  size_t totsize = 0; // Total data read
  size_t read_size;   // Amount of bytes to read in one round
  struct http_header_list headers, cookies;
  char errbuf[512];

  int redircount = 0;

  if(size == 0)
    return 0;

  /* Max 5 retries */
  for(i = 0; i < 5; i++) {
    /* If not connected, try to (re-)connect */
  retry:

    if(hf->hf_filesize != -1 && hf->hf_pos >= hf->hf_filesize)
      return totsize; // Reading outside known filesize

    if((hc = hf->hf_connection) == NULL) {
      if(hf->hf_no_retries)
        return -1;

      if(http_connect(hf, NULL, 0, 1))
	return -1;
      hc = hf->hf_connection;
    }

    if(hf->hf_rsize > 0) {
      /*
       * We have pending data input on the socket,
       * However, we can not read more data than is available,
       */
      read_size = MIN(hf->hf_rsize, size - totsize);

    } else {

      HF_TRACE(hf, "read() needs to send a new GET request (cid=%d)",
               hc->hc_id);
      read_size = size - totsize;

      /* Must send a new request */

      htsbuf_queue_init(&q, 0);

      http_send_verb(&q, hf, "GET");

      http_headers_init(&headers, hf);
      LIST_INIT(&cookies);
      if(http_request_inspect(&headers, &cookies, hf, "GET", NULL,
                              errbuf, sizeof(errbuf))) {
        http_detach(hf, 0, errbuf);
        return -1;
      }

      char range[100];

      if(hf->hf_filesize == -1 || hf->hf_no_ranges) {
	range[0] = 0;

      } else if(hf->hf_streaming || hf->hf_consecutive_read > STREAMING_LIMIT) {
	if(!hf->hf_streaming)
	  HF_TRACE(hf, "%s: switching to streaming mode", hf->hf_url);
	snprintf(range, sizeof(range), "bytes=%"PRId64"-", hf->hf_pos);
	tcp_huge_buffer(hf->hf_connection->hc_tc);

      } else {

	int64_t end = hf->hf_pos + read_size;
	if(end > hf->hf_filesize)
	  end = hf->hf_filesize;

	snprintf(range, sizeof(range), "bytes=%"PRId64"-%"PRId64,
		 hf->hf_pos, end - 1);
      }

      if(range[0])
	htsbuf_qprintf(&q, "Range: %s\r\n", range);

      http_cookie_append(hc->hc_hostname, hf->hf_path, &headers, &cookies);
      http_headers_free(&cookies);
      HF_TRACE(hf, "Read issuing new request for %s (cid=%d)",
               hf->hf_url, hf->hf_connection->hc_id);
      http_headers_send(&q, &headers, hf->hf_user_request_headers);
      if(hf->hf_debug)
        trace_request(&q, hf);

      tcp_write_queue(hc->hc_tc, &q);

      code = http_read_response(hf, hf->hf_user_response_headers);
      if(code == -1 && hf->hf_connection->hc_reused) {
	http_detach(hf, 0, "Read error on reused connection, retrying");
        http_reset_url(hf);
	goto retry;
      }

      switch(code) {
      case 206:
	// Range transfer OK
	break;

      case 301:
      case 302:
      case 303:
      case 307:
        if(redirect(hf, &redircount, NULL, 0, code, 1))
          return -1;

        if(hf->hf_connection_mode == CONNECTION_MODE_CLOSE)
          http_detach(hf, 0, "Redirect");

        continue;

      case 200:
	if(range[0])
	  hf->hf_no_ranges = 1;

	if(hf->hf_rsize == -1)
	  hf->hf_rsize = INT64_MAX;

	if(hf->hf_pos != 0) {
	  HF_TRACE(hf, "Skipping by reading %"PRId64" bytes, rsize=%"PRId64,
                hf->hf_pos, hf->hf_rsize);

	  if(hf_drain_bytes(hf, hf->hf_pos)) {
	    http_detach(hf, 0, "Read error during drain");
	    continue;
	  }

          HF_TRACE(hf, "rsize is now = %"PRId64, hf->hf_rsize);
	}


	break;

      case 416:
	hf->hf_no_ranges = 1;
	http_detach(hf, 0, "Requested Range Not Satisfiable");
        http_reset_url(hf);
	continue;

      default:
	HF_TRACE(hf, "Read error (%d) [%s] filesize %lld -- retrying", code,
                 range, hf->hf_filesize);
	http_detach(hf, 0, "Read error");
        http_reset_url(hf);
        continue;
      }

      if(hf->hf_rsize < read_size)
	read_size = hf->hf_rsize;
    }

    if(hf->hf_filesize == -1 && hf->hf_streaming &&
       !hf->hf_chunked_transfer) {
      // Read until EOF

      while(read_size) {
        int r = tcp_read_to_eof(hc->hc_tc, buf + totsize, read_size,
                                NULL, NULL);
        if(r < 0)
          return totsize;

        read_size               -= r;
        hf->hf_pos              += r;
        totsize                 += r;
        hf->hf_consecutive_read += r;
      }
      return totsize;
    }


    if(hf->hf_chunked_transfer) {
      if(hf->hf_chunk_size == 0) {
	if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader)) < 0)
	  goto bad;
	hf->hf_chunk_size = strtol(chunkheader, NULL, 16);
      }

      read_size = MIN(size - totsize, hf->hf_chunk_size);
    }

    if(read_size > 0) {
      assert(totsize + read_size <= size);
      if(tcp_read_data(hc->hc_tc, buf + totsize, read_size, NULL, NULL)) {
        // Fail, so disconnect
        http_detach(hf, 0, "Read error during fa_read()");
        http_reset_url(hf);
        // But we can retry a couple of times
        continue;
      }

      hf->hf_pos              += read_size;
      hf->hf_rsize            -= read_size;
      totsize                 += read_size;
      hf->hf_consecutive_read += read_size;

    } else {
      hf->hf_rsize = 0;
    }

    if(hf->hf_chunked_transfer) {

      hf->hf_chunk_size -= read_size;

      if(hf->hf_chunk_size == 0) {
	if(tcp_read_data(hc->hc_tc, chunkheader, 2, NULL, NULL))
	  goto bad;
      }
    }

    if(read_size == 0)
      return totsize;

    if(hf->hf_rsize == 0 && hf->hf_connection_mode == CONNECTION_MODE_CLOSE)
      http_detach(hf, 0, "Connection-mode = close");

    if(totsize != size) {
      i--;
      continue;
    }
    return totsize;
  }
 bad:
  http_detach(hf, 0, "Error during fa_read()");
  return -1;
}


static int
http_read(fa_handle_t *handle, void *buf, const size_t size)
{
  http_file_t *hf = (http_file_t *)handle;

  if(hf->hf_stats_speed == NULL)
    return http_read_i(hf, buf, size);

  int r = http_read_i(hf, buf, size);
  hf->hf_bytes_downloaded += r;

  time_t now = time(NULL);
  average_fill(&hf->hf_download_rate, now, hf->hf_bytes_downloaded);

  int rate = average_read(&hf->hf_download_rate, now) / 125;
  prop_set_int(hf->hf_stats_speed, rate);
  return r;
}

/**
 * Seek in file
 */
static int64_t
http_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  http_file_t *hf = (http_file_t *)handle;
  http_connection_t *hc = hf->hf_connection;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = hf->hf_pos + pos;
    break;

  case SEEK_END:
    if(hf->hf_filesize == -1) {
      HF_TRACE(hf, "%s: Refusing to seek to END on non-seekable file",
	       hf->hf_url);
      return -1;
    }
    np = hf->hf_filesize + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  if(hf->hf_filesize_is_final && hf->hf_filesize != -1 && np > hf->hf_filesize)
    return -1;

  if(hf->hf_pos != np && hc != NULL) {
    hf->hf_consecutive_read = 0;

    if(hf->hf_rsize != 0) {
      // We've data pending on socket
      int64_t d = np - hf->hf_pos;
      // We allow seek by reading if delta offset is small enough

      if(d > 0 && (d < SEEK_BY_READ_THRES || (hf->hf_no_ranges && !lazy)) &&
	 d < hf->hf_rsize) {

	if(!hf_drain_bytes(hf, d)) {
	  hf->hf_pos = np;
	  return np;
	}
        http_detach(hf, 0, "Disconnected while draining");
        hf->hf_pos = np;
        return np;
      }

      if(lazy && hf->hf_no_ranges)
        return -1;

      // Still got stale data on the socket, disconnect
      http_detach(hf, 0, "Seeking during streaming");
    }
    if(lazy && hf->hf_no_ranges)
      return -1;
  }
  hf->hf_pos = np;

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
http_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	  char *errbuf, size_t errlen, int non_interactive)
{
  fa_handle_t *handle;
  http_file_t *hf;
  int statcode = -1;

  if((handle = http_open_ex(fap, url, errbuf, errlen,
			    non_interactive ? &statcode : NULL, 0,
			    NULL)) == NULL)
    return statcode;
 
  memset(fs, 0, sizeof(struct fa_stat));
  hf = (http_file_t *)handle;
  
  fs->fs_type = CONTENT_FILE;
  fs->fs_size = hf->hf_filesize;
  
  http_destroy(hf);
  return 0;
}


/**
 *
 */
static buf_t *
http_load(struct fa_protocol *fap, const char *url,
	  char *errbuf, size_t errlen,
	  char **etag, time_t *mtime, int *max_age,
	  int flags, fa_load_cb_t *cb, void *opaque,
          cancellable_t *c,
          struct http_header_list *request_headers,
          struct http_header_list *response_headers,
          char **location, int *protocol_code)
{
  buf_t *b;
  int err;
  struct http_header_list headers_in;
  struct http_header_list headers_out;
  const char *s, *s2;

  LIST_INIT(&headers_in);
  LIST_INIT(&headers_out);

  if(request_headers == NULL)
    request_headers = &headers_in;

  if(response_headers == NULL)
    response_headers = &headers_out;

  if(mtime != NULL && *mtime) {
    char txt[40];
    http_asctime(*mtime, txt, sizeof(txt));
    http_header_add(request_headers, "If-Modified-Since", txt, 0);
  }

  if(etag != NULL && *etag != NULL) {
    http_header_add(request_headers, "If-None-Match", *etag, 0);
  }

  err = http_req(url,
                 HTTP_RESULT_PTR(&b),
                 HTTP_ERRBUF(errbuf, errlen),
                 HTTP_FLAGS(flags),
                 HTTP_RESPONSE_HEADERS(response_headers),
                 HTTP_REQUEST_HEADERS(request_headers),
                 HTTP_PROGRESS_CALLBACK(cb, opaque),
                 HTTP_CANCELLABLE(c),
                 HTTP_LOCATION(location),
                 HTTP_RESPONSE_CODE(protocol_code),
                 NULL);

  if(err == -1) {
    b = NULL;
    goto done;
  }

  if(err == 304) {
    b = NOT_MODIFIED;
    goto done;
  }

  s = http_header_get(response_headers, "content-type");
  if(s != NULL && b != NULL)
    b->b_content_type = rstr_alloc(s);

  if(mtime != NULL) {
    *mtime = 0;
    if((s = http_header_get(response_headers, "last-modified")) != NULL) {
      http_ctime(mtime, s);
    }
  }

  if(etag != NULL) {
    free(*etag);
    if((s = http_header_get(response_headers, "etag")) != NULL) {
      *etag = strdup(s);
    } else {
      *etag = NULL;
    }
  }

  if(max_age != NULL) {
    if((s  = http_header_get(response_headers, "date")) != NULL && 
       (s2 = http_header_get(response_headers, "expires")) != NULL) {
      time_t expires, sdate;
      if(!http_ctime(&sdate, s) && !http_ctime(&expires, s2))
	*max_age = expires - sdate;
    }

    if((s = http_header_get(response_headers, "cache-control")) != NULL) {
      if((s2 = strstr(s, "max-age=")) != NULL) {
	*max_age = atoi(s2 + strlen("max-age="));
      }
      
      if(strstr(s, "no-cache") || strstr(s, "no-store")) {
	*max_age = 0;
      }
    }
  }

 done:
  if(request_headers == &headers_in)
    http_headers_free(&headers_in);

  if(response_headers == &headers_out)
    http_headers_free(&headers_out);
  return b;
}


/**
 *
 */
static void
http_get_last_component(struct fa_protocol *fap, const char *url,
			char *dst, size_t dstlen)
{
  int e, b;

  for(e = 0; url[e] != 0 && url[e] != '?'; e++);
  if(e > 0 && url[e-1] == '/')
    e--;
  if(e > 0 && url[e-1] == '|')
    e--;

  if(e == 0) {
    *dst = 0;
    return;
  }

  b = e;
  while(b > 0) {
    b--;
    if(url[b] == '/') {
      b++;
      break;
    }
  }

  if(dstlen > e - b + 1)
    dstlen = e - b + 1;
  memcpy(dst, url + b, dstlen);
  dst[dstlen - 1] = 0;

  url_deescape(dst);
}


/**
 *
 */
static int
http_no_parking(fa_handle_t *fh)
{
  http_file_t *hf = (http_file_t *)fh;
  return hf->hf_connection_mode == CONNECTION_MODE_CLOSE;
}


/**
 *
 */
static void
http_init(void)
{
  TAILQ_INIT(&http_connections);
  hts_mutex_init(&http_connections_mutex);
  hts_mutex_init(&http_redirects_mutex);
  hts_mutex_init(&http_cookies_mutex);
  hts_mutex_init(&http_auth_caches_mutex);
  load_cookies();
}

/**
 *
 */
static fa_protocol_t fa_protocol_http = {
  .fap_init  = http_init,
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_name  = "http",
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = http_stat,
  .fap_load = http_load,
  .fap_get_last_component = http_get_last_component,
  .fap_set_read_timeout = http_set_read_timeout,
  .fap_no_parking = http_no_parking,
};

FAP_REGISTER(http);


/**
 *
 */
static fa_protocol_t fa_protocol_https = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_name  = "https",
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = http_stat,
  .fap_load = http_load,
  .fap_get_last_component = http_get_last_component,
  .fap_set_read_timeout = http_set_read_timeout,
  .fap_no_parking = http_no_parking,
};

FAP_REGISTER(https);

/**
 * Parse WEBDAV PROPFIND results
 */
static int
parse_propfind(http_file_t *hf, htsmsg_t *xml, fa_dir_t *fd,
	       char *errbuf, size_t errlen)
{
  htsmsg_t *m, *c;
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
  url_deescape(rpath);

  if((m = htsmsg_get_map(xml, "multistatus")) == NULL) {
    snprintf(errbuf, errlen, "WEBDAV: DAV:multistatus not found in XML");
    goto err;
  }

  HTSMSG_FOREACH(f, m) {
    if(strcmp(f->hmf_name, "response"))
      continue;
    if((c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    /* Some DAV servers seams to send an empty href tag for root path "/" */
    href = htsmsg_get_str(c, "href") ?: "/";

    // Get rid of http://hostname (lighttpd includes those)
    if((q = strstr(href, "://")) != NULL)
      href = strchr(q + strlen("://"), '/') ?: "/";

    snprintf(ehref, URL_MAX, "%s", href);
    url_deescape(ehref);

    if((c = htsmsg_get_map_multi(c, "propstat", "prop", NULL)) == NULL)
      continue;

    htsmsg_t *tr = htsmsg_get_map(c, "resourcetype");
    isdir = tr != NULL ? !!htsmsg_field_find(tr, "collection") : 0;

    if(fd != NULL) {

      if(strcmp(rpath, ehref)) {
	http_connection_t *hc = hf->hf_connection;

        if(!hc->hc_ssl && hc->hc_port == 80)
          snprintf(path, URL_MAX, "webdav://%s%s",
                   hc->hc_hostname, href);
        else if(hc->hc_ssl && hc->hc_port == 443)
          snprintf(path, URL_MAX, "webdavs://%s%s",
                   hc->hc_hostname, href);
        else
          snprintf(path, URL_MAX, "%s://%s:%d%s",
                   hc->hc_ssl ? "webdavs" : "webdav", hc->hc_hostname,
		   hc->hc_port, href);

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
	  url_deescape(fname);
	  
	  fde = fa_dir_add(fd, path, fname, 
			   isdir ? CONTENT_DIR : CONTENT_FILE);

	  if(fde != NULL) {

	    fde->fde_statdone = 1;

	    if(!isdir) {
	      
	      if((d = htsmsg_get_str(c, "getcontentlength")) != NULL)
		fde->fde_stat.fs_size = strtoll(d, NULL, 10);
	      else
		fde->fde_statdone = 0;
	    }

	    if((d = htsmsg_get_str(c, "getlastmodified")) != NULL)
	      http_ctime(&fde->fde_stat.fs_mtime, d);
	  }
	}
      }
    } else {
      /* single entry stat(2) */

      snprintf(fname, URL_MAX, "%s", href);
      url_deescape(fname);

      if(!strcmp(rpath, fname)) {
	/* This is the path we asked for */

	hf->hf_isdir = isdir;

	if(!isdir) {
	  if((d = htsmsg_get_str(c, "getcontentlength")) != NULL)
	    hf->hf_filesize = strtoll(d, NULL, 10);
        }
        hf->hf_mtime = 0;
        if((d = htsmsg_get_str(c, "getlastmodified")) != NULL)
          http_ctime(&hf->hf_mtime, d);
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
  buf_t *buf;
  htsmsg_t *xml;
  int redircount = 0;
  char err0[128];
  int i;
  struct http_header_list headers, cookies;

  for(i = 0; i < 5; i++) {

    if(hf->hf_connection == NULL) 
      if(http_connect(hf, errbuf, errlen, 1))
	return -1;

    htsbuf_queue_init(&q, 0);

    http_headers_init(&headers, hf);
  
    htsbuf_qprintf(&q, 
		   "PROPFIND %s HTTP/1.%d\r\n"
		   "Depth: %d\r\n",
		   hf->hf_path,
		   hf->hf_version,
		   fd != NULL ? 1 : 0);
    LIST_INIT(&cookies);
    if(http_request_inspect(&headers, &cookies, 
                            hf, "PROPFIND", NULL, errbuf, errlen))
      return -1;

    http_cookie_append(hf->hf_connection->hc_hostname, hf->hf_path, &headers,
		       &cookies);
    http_headers_free(&cookies);

    HF_TRACE(hf, "Webdav sending request for %s (cid=%d)",
             hf->hf_url, hf->hf_connection->hc_id);
    http_headers_send(&q, &headers, hf->hf_user_request_headers);

    if(hf->hf_debug)
      trace_request(&q, hf);

    tcp_write_queue(hf->hf_connection->hc_tc, &q);
    code = http_read_response(hf, hf->hf_user_response_headers);

    if(code == -1) {
      if(hf->hf_connection->hc_reused)
	i--;
      http_detach(hf, 0, "Read error");
      continue;
    }

    switch(code) {
      
    case 207: /* 207 Multi-part */
      if((buf = http_read_content(hf)) == NULL) {
	snprintf(errbuf, errlen, "Connection lost");
	return -1;
      }

      xml = htsmsg_xml_deserialize_buf(buf, err0, sizeof(err0));
      if(xml == NULL) {
	snprintf(errbuf, errlen,
		 "WEBDAV/PROPFIND: XML parsing failed:\n%s", err0);
	return -1;
      }
      retval = parse_propfind(hf, xml, fd, errbuf, errlen);
      htsmsg_release(xml);
      return retval;

    case 301:
    case 302:
    case 303:
    case 307:
      if(redirect(hf, &redircount, errbuf, errlen, code, 1))
	return -1;
      continue;

    case 401:
      if(authenticate(hf, errbuf, errlen, non_interactive, 1))
	return -1;
      continue;

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
dav_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	 char *errbuf, size_t errlen, int non_interactive)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  int statcode = -1;
  hf->hf_debug = gconf.enable_http_debug;

  hf->hf_id = atomic_add_and_fetch(&http_file_tally, 1);
  hf->hf_version = 1;
  hf->hf_url = strdup(url);

  if(dav_propfind(hf, NULL, errbuf, errlen, 
		  non_interactive ? &statcode : NULL)) {
    http_destroy(hf);
    return statcode;
  }

  memset(fs, 0, sizeof(struct fa_stat));

  fs->fs_type = hf->hf_isdir ? CONTENT_DIR : CONTENT_FILE;
  fs->fs_size = hf->hf_filesize;
  fs->fs_mtime = hf->hf_mtime;

  http_destroy(hf);
  return 0;
}


/**
 *
 */
static int
dav_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
            char *errbuf, size_t errlen, int flags)
{
  int retval;
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  hf->hf_id = atomic_add_and_fetch(&http_file_tally, 1);
  hf->hf_version = 1;
  hf->hf_url = strdup(url);
  
  retval = dav_propfind(hf, fd, errbuf, errlen, NULL);
  http_destroy(hf);
  return retval;
}



/**
 *
 */
static fa_protocol_t fa_protocol_webdav = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_name  = "webdav",
  .fap_scan  = dav_scandir,
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = dav_stat,
  .fap_load = http_load,
  .fap_get_last_component = http_get_last_component,
  .fap_set_read_timeout = http_set_read_timeout,
  .fap_no_parking = http_no_parking,
};
FAP_REGISTER(webdav);

/**
 *
 */
static fa_protocol_t fa_protocol_webdavs = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL | FAP_ALLOW_CACHE,
  .fap_name  = "webdavs",
  .fap_scan  = dav_scandir,
  .fap_open  = http_open,
  .fap_close = http_close,
  .fap_read  = http_read,
  .fap_seek  = http_seek,
  .fap_fsize = http_fsize,
  .fap_stat  = dav_stat,
  .fap_load = http_load,
  .fap_get_last_component = http_get_last_component,
  .fap_set_read_timeout = http_set_read_timeout,
  .fap_no_parking = http_no_parking,
};
FAP_REGISTER(webdavs);


#define HTTP_TMP_SIZE 16384



/**
 *
 */
typedef struct http_query_arg {
  TAILQ_ENTRY(http_query_arg) link;
  char *key;
  char *val;
  size_t val_len;
} http_query_arg_t;

struct http_req_aux {
  atomic_t refcount;

  size_t total;
  int64_t bytes_completed;
  fa_load_cb_t *cb;
  void *opaque;
  char *tmpbuf;
  char *errbuf;
  size_t errlen;

  int (*encoded_data)(http_file_t *hf, struct http_req_aux *hra,
                      const void *data, int size);

  int (*decoded_data)(http_file_t *hf, struct http_req_aux *hra,
                      const void *data, int size);
  z_stream zstream;

  void *decoded_opaque;

  void (*decoded_cleanup)(struct http_req_aux *hra);

  http_file_t *hf;
  char *method;

  int post;
  htsbuf_queue_t postdata;
  char *postcontenttype;
  int want_result;
  char **arguments;
  TAILQ_HEAD(, http_query_arg) query_args;
  uint32_t flags;

  struct http_header_list headers_in;

  struct http_header_list *headers_out;

  void (*async_callback)(http_req_aux_t *har, void *opaque, int error);
  void *async_opaque;

  buf_t *result;

  int *http_code_ptr;
};

/**
 *
 */
static void
http_request_partial(void *opaque, int amount)
{
  http_req_aux_t *hra = opaque;

  amount += hra->bytes_completed;
  if(hra->cb != NULL)
    hra->cb(hra->opaque, amount, hra->total);
}


/**
 *
 */
static int
append_buf(http_file_t *hf, struct http_req_aux *hra,
           const void *data, int size)
{
  buf_t *b = hra->decoded_opaque;
  char *tmp = myreallocf(b->b_ptr, b->b_size + size + 1);
  if(tmp == NULL) {
    snprintf(hra->errbuf, hra->errlen, "out of memory");
    return -1;
  }

  b->b_ptr = tmp;
  memcpy(b->b_ptr + b->b_size, data, size);
  b->b_size += size;
  tmp[b->b_size] = 0;
  return 0;
}


/**
 *
 */
static void
cleanup_buf(struct http_req_aux *hra)
{
  buf_release(hra->decoded_opaque);
}


/**
 *
 */
static int
append_waste(http_file_t *hf, struct http_req_aux *hra,
	     const void *data, int size)
{
  return 0;
}


/**
 *
 */
static int
append_gzip(http_file_t *hf, struct http_req_aux *hra,
            const void *data, int size)
{
  z_stream *z = &hra->zstream;
  char tmp[8192];
  int zr;

  if(size == 0) {
    // EOF
    assert(z->avail_in == 0);

    while(1) {
      z->next_out = (void *)tmp;
      z->avail_out = sizeof(tmp);

      zr = inflate(z, Z_FINISH);
      if(zr < 0) {
        snprintf(hra->errbuf, hra->errlen, "zlib error %d", zr);
        return -1;
      }
      if(zr == Z_STREAM_END)
        return hra->decoded_data(hf, hra, NULL, 0);

      if(hra->decoded_data(hf, hra, tmp, sizeof(tmp) - z->avail_out))
        return -1;
    }
  }

  z->next_in = (void *)data;
  z->avail_in = size;

  while(z->avail_in) {
    z->next_out = (void *)tmp;
    z->avail_out = sizeof(tmp);

    zr = inflate(z, Z_NO_FLUSH);
    if(zr < 0) {
      snprintf(hra->errbuf, hra->errlen, "zlib error %d", zr);
      return -1;
    }
    if(hra->decoded_data(hf, hra, tmp, sizeof(tmp) - z->avail_out))
      return -1;
  }
  return 0;
}


/**
 *
 */
static int
http_recv_chunked(http_file_t *hf, http_req_aux_t *hra)
{
  char chunkheader[100];
  http_connection_t *hc = hf->hf_connection;

  while(1) {
    int remain;
    if(tcp_read_line(hc->hc_tc, chunkheader, sizeof(chunkheader)) < 0)
      return -2;

    remain = strtol(chunkheader, NULL, 16);
    if(remain == 0)
      break;

    while(remain > 0) {
      int rsize = MIN(remain, HTTP_TMP_SIZE);
      if(tcp_read_data(hc->hc_tc, hra->tmpbuf, rsize,
                       http_request_partial, hra))
        return -2;

      if(hra->encoded_data(hf, hra, hra->tmpbuf, rsize))
        return -1;

      hra->bytes_completed += rsize;

      remain -= rsize;
    }

    if(tcp_read_data(hc->hc_tc, chunkheader, 2, NULL, 0))
      return -2;
  }
  hf->hf_rsize = 0;
  return hra->encoded_data(hf, hra, NULL, 0);
}


/**
 *
 */
static int
http_recv_until_eof(http_file_t *hf, http_req_aux_t *hra)
{
  http_connection_t *hc = hf->hf_connection;

  while(1) {
    int r = tcp_read_data_nowait(hc->hc_tc, hra->tmpbuf, HTTP_TMP_SIZE);
    if(r < 0)
      break;

    if(hra->encoded_data(hf, hra, hra->tmpbuf, r))
      return -1;
    hra->bytes_completed += r;
  }
  return hra->encoded_data(hf, hra, NULL, 0);
}


/**
 *
 */
static int
http_recv(http_file_t *hf, http_req_aux_t *hra)
{
  http_connection_t *hc = hf->hf_connection;
  int64_t remain = hf->hf_rsize;

  while(remain > 0) {
    int rsize = MIN(remain, HTTP_TMP_SIZE);

    if(tcp_read_data(hc->hc_tc, hra->tmpbuf, rsize,
                     http_request_partial, hra))
      return -2;

    if(hra->encoded_data(hf, hra, hra->tmpbuf, rsize))
      return -1;

    hra->bytes_completed += rsize;

    remain -= rsize;
  }
  hf->hf_rsize = 0;
  return hra->encoded_data(hf, hra, hra->tmpbuf, 0);
}

/**
 *
 */
static int
http_req_do(http_req_aux_t *hra)
{
  htsbuf_queue_t q;
  int code, r = -1;
  int redircount = 0;
  struct http_header_list headers;
  struct http_query_arg *hqa;
  struct http_header_list cookies;
  http_file_t *hf = hra->hf;

 retry:

  http_connect(hf, hra->errbuf, hra->errlen, !hra->post);
  if(hf->hf_connection == NULL)
    goto cleanup;

  http_connection_t *hc = hf->hf_connection;

  htsbuf_queue_init(&q, 0);

  const char *m = hra->method ?: hra->post ? "POST": (hra->want_result ? "GET" : "HEAD");

  htsbuf_append(&q, m, strlen(m));
  htsbuf_append(&q, " ", 1);
  htsbuf_append(&q, hf->hf_path, strlen(hf->hf_path));

  // If the path already contains a '?' we prefix first parameter with '&'
  // instead
  char prefix = strchr(hf->hf_path, '?') ? '&' : '?';

  if(hra->arguments != NULL) {
    char **args = hra->arguments;

    while(args[0] != NULL) {
      if(args[1] != NULL) {
	htsbuf_append(&q, &prefix, 1);
	htsbuf_append_and_escape_url(&q, args[0]);
	htsbuf_append(&q, "=", 1);
	htsbuf_append_and_escape_url(&q, args[1]);
	prefix = '&';
      }
      args += 2;
    }
  }

  TAILQ_FOREACH(hqa, &hra->query_args, link) {
    htsbuf_append(&q, &prefix, 1);
    htsbuf_append_and_escape_url(&q, hqa->key);
    htsbuf_append(&q, "=", 1);
    htsbuf_append_and_escape_url_len(&q, hqa->val, hqa->val_len);
    prefix = '&';
  }

  htsbuf_qprintf(&q, " HTTP/1.%d\r\n", hf->hf_version);

  http_headers_init(&headers, hf);

  if(hra->post)
    http_header_add_int(&headers, "Content-Length", hra->postdata.hq_size);

  if(hra->postcontenttype != NULL)
    http_header_add(&headers, "Content-Type", hra->postcontenttype, 0);

  LIST_INIT(&cookies);

  if(!(hra->flags & FA_DISABLE_AUTH)) {
    if(http_request_inspect(&headers, &cookies, hf, m,
                            (const char **)hra->arguments,
                            hra->errbuf, hra->errlen)) {
      htsbuf_queue_flush(&q);
      r = -1;
      goto cleanup;
    }
  }

  http_cookie_append(hc->hc_hostname, hf->hf_path, &headers, &cookies);
  http_headers_free(&cookies);

  HF_TRACE(hf, "Sending request for %s (cid=%d)",
           hf->hf_url, hf->hf_connection->hc_id);
  http_headers_send(&q, &headers, &hra->headers_in);

  if(hf->hf_debug)
    trace_request(&q, hf);

  tcp_write_queue(hf->hf_connection->hc_tc, &q);

  if(hra->post) {
    if(hf->hf_debug)
      htsbuf_hexdump(&hra->postdata, "HTTP-POSTDATA");

    tcp_write_queue_dontfree(hf->hf_connection->hc_tc, &hra->postdata);
  }

  code = http_read_response(hf, hra->headers_out);
  if(code == -1 && hf->hf_connection->hc_reused) {
    http_detach(hf, 0, "Read error on reused connection");
    goto retry;
  }

  int no_content = !strcmp(m, "HEAD");

  if(hra->http_code_ptr != NULL)
    *hra->http_code_ptr = code;

  switch(code) {
  case 204:
    no_content = 1;
    // FALLTHRU
  case 200:
  case 201:
  case 202:
  case 203:
  case 205:
    if(no_content)
      hf->hf_rsize = 0;
    break;

  case 304:
    // Not modified
    http_drain_content(hf);
    http_destroy(hf);
    if(hra->decoded_cleanup)
      hra->decoded_cleanup(hra);
    return 304;

  case 302:
  case 303:
    hra->post = 0;
    mystrset(&hra->postcontenttype, NULL);
    mystrset(&hra->method, hra->want_result ? "GET" : "HEAD");
    // FALLTHRU
  case 301:
  case 307:
    if(hra->flags & FA_NOFOLLOW) {
      HF_TRACE(hf, "Not following redirect as requested by caller");
      break;
    }
    if(redirect(hf, &redircount, hra->errbuf, hra->errlen, code, !no_content))
      goto cleanup;

    goto retry;

  case 401:
    if(authenticate(hf, hra->errbuf, hra->errlen, NULL, !no_content))
      goto cleanup;
    goto retry;

  case 206:
    /* We got "Partial Content" without asking for it.  Some servers
       (FlashCom/3.5.7) seem to "remember" Range requests from
       previous queries on same connection if it's not overwritten
       with a new Range: HTTP header. Clearly a bug, but we'll deal
       with it by redoing the request
    */
    http_detach(hf, 0, "Got 206 without asking for it");
    goto retry;

  default:

    if(hra->flags & FA_CONTENT_ON_ERROR && hf->hf_rsize && code > 0) {
      HF_TRACE(hf, "%s failed with %d but content is available",
               hf->hf_url, code);
      break;
    }

    snprintf(hra->errbuf, hra->errlen, "HTTP error: %d", code);

    if(!no_content && http_drain_content(hf))
      hf->hf_connection_mode = CONNECTION_MODE_CLOSE;
    goto cleanup;
  }

  if(!no_content) {

    if(hf->hf_content_encoding == HTTP_CE_GZIP) {
      inflateInit2(&hra->zstream, 16+MAX_WBITS);
      hra->encoded_data = &append_gzip;
      HF_TRACE(hf, "Inflating content using gzip");
    } else {
      hra->encoded_data = hra->decoded_data;
    }

    hra->tmpbuf = malloc(HTTP_TMP_SIZE);

    if(hf->hf_chunked_transfer) {
      HF_TRACE(hf, "Chunked transfer");
      r = http_recv_chunked(hf, hra);
    } else if(hf->hf_rsize == -1) {
      HF_TRACE(hf, "Reading data until EOF");
      r = http_recv_until_eof(hf, hra);
    } else {
      HF_TRACE(hf, "Reading %"PRId64" bytes", hf->hf_rsize);
      hra->total = hf->hf_rsize;
      r = http_recv(hf, hra);
    }

    if(r == -2) {
      snprintf(hra->errbuf, hra->errlen, "Network error");
      r = -1;
    }

    if(hf->hf_content_encoding == HTTP_CE_GZIP)
      inflateEnd(&hra->zstream);
  } else {
    HF_TRACE(hf, "No data transfered");
    r = 0;
  }
 cleanup:
  free(hra->tmpbuf);

  if(r && hra->decoded_cleanup)
    hra->decoded_cleanup(hra);
  http_destroy(hra->hf);
  return r;
}



/**
 *
 */
void
http_req_release(http_req_aux_t *hra)
{
  http_query_arg_t *hqa, *next;

  if(atomic_dec(&hra->refcount))
    return;

  http_headers_free(&hra->headers_in);
  free(hra->method);
  strvec_free(hra->arguments);
  free(hra->postcontenttype);

  for(hqa = TAILQ_FIRST(&hra->query_args); hqa != NULL; hqa = next) {
    next = TAILQ_NEXT(hqa, link);
    free(hqa->key);
    free(hqa->val);
    free(hqa);
  }

  htsbuf_queue_flush(&hra->postdata);
  buf_release(hra->result);
  free(hra);
}


/**
 *
 */
http_req_aux_t *
http_req_retain(http_req_aux_t *hra)
{
  atomic_inc(&hra->refcount);
  return hra;
}


/**
 *
 */
static void
http_req_async(void *opaque)
{
  http_req_aux_t *hra = opaque;
  int r = http_req_do(hra);
  if(r)
    hra->result = NULL;

  hra->async_callback(hra, hra->async_opaque, r);
  http_req_release(hra);
}

/**
 *
 */
int
http_reqv(const char *url, va_list ap,
          void (*async_callback)(http_req_aux_t *hra, void *opaque, int error),
          void *async_opaque)
{
  http_file_t *hf = calloc(1, sizeof(http_file_t));
  http_req_aux_t *hra = calloc(1, sizeof(http_req_aux_t));
  int tag;
  int i, j;
  const char *key, *val;
  int len;
  char tmpbuf[32];
  const char **arguments;
  htsbuf_queue_t *hq;
  int i32;
  int64_t i64;

  hf->hf_id = atomic_add_and_fetch(&http_file_tally, 1);

  atomic_set(&hra->refcount, 1);

  TAILQ_INIT(&hra->query_args);

  hra->decoded_data = append_waste;
  htsbuf_queue_init(&hra->postdata, 0);

  while((tag = va_arg(ap, int)) != 0) {
    switch(tag) {

    case HTTP_TAG_ARG:
      key = va_arg(ap, const char *);
      val = va_arg(ap, const char *);
      if(key != NULL && val != NULL) {
        http_query_arg_t *hqa = malloc(sizeof(http_query_arg_t));
        hqa->key = strdup(key);
        hqa->val = strdup(val);
        hqa->val_len = strlen(val);
        TAILQ_INSERT_TAIL(&hra->query_args, hqa, link);
      }
      break;

    case HTTP_TAG_ARGINT:
      key = va_arg(ap, const char *);
      i32 = va_arg(ap, int);
      if(key != NULL) {
        http_query_arg_t *hqa = malloc(sizeof(http_query_arg_t));
        hqa->key = strdup(key);
        snprintf(tmpbuf, sizeof(tmpbuf), "%d", i32);
        hqa->val = strdup(tmpbuf);
        hqa->val_len = strlen(tmpbuf);
        TAILQ_INSERT_TAIL(&hra->query_args, hqa, link);
      }
      break;

    case HTTP_TAG_ARGINT64:
      key = va_arg(ap, const char *);
      i64 = va_arg(ap, int64_t);
      if(key != NULL) {
        http_query_arg_t *hqa = malloc(sizeof(http_query_arg_t));
        hqa->key = strdup(key);
        snprintf(tmpbuf, sizeof(tmpbuf), "%"PRId64, i64);
        hqa->val = strdup(tmpbuf);
        hqa->val_len = strlen(tmpbuf);
        TAILQ_INSERT_TAIL(&hra->query_args, hqa, link);
      }
      break;


    case HTTP_TAG_ARGBIN:
      key = va_arg(ap, const char *);
      val = va_arg(ap, const char *);
      len = va_arg(ap, int);
      if(key != NULL) {
        http_query_arg_t *hqa = malloc(sizeof(http_query_arg_t));
        hqa->key = strdup(key);
        hqa->val = malloc(len);
        memcpy(hqa->val, val, len);
        hqa->val_len = len;
        TAILQ_INSERT_TAIL(&hra->query_args, hqa, link);
      }
      break;

    case HTTP_TAG_ARGLIST:
      arguments = va_arg(ap, const char **);
      if(arguments != NULL) {
        assert(hra->arguments == NULL); // May not be passed multiple times
        j = 0;
        while(arguments[j] != NULL)
          j++;

        hra->arguments = malloc(sizeof(const char *) * (j + 1));
        for(i = 0; i < j; i++)
          hra->arguments[i] = strdup(arguments[i]);
        hra->arguments[i] = NULL;
      }
      break;

    case HTTP_TAG_RESULT_PTR:
      assert(hra->decoded_opaque == NULL);
      assert(hra->want_result == 0);
      buf_t **ptr = va_arg(ap, buf_t **);
      if(ptr == NULL)
	break;

      if(ptr == HTTP_BUFFER_INTERNALLY)
        ptr = &hra->result;
      else
        *ptr = NULL;

      hra->decoded_opaque = calloc(1, sizeof(buf_t));
      *ptr = hra->decoded_opaque;
      hra->decoded_data = append_buf;
      hra->decoded_cleanup = cleanup_buf;
      buf_t *b = hra->decoded_opaque;
      atomic_set(&b->b_refcount, 1);
      b->b_free = &free;
      hra->want_result = 1;
      break;

    case HTTP_TAG_ERRBUF:
      hra->errbuf = va_arg(ap, char *);
      hra->errlen = va_arg(ap, size_t);
      break;

    case HTTP_TAG_POSTDATA:
      hq = va_arg(ap, htsbuf_queue_t *);
      key = va_arg(ap, const char *);
      if(hq != NULL) {
        htsbuf_appendq(&hra->postdata, hq);
        hra->postcontenttype = strdup(key);
        hra->post = 1;
      }
      break;

    case HTTP_TAG_FLAGS:
      hra->flags = va_arg(ap, int);
      break;

    case HTTP_TAG_REQUEST_HEADER:
      key = va_arg(ap, const char *);
      val = va_arg(ap, const char *);
      if(key != NULL && val != NULL)
        http_header_add(&hra->headers_in, key, val, 1);
      break;

    case HTTP_TAG_REQUEST_HEADERS:
      http_header_merge(&hra->headers_in,
                        va_arg(ap, const struct http_header_list *));
      break;

    case HTTP_TAG_RESPONSE_HEADERS:
      hra->headers_out = va_arg(ap, struct http_header_list *);
      break;

    case HTTP_TAG_METHOD:
      mystrset(&hra->method, va_arg(ap, const char *));
      break;

    case HTTP_TAG_PROGRESS_CALLBACK:
      hra->cb = va_arg(ap, fa_load_cb_t *);
      hra->opaque = va_arg(ap, void *);
      break;

    case HTTP_TAG_CANCELLABLE:
      hf->hf_cancellable = cancellable_retain(va_arg(ap, cancellable_t *));
      break;

    case HTTP_TAG_CONNECT_TIMEOUT:
      hf->hf_connect_timeout = va_arg(ap, int);
      break;

    case HTTP_TAG_READ_TIMEOUT:
      hf->hf_read_timeout = va_arg(ap, int);
      break;

    case HTTP_TAG_LOCATION:
      hf->hf_ret_location = va_arg(ap, char **);
      if(hf->hf_ret_location != NULL)
        *hf->hf_ret_location = NULL;
      break;

    case HTTP_TAG_RESPONSE_CODE:
      hra->http_code_ptr = va_arg(ap, int *);
      if(hra->http_code_ptr != NULL)
        *hra->http_code_ptr = 0;
      break;

    default:
      abort();
    }
  }

  hra->async_callback = async_callback;
  hra->async_opaque   = async_opaque;

  if(hra->headers_out != NULL)
    LIST_INIT(hra->headers_out);
  hf->hf_version = 1;
  if(!(hra->flags & FA_NO_DEBUG))
    hf->hf_debug = !!(hra->flags & FA_DEBUG) || gconf.enable_http_debug;
  hf->hf_req_compression = !!(hra->flags & FA_COMPRESSION);
  hf->hf_url = strdup(url);

  hra->hf = hf;

  if(hra->async_callback != NULL) {
    task_run(http_req_async, hra);
    return 0;
  }

  int r = http_req_do(hra);
  http_req_release(hra);
  return r;
}



/**
 *
 */
int
http_req(const char *url, ...)
{
  va_list ap;
  va_start(ap, url);
  int r = http_reqv(url, ap, NULL, NULL);
  va_end(ap);
  return r;
}



/**
 *
 */
buf_t *
http_req_get_result(http_req_aux_t *hra)
{
  return hra->result;
}
