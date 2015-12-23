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
#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <limits.h>

#include "networking/http_server.h"
#include "event.h"
#include "image/image.h"
#include "misc/str.h"
#include "backend/backend.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/http_client.h"
#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_json.h"

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
    event_to_ui(event_create_openurl(url));
    return http_redirect(hc, "/api/open");
  }

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, openpage, strlen(openpage));
  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}


/**
 *
 */
static void
diag_html(http_connection_t *hc, htsbuf_queue_t *out)
{
  char p1[1000];
  time_t t0;
  int i;

  time(&t0);

  for(i = 0; i <= 5; i++) {
    struct stat st;
    snprintf(p1, sizeof(p1), "%s/log/"APPNAME"-%d.log", gconf.cache_path,i);
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


    htsbuf_qprintf(out,
		   APPNAME"-%d.log (Last modified %s ago): <a href=\"/api/logfile/%d\">View</a> | <a href=\"/api/logfile/%d?mode=download\">Download</a>| <a href=\"/api/logfile/%d?mode=pastebin\">Pastebin</a><br>", i, timestr, i, i, i);
  }
}

/**
 *
 */
static int
hc_root_old(http_connection_t *hc)
{
  htsbuf_queue_t out;

  const char *url = http_arg_get_req(hc, "url");

  if(url != NULL) {
    event_dispatch(event_create_openurl(url));
    return http_redirect(hc, "/");
  }

  htsbuf_queue_init(&out, 0);

  htsbuf_qprintf(&out, 
		 "<html><body>"
		 "<h2>%s</h2><p>Version %s"
		 , gconf.system_name,
		 htsversion_full);

  htsbuf_qprintf(&out, 
		 "<form name=\"input\" method=\"get\">"
		 "Open URL in %s: "
		 "<input type=\"text\" name=\"url\" style=\"width:500px\"/>"
		 "<input type=\"submit\" value=\"Open\" />"
		 "</form>", APPNAMEUSER);

  htsbuf_qprintf(&out, "<h3>Diagnostics</h3>"); 

  diag_html(hc, &out);
  htsbuf_qprintf(&out, "<p><a href=\"/api/screenshot\">Upload screenshot to imgur</a></p>");
  htsbuf_qprintf(&out, "<p><a href=\"/api/translation\">Upload and test new translation (.lang) file</a></p>");

  htsbuf_qprintf(&out, "</body></html>");

  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}


static int
hc_done(http_connection_t *hc, const char *remain, void *opaque,
        http_cmd_t method)
{
  htsbuf_queue_t out;

  htsbuf_queue_init(&out, 0);
  htsbuf_qprintf(&out, "OK");
  return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
}


