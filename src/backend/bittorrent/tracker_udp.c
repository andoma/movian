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
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "misc/bytestream.h"
#include "networking/http.h"

#include "bittorrent.h"

static int txid_gen;
static asyncio_fd_t *tracker_udp_fd;

/**
 *
 */
static void
tracker_udp_send_connect(tracker_t *t)
{
  static uint32_t idgen;

  idgen++;
  idgen ^= (arch_get_ts() & 0xfffff000);
  t->t_conn_txid = idgen | 0x80000000;

  t->t_state = TRACKER_STATE_CONNECTING;
  uint8_t hello[16];
  wr64_be(hello, 0x41727101980ULL);
  wr32_be(hello + 8, 0); // connect
  wr32_be(hello + 12, t->t_conn_txid);

  asyncio_udp_send(tracker_udp_fd, hello, 16, &t->t_addr);

  int timeout = 15 * (1 << t->t_conn_attempt);
  asyncio_timer_arm_delta_sec(&t->t_timer, timeout);
  t->t_conn_attempt++;
  tracker_trace(t,
                "Sending connect to %s (attempt:%d txid:0x%08x timeout: %ds)",
                t->t_url, t->t_conn_attempt, t->t_conn_txid, timeout);
}


/**
 *
 */
static void
tracker_udp_got_dns(void *opaque, int status, const void *data)
{
  tracker_t *t = opaque;

  t->t_adr = NULL;

  hts_mutex_lock(&bittorrent_mutex);

  switch(status) {
  case ASYNCIO_DNS_STATUS_COMPLETED:
    t->t_addr = *(const net_addr_t *)data;
    t->t_addr.na_port = t->t_port;
    tracker_trace(t, "DNS resolved to %s", net_addr_str(&t->t_addr));
    tracker_udp_send_connect(t);
    break;

  case ASYNCIO_DNS_STATUS_FAILED:
    tracker_trace(t, "Unable to resolve DNS: %s", (const char *)data);
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
tracker_udp_timer_cb(void *aux)
{
  tracker_t *t = aux;
  hts_mutex_lock(&bittorrent_mutex);
  switch(t->t_state) {
  case TRACKER_STATE_CONNECTING:
    tracker_udp_send_connect(t);
    break;
  default:
    break;
  }
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
static void
tracker_udp_torrent_announce(tracker_torrent_t *tt, int event)
{
  uint8_t out[98] = {0};
  tracker_t *t = tt->tt_tracker;
  const torrent_t *to = tt->tt_torrent;

  if(t->t_state != TRACKER_STATE_CONNECTED)
    return;

  tt->tt_tentative = 0;

  tt->tt_txid = ++txid_gen;

  tracker_trace(t, "Sending annouce for \"%s\" event:%d txid:0x%x",
                to->to_title,
                event, tt->tt_txid);

  wr64_be(out + 0, t->t_conn_id);
  wr32_be(out + 8, 1); // Announce
  wr32_be(out + 12, tt->tt_txid);
  memcpy(out + 16, to->to_info_hash, 20);
  memcpy(out + 36, btg.btg_peer_id, 20);

  wr64_be(out + 56, to->to_downloaded_bytes);
  wr64_be(out + 64, to->to_total_length ?: 16384);
  wr64_be(out + 72, to->to_uploaded_bytes);
  wr32_be(out + 80, event);

  wr32_be(out + 92, -1);
  wr16_be(out + 96, 43213);
  asyncio_udp_send(tracker_udp_fd, out, 98, &t->t_addr);
  asyncio_timer_arm_delta_sec(&tt->tt_timer, tt->tt_interval);
}


/**
 *
 */
static void
tracker_udp_announce_all(tracker_t *t)
{
  tracker_torrent_t *tt;

  LIST_FOREACH(tt, &t->t_torrents, tt_tracker_link)
    if(tt->tt_torrent != NULL)
      tracker_udp_torrent_announce(tt, 2);
}


/**
 *
 */
static void
tracker_udp_handle_connect_reply(tracker_t *t, const uint8_t *data, int size)
{
  if(size < 16)
    return;

  uint32_t txid = rd32_be(data + 4);

  if(t->t_conn_txid != txid)
    return;

  t->t_conn_attempt = 0;
  t->t_conn_id = rd64_be(data + 8);
  tracker_trace(t, "Connected to tracker");
  asyncio_timer_disarm(&t->t_timer);
  t->t_state = TRACKER_STATE_CONNECTED;
  tracker_udp_announce_all(t);
}

/**
 *
 */
static void
tracker_udp_handle_announce_reply(tracker_t *tr, const uint8_t *data, int size)
{
  if(size < 20)
    return;

  uint32_t txid = rd32_be(data + 4);

  tracker_torrent_t *tt;
  LIST_FOREACH(tt, &tr->t_torrents, tt_tracker_link)
    if(tt->tt_txid == txid)
      break;

  if(tt == NULL) {
    tracker_trace(tr, "Got announce reply for unknown torrent, ignoring");
    return;
  }

  tt->tt_interval = rd32_be(data + 8);
  tt->tt_leechers = rd32_be(data + 12);
  tt->tt_seeders  = rd32_be(data + 16);

  torrent_t *to = tt->tt_torrent;

  if(to == NULL) {
    // We have successfully stopped our announcement, this is the end
    tracker_torrent_destroy(tt);
    return;
  }

  tracker_trace(tr, "Got announce reply for \"%s\" (leechers:%d seeders:%d), "
                "refresh in %d seconds",
                to->to_title,
                tt->tt_leechers,
                tt->tt_seeders,
                tt->tt_interval);

  size -= 20;
  data += 20;

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
  asyncio_timer_arm_delta_sec(&tt->tt_timer, tt->tt_interval);
}


/**
 * Error, we need to reconnect
 */
static void
tracker_udp_handle_error(tracker_t *tr, const uint8_t *data, int size)
{
  if(size < 8)
    return;

  uint32_t txid = rd32_be(data + 4);

  tracker_torrent_t *tt;
  LIST_FOREACH(tt, &tr->t_torrents, tt_tracker_link)
    if(tt->tt_txid == txid)
      break;

  if(tt == NULL)
    return; // Error does not respond to our request

  const torrent_t *to = tt->tt_torrent;
  if(to == NULL) {
    tracker_torrent_destroy(tt);
    return;
  }

  size -= 8;
  data += 8;
  rstr_t *errmsg = rstr_allocl((const char *)data, size);
  tracker_trace(tr, "Got error for \"%s\" (%s) reconnecting",
                to->to_title,
                rstr_get(errmsg));
  rstr_release(errmsg);

  tracker_udp_send_connect(tr);
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
  tracker_trace(tr, "Got packet (command 0x%x)", cmd);

  switch(cmd) {
  case 0:
    tracker_udp_handle_connect_reply(tr, data, size);
    break;

  case 1:
    tracker_udp_handle_announce_reply(tr, data, size);
    break;

  case 3:
  case 0x3000000: // Some trackers forgot to htonl() this command
    tracker_udp_handle_error(tr, data, size);
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
tracker_t *
tracker_udp_create(const char *hostname, int port)
{
  tracker_t *t = calloc(1, sizeof(tracker_t));
  asyncio_timer_init(&t->t_timer, tracker_udp_timer_cb, t);
  t->t_port = port;
  t->t_adr = asyncio_dns_lookup_host(hostname, tracker_udp_got_dns, t);
  t->t_announce = &tracker_udp_torrent_announce;
  return t;
}


/**
 *
 */
static void
tracker_udp_init(void)
{
  tracker_udp_fd = asyncio_udp_bind("bittorrent udp tracker",
				    0, tracker_udp_input, NULL, 0, 0);
}

INITME(INIT_GROUP_ASYNCIO, tracker_udp_init, NULL, 0);
