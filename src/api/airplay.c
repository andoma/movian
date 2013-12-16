/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include "networking/http_server.h"
#include "airplay.h"
#include "event.h"



static int
airplay_reverse(http_connection_t *hc, const char *remain, void *opaque,
		   http_cmd_t method)
{
  struct http_header_list headers;
  LIST_INIT(&headers);

  http_header_add(&headers, "Connection", "Upgrade", 0);
  http_header_add(&headers, "Upgrade", "PTTH/1.0", 0);

  return http_send_raw(hc, 101, "Switching Protocols", &headers, NULL);
}


static int
airplay_scrub(http_connection_t *hc, const char *remain, void *opaque,
		   http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);
  htsbuf_qprintf(&out, 
		 "position: 0.123456\r\n"
		 "duration: 50.123456");

  return http_send_reply(hc, 0, NULL, NULL, NULL, 0, &out);
}


static int
airplay_play(http_connection_t *hc, const char *remain, void *opaque,
		http_cmd_t method)
{
  char *data = http_get_post_data(hc, NULL, 0);

  char *url = strstr(data, "Content-Location: ");
  char *startpos = strstr(data, "Start-Position: ");

  if(url == NULL)
    return 400;

  url += strlen("Content-Location: ");
  url[strcspn(url, "\r\n")] = 0;

  if(startpos != NULL) {
    startpos += strlen("Start-Position: ");
    startpos[strcspn(startpos, "\r\n")] = 0;
  }

  event_dispatch(event_create_openurl(url, NULL, NULL, NULL, NULL));
  return 200;
}


static int
airplay_rate(http_connection_t *hc, const char *remain, void *opaque,
		http_cmd_t method)
{


  
  return 200;
}



/**
 *
 */
void
airplay_init(void)
{
  http_path_add("/reverse", NULL, airplay_reverse, 1);
  http_path_add("/scrub", NULL, airplay_scrub, 1);
  http_path_add("/play", NULL, airplay_play, 1);
  http_path_add("/rate", NULL, airplay_rate, 1);
}