static int
hc_image(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;
  image_t *img;
  char errbuf[200];
  const char *content;
  image_meta_t im = {0};
  im.im_no_decoding = 1;
  rstr_t *url;
  const char *u = http_arg_get_req(hc, "url");
  
  if(u != NULL) {
    url = rstr_alloc(u);
    url_deescape(rstr_data(url));
  } else {
    if(remain == NULL) {
      return 404;
    }
    url = rstr_alloc(remain);
  }

  img = backend_imageloader(url, &im, NULL, errbuf, sizeof(errbuf), NULL,
                            NULL);
  rstr_release(url);
  if(img == NULL)
    return http_error(hc, 404, "Unable to load image %s : %s",
		      remain, errbuf);

  const image_component_t *ic = image_find_component(img, IMAGE_CODED);
  if(ic == NULL) {
    image_release(img);
    return http_error(hc, 404,
		      "Unable to load image %s : Original data not available",
		      remain);
  }
  const image_component_coded_t *icc = &ic->coded;

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, buf_cstr(icc->icc_buf), buf_len(icc->icc_buf));

  switch(icc->icc_type) {
  case IMAGE_JPEG:
    content = "image/jpeg";
    break;
  case IMAGE_PNG:
    content = "image/png";
    break;
  case IMAGE_GIF:
    content = "image/gif";
    break;
  case IMAGE_BMP:
    content = "image/bmp";
    break;
  default:
    content = "image";
    break;
  }

  image_release(img);

  return http_send_reply(hc, 0, content, NULL, NULL, 0, &out);
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
  
  const char *fname = gconf.upgrade_path ?: gconf.binary;

  TRACE(TRACE_INFO, "BINREPLACE", "Replacing %s with %d bytes received",
	fname, (int)len);

  unlink(fname);

  int fd = open(fname, O_CREAT | O_RDWR, 0777);
  if(fd == -1) {
    TRACE(TRACE_ERROR, "BINREPLACE", "Unable to open file");
    return HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
  }

  if(write(fd, data, len) != len)
    TRACE(TRACE_ERROR, "BINREPLACE", "Unable to write to file");
  
  close(fd);
  TRACE(TRACE_INFO, "BINREPLACE", "All done, restarting");
  app_shutdown(13);
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

  htsbuf_qprintf(&out, 
		 "<html><body>"
		 "<strong>%s</strong> Version %s<br><br>"
		 , gconf.system_name,
		 htsversion_full);

  diag_html(hc, &out);

  htsbuf_qprintf(&out, "</body></html>");

  return http_send_reply(hc, 0, "text/html; charset=utf-8", NULL, NULL, 0, &out);
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
  const char *mode = http_arg_get_req(hc, "mode");

  char p1[500];
  snprintf(p1, sizeof(p1), "%s/log/"APPNAME"-%d.log", gconf.cache_path, n);
  buf_t *buf = fa_load(p1, NULL);

  if(buf == NULL)
    return 404;

  if(mode != NULL && !strcmp(mode, "pastebin")) {
    buf_t *result = NULL;
    htsbuf_queue_t hq;
    htsbuf_queue_init(&hq, 0);

    htsbuf_append(&hq, "sprunge=", 8);
    htsbuf_append_and_escape_url(&hq, buf_cstr(buf));
    buf_release(buf);

    char errbuf[256];

    int ret = http_req("http://sprunge.us",
		       HTTP_RESULT_PTR(&result),
		       HTTP_POSTDATA(&hq, "application/x-www-form-urlencoded"),
		       HTTP_ERRBUF(errbuf, sizeof(errbuf)),
		       NULL);


    if(ret) {
      htsbuf_append(&out, errbuf, strlen(errbuf));
      return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
    }

    htsbuf_qprintf(&out, "<html><body><a href=\"%s\">%s</a></body></html>",
		   buf_cstr(result), buf_cstr(result));

    buf_release(result);
    return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
  }

  htsbuf_append_buf(&out, buf);
  if (mode != NULL && !strcmp(mode, "download")) {
    snprintf(p1, sizeof(p1), "attachment; filename=\""APPNAME"-%d.log\"", n);
    http_set_response_hdr(hc, "Content-Disposition", p1);
  }
  return http_send_reply(hc, 0, "text/plain; charset=utf-8", NULL, NULL, 0, &out);
}


#if 0

extern void my_malloc_stats(void (*fn)(const char *fmt, ...));

char hugebuf[1024 * 1024];
int hugeptr;

static void memdumpf(const char *fmt, ...)
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
  return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
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
  return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
}

#endif





/**
 *
 */
static int
hc_echo_init(http_connection_t *hc)
{
  TRACE(TRACE_DEBUG, "WS", "Connected to echo");
  return 0;
}


/**
 *
 */
static int
hc_echo_data(http_connection_t *hc, int opcode, 
	     uint8_t *data, size_t len, void *opaque)
{
  websocket_send(hc, opcode, data, len);
  TRACE(TRACE_DEBUG, "WS", "Echoing %zd bytes (opcode:%d)", len, opcode);
  return 0;
}


/**
 *
 */
static void
hc_echo_fini(http_connection_t *hc, void *opaque)
{
  TRACE(TRACE_DEBUG, "WS", "Disconnected from echo");
}


/**
 *
 */
static const struct {
  const char *pfx;
  const char *contenttype;
} cttable[] = {
  { "html", "text/html; charset=utf-8" },
  { "js", "application/javascript" },
  { "css", "text/css" },
};


