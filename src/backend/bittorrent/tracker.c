/*
 *  Copyright (C) 2013 Andreas Öman
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
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "misc/bytestream.h"
#include "networking/http.h"

#include "bittorrent.h"

static int txid_gen;
static struct tracker_list trackers;
static asyncio_fd_t *tracker_udp_fd;




/**
 *
 */
static void
tracker_send_connect(tracker_t *t)
{
  static uint32_t idgen;

  idgen++;
  idgen ^= (showtime_get_ts() & 0xfffff000);
  t->tr_conn_txid = idgen | 0x80000000;

  t->t_state = TRACKER_STATE_CONNECTING;
  uint8_t hello[16];
  wr64_be(hello, 0x41727101980ULL);
  wr32_be(hello + 8, 0); // connect
  wr32_be(hello + 12, t->tr_conn_txid);

  asyncio_udp_send(tracker_udp_fd, hello, 16, &t->t_addr);

  int timeout = 15 * (1 << t->t_conn_attempt);
  asyncio_timer_arm(&t->t_timer, showtime_get_ts() + timeout * 1000000LL);
  t->t_conn_attempt++;
}


/**
 *
 */
static void
tracker_got_dns(void *opaque, int status, const void *data)
{
  tracker_t *t = opaque;

  hts_mutex_lock(&bittorrent_mutex);

  switch(status) {
  case ASYNCIO_DNS_STATUS_COMPLETED:
    t->t_addr = *(const net_addr_t *)data;
    t->t_addr.na_port = t->t_port;
    tracker_send_connect(t);
    break;

  case ASYNCIO_DNS_STATUS_FAILED:
    TRACE(TRACE_DEBUG, "BT", "Unable to resolve %s -- %s",
	  t->t_url, data);
    t->t_state = TRACKER_STATE_ERROR;
    break;

  default:
    abort();
  }
  
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
static void
tracker_timer_cb(void *aux)
{
  tracker_t *t = aux;
  hts_mutex_lock(&bittorrent_mutex);
  switch(t->t_state) {
  case TRACKER_STATE_CONNECTING:
    tracker_send_connect(t);
    break;
  default:
    break;
  }
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
tracker_t *
tracker_create(const char *url)
{
  tracker_t *t;
  hts_mutex_assert(&bittorrent_mutex);

  LIST_FOREACH(t, &trackers, t_link) {
    if(!strcmp(t->t_url, url))
      return t;
  }

  char protostr[16];
  char hostname[128];
  int port = -1;
  int proto;

  TRACE(TRACE_DEBUG, "BT", "Adding new tracker %s", url);
  url_split(protostr, sizeof(protostr), NULL, 0,
	    hostname, sizeof(hostname),
	    &port, NULL, 0, url);

  if(!strcmp(protostr, "udp")) {
    proto = TRACKER_PROTO_UDP;
    if(port == -1)
      port = 80;
  } else {
    return NULL;
  }

  t = calloc(1, sizeof(tracker_t));
  asyncio_timer_init(&t->t_timer, tracker_timer_cb, t);
  t->t_url = strdup(url);
  t->t_port = port;
  t->t_proto = proto;
  LIST_INSERT_HEAD(&trackers, t, t_link);
  asyncio_dns_lookup_host(hostname, tracker_got_dns, t);
  return t;
}




/**
 *
 */
static void
torrent_tracker_announce(torrent_tracker_t *tt, int event)
{
  uint8_t out[98];

  tracker_t *tr = tt->tt_tracker;
  const torrent_t *to = tt->tt_torrent;

  wr64_be(out + 0, tr->tr_conn_id);
  wr32_be(out + 8, 1); // Announce
  wr32_be(out + 12, tt->tt_txid);
  memcpy(out + 16, to->to_info_hash, 20);
  memcpy(out + 36, btc.btc_peer_id, 20);

  wr64_be(out + 56, to->to_downloaded_bytes);
  wr64_be(out + 64, to->to_remaining_bytes);
  wr64_be(out + 72, to->to_uploaded_bytes);
  wr32_be(out + 80, event);

  wr32_be(out + 92, -1);
  wr16_be(out + 96, 43213);
  asyncio_udp_send(tracker_udp_fd, out, 98, &tr->t_addr);
}


static void
torrent_tracker_periodic(void *aux)
{
  torrent_tracker_t *tt = aux;
  torrent_tracker_announce(tt, 0);
  asyncio_timer_arm(&tt->tt_timer, async_now + tt->tt_interval * 1000000LL);
}


/**
 *
 */
void
tracker_add_torrent(tracker_t *tr, torrent_t *to)
{
  torrent_tracker_t *tt = calloc(1, sizeof(torrent_tracker_t));
  tt->tt_interval = 60;
  tt->tt_tracker = tr;
  tt->tt_torrent = to;
  LIST_INSERT_HEAD(&to->to_trackers, tt, tt_torrent_link);
  LIST_INSERT_HEAD(&tr->t_torrents, tt, tt_tracker_link);
  tt->tt_txid = ++txid_gen;
  asyncio_timer_init(&tt->tt_timer, torrent_tracker_periodic, tt);
}


/**
 *
 */
static void
tracker_announce_all(tracker_t *tr)
{
  torrent_tracker_t *tt;

  LIST_FOREACH(tt, &tr->t_torrents, tt_tracker_link)
    torrent_tracker_announce(tt, 0);
}


/**
 *
 */
void
torrent_announce_all(torrent_t *to, int event)
{
  torrent_tracker_t *tt;

  LIST_FOREACH(tt, &to->to_trackers, tt_torrent_link)
    torrent_tracker_announce(tt, event);
}


/**
 *
 */
static void
tracker_udp_handle_connect_reply(tracker_t *tr, const uint8_t *data, int size)
{
  if(size < 16)
    return;

  uint32_t txid = rd32_be(data + 4);

  if(tr->tr_conn_txid != txid)
    return;
    
  tr->tr_conn_id = rd64_be(data + 8);
  TRACE(TRACE_DEBUG, "BT", "Connected to tracker %s", tr->t_url);
  asyncio_timer_disarm(&tr->t_timer);
  tr->t_state = TRACKER_STATE_CONNECTED;
  tracker_announce_all(tr);
}

/**
 *
 */
static void
tracker_udp_handle_announce_reply(tracker_t *tr, const uint8_t *data, int size)
{
  if(size < 20)
    return;

  uint32_t txid     = rd32_be(data + 4);

  torrent_tracker_t *tt;
  LIST_FOREACH(tt, &tr->t_torrents, tt_tracker_link)
    if(tt->tt_txid == txid)
      break;

  if(tt == NULL) {
    TRACE(TRACE_DEBUG, "BT", "Got announce reply for unknown torrent");
    return;
  }

  tt->tt_interval = rd32_be(data + 8);
  tt->tt_leechers = rd32_be(data + 12);
  tt->tt_seeders  = rd32_be(data + 16);

  size -= 20;
  data += 20;

  net_addr_t na;
  na.na_family = 4;

  while(size >= 6) {
    memcpy(na.na_addr, data, 4);
    na.na_port = rd16_be(data + 4);
    if(na.na_port > 0)
      torrent_add_peer(tt->tt_torrent, &na);
    data += 6;
    size -= 6;
  }
  asyncio_timer_arm(&tt->tt_timer, async_now + tt->tt_interval * 1000000LL);
}


/**
 *
 */
static void
tracker_udp_handle_input(const uint8_t *data, int size,
			 const net_addr_t *remote_addr)
{
  tracker_t *tr;
  const int cmd = rd32_be(data);

  LIST_FOREACH(tr, &trackers, t_link)
    if(!net_addr_cmp(&tr->t_addr, remote_addr))
      break;
  if(tr == NULL)
    return;

  TRACE(TRACE_DEBUG, "BT", "Got packet from %s", tr->t_url);

  switch(cmd) {
  case 0:
    tracker_udp_handle_connect_reply(tr, data, size);
    break;

  case 1:
    tracker_udp_handle_announce_reply(tr, data, size);
    break;

  }
}


/**
 *
 */
static void
tracker_udp_input(void *opaque, const void *data, int size,
		  const net_addr_t *remote_addr)
{
  if(size < 4)
    return;
  hts_mutex_lock(&bittorrent_mutex);
  tracker_udp_handle_input(data, size, remote_addr);
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
static void
trackers_init(void)
{
  uint32_t x = showtime_get_ts();
  for(int i = 0; i < 20; i++) {
    x = x * 1664525 + 1013904223;
    btc.btc_peer_id[i] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_."[x & 0x3f];
  }

  tracker_udp_fd = asyncio_udp_bind("bittorrent udp tracker",
				    0, tracker_udp_input, NULL, 0);
}

INITME(INIT_GROUP_ASYNCIO, trackers_init);
