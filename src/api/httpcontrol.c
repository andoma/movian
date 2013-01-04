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

#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>

#include "networking/http_server.h"
#include "event.h"
#include "misc/pixmap.h"
#include "misc/str.h"
#include "backend/backend.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"

#define STRINGIFY(A)  #A

const char *openpage = STRINGIFY(
<html>
 <body>
  <form name="input" method="get">
   URL: <input type="text" name="url" style="width:500px"/>
   <input type="submit" value="Open" />
  </form>
 </body>
</html>
);

static int
hc_open(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;

  const char *url = http_arg_get_req(hc, "url");

  if(url != NULL) {
    event_dispatch(event_create_openurl(url, NULL, NULL, NULL, NULL));
    return http_redirect(hc, "/showtime/open");
  }

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, openpage, strlen(openpage));
  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}


static int
hc_done(http_connection_t *hc, const char *remain, void *opaque,
        http_cmd_t method)
{
  htsbuf_queue_t out;

  htsbuf_queue_init(&out, 0);
  htsbuf_qprintf(&out, "OK");
  return http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
}


static int
hc_image(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;
  pixmap_t *pm;
  char errbuf[200];
  const char *content;
  image_meta_t im = {0};
  im.im_no_decoding = 1;

  rstr_t *url = rstr_alloc(remain);

  pm = backend_imageloader(url, &im, NULL, errbuf, sizeof(errbuf), NULL,
			   NULL, NULL);
  rstr_release(url);
  if(pm == NULL)
    return http_error(hc, 404, "Unable to load image %s : %s",
		      remain, errbuf);
  
  if(!pixmap_is_coded(pm)) {
    pixmap_release(pm);
    return http_error(hc, 404, 
		      "Unable to load image %s : Original data not available",
		      remain);
  }

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, pm->pm_data, pm->pm_size);

  switch(pm->pm_type) {
  case PIXMAP_JPEG:
    content = "image/jpeg";
    break;
  case PIXMAP_PNG:
    content = "image/png";
    break;
  case PIXMAP_GIF:
    content = "image/gif";
    break;
  default:
    content = "image";
    break;
  }

  pixmap_release(pm);

  return http_send_reply(hc, 0, content, NULL, NULL, 0, &out);
}



static prop_t *
prop_from_path(const char *path)
{
  char **n = strvec_split(path, '/');
  prop_t *p = prop_get_by_name((const char **)n, 1, NULL);
  strvec_free(n);
  return p;
}



static int
hc_prop(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;
  rstr_t *r;
  int rval, i;
  prop_t *p;
  const char *action = http_arg_get_req(hc, "action");

  if(remain == NULL)
    return 404;

  p = prop_from_path(remain);

  if(p == NULL)
    return 404;
  
  htsbuf_queue_init(&out, 0);

  switch(method) {
  case HTTP_CMD_GET:

    if(action != NULL) {
      event_t *e = event_create_action_str(action);
      prop_send_ext_event(p, e);
      event_release(e);
      rval = HTTP_STATUS_OK;
      break;
    }

    r = prop_get_string(p, NULL);

    if(r == NULL) {

      char **childs = prop_get_name_of_childs(p);
      if(childs == NULL) {
	rval = HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
	break;
      }
      htsbuf_qprintf(&out, "dir");
      for(i = 0; childs[i] != NULL; i++) {
	htsbuf_qprintf(&out, "%c%s", i ? ',' : ':', childs[i]);
      }
    } else {
      htsbuf_qprintf(&out, "value:");
      htsbuf_append(&out, rstr_get(r), strlen(rstr_get(r)));
      rstr_release(r);
    }
    htsbuf_append(&out, "\n", 1);
    rval = http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
    break;

  default:
    rval = HTTP_STATUS_METHOD_NOT_ALLOWED;
    break;
  }

  prop_ref_dec(p);

  return rval;
}


static int
hc_action(http_connection_t *hc, const char *remain, void *opaque,
	  http_cmd_t method)
{
  if(remain == NULL)
    return 404;

  event_to_ui(event_create_action_str(remain));
  return HTTP_STATUS_OK;
}