/**
 *
 */
static int
hc_serve_file(http_connection_t *hc, const char *file, const char *contenttype)
{
  htsbuf_queue_t out;

  if(contenttype == NULL) {
    const char *pfx = strrchr(file, '.');
    if(pfx != NULL) {
      pfx++;
      int i;
      for(i = 0; i < sizeof(cttable) / sizeof(cttable[0]); i++) {
	if(!strcmp(pfx, cttable[i].pfx)) {
	  contenttype = cttable[i].contenttype;
	  break;
	}
      }
    }
  }

  buf_t *b = fa_load(file, NULL);
  if(b == NULL)
    return 404;

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, b->b_ptr, b->b_size);
  buf_release(b);
  return http_send_reply(hc, 0, contenttype, NULL, NULL, 0, &out);
}


/**
 *
 */
static int
hc_root(http_connection_t *hc, const char *remain, void *opaque,
	  http_cmd_t method)
{
  if(!gconf.enable_experimental)
    return hc_root_old(hc);
  return hc_serve_file(hc, "dataroot://res/static/index.html", NULL);
}

/**
 *
 */
static int
hc_favicon(http_connection_t *hc, const char *remain, void *opaque,
	   http_cmd_t method)
{
  return hc_serve_file(hc, "dataroot://res/static/favicon.ico",
		       "image/ico");
}


/**
 *
 */
static int
hc_static(http_connection_t *hc, const char *remain, void *opaque,
	   http_cmd_t method)
{
  char path[PATH_MAX];
  if(remain == NULL || strstr(remain, ".."))
    return 404;
  snprintf(path, sizeof(path), "dataroot://res/static/%s", remain);
  return hc_serve_file(hc, path, NULL);
}

/**
 *
 */
static int
hc_open_parameterized(http_connection_t *hc, const char *remain,
                      void *opaque, http_cmd_t method)
{
  if(remain == NULL)
    return 404;

  htsmsg_t *msg = htsmsg_create_map();
  http_req_args_fill_htsmsg(hc, msg);

  htsbuf_queue_t buf;
  htsbuf_queue_init(&buf, 0);

  htsbuf_qprintf(&buf, "%s:%s", remain, htsmsg_json_serialize_to_str(msg, 0));
  char *s = htsbuf_to_string(&buf);
  event_dispatch(event_create_openurl(s));
  free(s);
  htsbuf_queue_flush(&buf);
  htsmsg_release(msg);
  return http_redirect(hc, "/");
}

/**
 *
 */
static int
hc_restart(http_connection_t *hc, const char *remain,
                      void *opaque, http_cmd_t method)
{
  app_shutdown(13);
  return HTTP_STATUS_OK;
}


/**
 *
 */
static void
httpcontrol_init(void)
{
  http_path_add("/api/done", NULL, hc_done, 0);
  http_path_add("/api/image", NULL, hc_image, 0);
  http_path_add("/api/open", NULL, hc_open, 1);
  http_path_add("/api/openparameterized", NULL, hc_open_parameterized, 0);
  http_path_add("/api/input/action", NULL, hc_action, 0);
  http_path_add("/api/input/utf8", NULL, hc_utf8, 1);
  http_path_add("/api/notifyuser", NULL, hc_notify_user, 1);
  http_path_add("/api/diag", NULL, hc_diagnostics, 1);
  http_path_add("/api/logfile", NULL, hc_logfile, 0);
  http_path_add("/api/replace", NULL, hc_binreplace, 1);
  http_add_websocket("/api/ws/echo",
		     hc_echo_init, hc_echo_data, hc_echo_fini);

  http_path_add("/", NULL, hc_root, 1);
  http_path_add("/favicon.ico", NULL, hc_favicon, 1);
  http_path_add("/api/static", NULL, hc_static, 0);
  if(gconf.can_restart)
    http_path_add("/api/restart", NULL, hc_restart, 1);
}

INITME(INIT_GROUP_API, httpcontrol_init, NULL, 0);
