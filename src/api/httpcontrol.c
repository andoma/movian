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

#include "networking/http_server.h"
#include "httpcontrol.h"
#include "event.h"
#include "misc/pixmap.h"
#include "backend/backend.h"

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
    event_dispatch(event_create_openurl(url, NULL));
    return http_redirect(hc, "/control/open");
  }

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, openpage, strlen(openpage));
  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}


static int
hc_image(http_connection_t *hc, const char *remain, void *opaque,
	http_cmd_t method)
{
  htsbuf_queue_t out;
  pixmap_t *pm;
  char errbuf[200];
  const char *content;
  pm = backend_imageloader(remain, 0, NULL, errbuf, sizeof(errbuf));
  if(pm == NULL)
    return http_error(hc, 404, "Unable to load image %s : %s",
		      remain, errbuf);

  if(pm->pm_codec == CODEC_ID_NONE) {
    pixmap_release(pm);
    return http_error(hc, 404, 
		      "Unable to load image %s : Original data not available",
		      remain);
  }

  htsbuf_queue_init(&out, 0);
  htsbuf_append(&out, pm->pm_data, pm->pm_size);

  switch(pm->pm_codec) {
  case CODEC_ID_MJPEG:
    content = "image/jpeg";
    break;
  case CODEC_ID_PNG:
    content = "image/png";
    break;
  case CODEC_ID_GIF:
    content = "image/gif";
    break;
  default:
    content = "image";
    break;
  }

  pixmap_release(pm);

  return http_send_reply(hc, 0, content, NULL, NULL, 0, &out);
}


/**
 *
 */
void
httpcontrol_init(void)
{
  http_path_add("/control/open", NULL, hc_open);
  http_path_add("/image", NULL, hc_image);
}