static int
hc_utf8(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  const char *str = http_arg_get_req(hc, "str");
  int c;
  event_t *e;

  if(str == NULL)
    return HTTP_STATUS_BAD_REQUEST;

  while((c = utf8_get(&str)) != 0) {
    switch(c) {
    case 8:
      e = event_create_action_multi(
				    (const action_type_t[]){
				      ACTION_BS, ACTION_NAV_BACK}, 2);
      break;

    default:
      e = event_create_int(EVENT_UNICODE, c);
      break;
    }
    event_to_ui(e);
  }
  return HTTP_STATUS_OK;
}


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/**
 *
 */
static int
hc_binreplace(http_connection_t *hc, const char *remain, void *opaque,
	      http_cmd_t method)
{
  if(gconf.binary == NULL)
    return HTTP_STATUS_PRECONDITION_FAILED;

  if(!gconf.enable_bin_replace)
    return 403;

  size_t len;
  void *data = http_get_post_data(hc, &len, 0);

  if(method != HTTP_CMD_POST || data == NULL)
    return 405;
  
  TRACE(TRACE_INFO, "BINREPLACE", "Replacing %s with %d bytes received",
	gconf.binary, (int)len);

  int fd = open(gconf.binary, O_TRUNC | O_RDWR, 0777);
  if(fd == -1) {
    TRACE(TRACE_ERROR, "BINREPLACE", "Unable to open file");
    return HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
  }

  if(write(fd, data, len) != len)
    TRACE(TRACE_ERROR, "BINREPLACE", "Unable to write to file");
  
  close(fd);
  TRACE(TRACE_INFO, "BINREPLACE", "All done, restarting");
  showtime_shutdown(13);
  return HTTP_STATUS_OK;
}


/**
 *
 */
static int
hc_notify_user(http_connection_t *hc, const char *remain, void *opaque,
	       http_cmd_t method)
{
  const char *msg  = http_arg_get_req(hc, "msg");
  const char *icon = http_arg_get_req(hc, "icon");
  const char *lstr = http_arg_get_req(hc, "level");
  int timeout = atoi(http_arg_get_req(hc, "timeout") ?: "5");
  notify_type_t type = NOTIFY_INFO;

  if(msg == NULL)
    return HTTP_STATUS_BAD_REQUEST;

  if(lstr != NULL && !strcmp(lstr, "error"))
    type = NOTIFY_ERROR;
  else if(lstr != NULL && !strcmp(lstr, "warning"))
    type = NOTIFY_WARNING;

  notify_add(NULL, type, icon, timeout, rstr_alloc("%s"), msg);
  return HTTP_STATUS_OK;
}


/**
 *
 */
static int
hc_diagnostics(http_connection_t *hc, const char *remain, void *opaque,
	       http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);
  extern const char *htsversion_full;
  char p1[1000];
  time_t t0;
  int i;

  time(&t0);

  htsbuf_qprintf(&out, 
		 "<html><body>"
		 "Showtime version %s<br><br>"
		 ,
		 htsversion_full);

  for(i = 0; i <= 5; i++) {
    struct stat st;
    snprintf(p1, sizeof(p1), "%s/log/showtime.log.%d", gconf.cache_path,i);
    if(stat(p1, &st)) 
      continue;
    char timestr[32];
    time_t modtime = t0 - st.st_mtime;
    if(modtime < 60)
      snprintf(timestr, sizeof(timestr), "%d seconds", (int)modtime);
    else if(modtime < 3600)
      snprintf(timestr, sizeof(timestr), "%d minutes", (int)modtime / 60);
    else
      snprintf(timestr, sizeof(timestr), "%d hours", (int)modtime / 3600);


    htsbuf_qprintf(&out,
		   "showtime.log.%d (Last modified %s ago): <a href=\"/showtime/logfile/%d\">View</a> | <a href=\"/showtime/logfile/%d?mode=download\">Download</a><br>", i, timestr, i, i);
  }
  htsbuf_qprintf(&out, 
		 "</body></html>");

  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}


