/*
 *  Copyright (C) 2014 Andreas Öman
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

#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "fileaccess/http_client.h"
#include "networking/asyncio.h"
#include "htsmsg/htsmsg.h"
#include "misc/minmax.h"
#include "misc/bytestream.h"
#include "bittorrent.h"
#include "bencode.h"


/**
 *
 */
static void
tracker_http_torrent_destroy(tracker_torrent_t *tt)
{
  if(tt->tt_http_req != NULL)
    asyncio_http_cancel(tt->tt_http_req);
  free(tt->tt_trackerid);
}


/**
 *
 */
static void
http_callback(http_req_aux_t *req, void *opaque)
{
  char errbuf[128];
  tracker_torrent_t *tt = opaque;
  torrent_t *to = tt->tt_torrent;
  htsmsg_t *msg;
  net_addr_t na;

  assert(tt->tt_http_req != NULL);
  tt->tt_http_req = NULL;

  buf_t *b = http_req_get_result(req);

  tt->tt_interval = MIN(3600, tt->tt_interval * 2);

  if(b != NULL) {
    msg = bencode_deserialize(buf_cstr(b),
                              buf_cstr(b) + buf_size(b),
                              errbuf, sizeof(errbuf), NULL, NULL, NULL);
    if(msg != NULL) {

      if(gconf.enable_torrent_tracker_debug) {
        TRACE(TRACE_DEBUG, "TRACKER", "%s: Decoded response:",
              tt->tt_tracker->t_url);
        htsmsg_print("TRACKER", msg);
      }

      const char *err = htsmsg_get_str(msg, "failure reason");
      if(err != NULL) {
        tracker_trace(tt->tt_tracker, "%s for %s", err, to->to_title);
        goto done;
      }

      const char *trackerid = htsmsg_get_str(msg, "trackerid");

      if(trackerid != NULL)
        mystrset(&tt->tt_trackerid, trackerid);

      tt->tt_interval =
        htsmsg_get_u32_or_default(msg, "min interval",
                                  htsmsg_get_u32_or_default(msg,
                                                            "interval", 1800));

      htsmsg_t *peers = htsmsg_get_list(msg, "peers");
      if(peers != NULL) {
        htsmsg_field_t *f;
        HTSMSG_FOREACH(f, peers) {
          htsmsg_t *sub = htsmsg_get_map_by_field(f);
          if(sub == NULL)
            continue;
          const char *ip = htsmsg_get_str(sub, "ip");
          if(ip == NULL)
            continue;

          if(net_resolve_numeric(ip, &na))
            continue;

          na.na_port = htsmsg_get_u32_or_default(sub, "port", 0);
          if(na.na_port == 0)
            continue;
          peer_add(to, &na);
        }
      }

      const void *data;
      size_t size;

      if(!htsmsg_get_bin(msg, "peers", &data, &size)) {
        net_addr_t na;
        na.na_family = 4;
        while(size >= 6) {
          memcpy(na.na_addr, data, 4);
          na.na_port = rd16_be(data + 4);
          if(na.na_port > 0)
            peer_add(to, &na);
          data += 6;
          size -= 6;
        }
      }
      htsmsg_release(msg);
    }
  }
 done:
  asyncio_timer_arm(&tt->tt_timer, async_now + tt->tt_interval * 1000000LL);
}


/**
 *
 */
static void
tracker_http_torrent_announce(tracker_torrent_t *tt, int event)
{
  tracker_t *t = tt->tt_tracker;
  torrent_t *to = tt->tt_torrent;
  const char *eventstr;
  int flags = 0;

  if(tt->tt_tentative)
    eventstr = "started";
  else if(event == 3)
    eventstr = "stopped";
  else
    eventstr = NULL;

  tt->tt_tentative = 0;

  if(tt->tt_http_req != NULL)
    asyncio_http_cancel(tt->tt_http_req);

  if(gconf.enable_torrent_tracker_debug)
    flags = FA_DEBUG;

  tt->tt_http_req =
    asyncio_http_req(t->t_url, http_callback, tt,
                     HTTP_ARGBIN("info_hash", to->to_info_hash, 20),
                     HTTP_ARGBIN("peer_id", btg.btg_peer_id, 20),
                     HTTP_ARGINT("port", 7898),
                     HTTP_ARGINT("compact", 1),
                     HTTP_ARGINT64("uploaded", to->to_uploaded_bytes),
                     HTTP_ARGINT64("downloaded", to->to_downloaded_bytes),
                     HTTP_ARGINT64("left", to->to_total_length ?: 16384),
                     HTTP_ARG("event", eventstr),
                     HTTP_ARG("trackerid", tt->tt_trackerid),
                     HTTP_RESULT_PTR(HTTP_BUFFER_INTERNALLY),
                     HTTP_FLAGS(flags),
                     NULL);

}



/**
 *
 */
tracker_t *
tracker_http_create(void)
{
  tracker_t *t = calloc(1, sizeof(tracker_t));
  t->t_announce = &tracker_http_torrent_announce;
  t->t_destroy = &tracker_http_torrent_destroy;
  return t;
}