/**
 *
 */
static int
hc_logfile(http_connection_t *hc, const char *remain, void *opaque,
	   http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  if(remain == NULL)
    return 400;
  const int n = atoi(remain);
  size_t size;
  const char *mode = http_arg_get_req(hc, "mode");

  char p1[500];
  snprintf(p1, sizeof(p1), "%s/log/showtime.log.%d", gconf.cache_path, n);
  char *buf = fa_load(p1, &size, NULL, NULL, 0, NULL, 0, NULL, NULL);
  
  if(buf == NULL)
    return 404;
  htsbuf_append_prealloc(&out, buf, size);
  if (mode != NULL && !strcmp(mode, "download")) {
    snprintf(p1, sizeof(p1), "attachment; filename=\"showtime.log.%d\"", n);
    http_set_response_hdr(hc, "Content-Disposition", p1);
  }
  return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
}


#if 0

extern void my_malloc_stats(void (*fn)(const char *fmt, ...));

char hugebuf[1024 * 1024];
int hugeptr;

static void __attribute__((unused))memdumpf(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  hugeptr += vsprintf(hugebuf + hugeptr, fmt, ap);
  va_end(ap);
  hugebuf[hugeptr++] = '\n';

}


/**
 *
 */
static int
hc_memstats(http_connection_t *hc, const char * remain, void *opaque,
	    http_cmd_t method)
{
  htsbuf_queue_t out;
  hugeptr = 0;

  my_malloc_stats(memdumpf);

  htsbuf_queue_init(&out, 0);

  htsbuf_append(&out, hugebuf, hugeptr);
  return http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
}

/**
 *
 */
static void
hexdump(const char *pfx, const uint8_t *data, int len, htsbuf_queue_t *out)
{
  int i, j;
  for(i = 0; i < len; i+= 16) {
    htsbuf_qprintf(out, "%s: 0x%06x: ", pfx, i);

    for(j = 0; j + i < len && j < 16; j++) {
      htsbuf_qprintf(out, "%02x ", data[i+j]);
    }
    
    for(; j < 16; j++) {
      htsbuf_qprintf(out, "   ");
    }

    htsbuf_qprintf(out, "  ");


    for(j = 0; j + i < len && j < 16; j++) {
      htsbuf_qprintf(out, "%c", data[i+j] < 32 ? '.' : data[i+j]);
    }
    htsbuf_qprintf(out, "\n");
  }
  htsbuf_qprintf(out, "\n");

}

/**
 *
 */
static int
hc_hexdump(http_connection_t *hc, const char *remain, void *opaque,
	    http_cmd_t method)
{
  htsbuf_queue_t out;
  const char *atxt = http_arg_get_req(hc, "addr");
  const char *ltxt = http_arg_get_req(hc, "len");

  if(atxt == NULL || ltxt == NULL)
    return HTTP_STATUS_BAD_REQUEST;

  void *a = (void *)strtol(atxt, NULL, 16);
  long len = strtol(ltxt, NULL, 0);
  
  htsbuf_queue_init(&out, 0);

  hexdump("mem", a, len, &out);
  return http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
}

#endif




/**
 *
 */
static void
httpcontrol_init(void)
{
  http_path_add("/showtime/done", NULL, hc_done, 0);
  http_path_add("/showtime/image", NULL, hc_image, 0);
  http_path_add("/showtime/open", NULL, hc_open, 1);
  http_path_add("/showtime/prop", NULL, hc_prop, 0);
  http_path_add("/showtime/input/action", NULL, hc_action, 0);
  http_path_add("/showtime/input/utf8", NULL, hc_utf8, 1);
  http_path_add("/showtime/notifyuser", NULL, hc_notify_user, 1);
  http_path_add("/showtime/diag", NULL, hc_diagnostics, 1);
  http_path_add("/showtime/logfile", NULL, hc_logfile, 0);
  http_path_add("/showtime/replace", NULL, hc_binreplace, 1);
}

INITME(INIT_GROUP_API, httpcontrol_init);
