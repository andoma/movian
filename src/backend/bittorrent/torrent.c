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
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "networking/http.h"

#include "bittorrent.h"

bt_config_t btc;
hts_mutex_t bittorrent_mutex;
static struct torrent_list torrents;
static int active_peers;


torrent_t *
torrent_create(const uint8_t *info_hash, const char *title,
	       const char **trackers)
{
  torrent_t *to;
  torrent_tracker_t *tt;

  hts_mutex_lock(&bittorrent_mutex);

  LIST_FOREACH(to, &torrents, to_link) {
    if(!memcmp(to->to_info_hash, info_hash, 20))
      break;
  }

  if(to == NULL) {
    to = calloc(1, sizeof(torrent_t));
    memcpy(to->to_info_hash, info_hash, 20);
    LIST_INSERT_HEAD(&torrents, to, to_link);
    TAILQ_INIT(&to->to_inactive_peers);
  }
  to->to_refcount++;

  for(;*trackers; trackers++) {
    LIST_FOREACH(tt, &to->to_trackers, tt_torrent_link) {
      if(!strcmp(tt->tt_tracker->t_url, *trackers))
	break;
    }

    if(tt == NULL) {
      tracker_t *tr = tracker_create(*trackers);
      if(tr != NULL)
	tracker_add_torrent(tr, to);
    }
  }

  
  hts_mutex_unlock(&bittorrent_mutex);
  return to;
}


/**
 *
 */
static void
peer_error_cb(void *opaque, const char *error)
{
  peer_t *p = opaque;

  if(error == NULL) {
    printf("Connected to %s\n", net_addr_str(&p->p_addr));

    p->p_am_choking = 1;
    p->p_am_interested = 0;
    p->p_peer_choking = 1;
    p->p_peer_interested = 0;

    return;
  }

  printf("%s: %s\n", net_addr_str(&p->p_addr), error);
  asyncio_del_fd(p->p_connection);
  p->p_connection = NULL;
  return;
}


/**
 *
 */
static void
peer_read_cb(void *opaque, htsbuf_queue_t *q)
{
  peer_t *p = opaque;

  printf("%s: Got input data %d bytes\n", net_addr_str(&p->p_addr),q->hq_size);


}


/**
 *
 */
static void
peer_connect(peer_t *p)
{
  torrent_t *to = p->p_torrent;

  assert(p->p_connection == NULL);

  p->p_state = PEER_STATE_CONNECTING;

  to->to_active_peers++;
  active_peers++;

  printf("Connecting to %s\n", net_addr_str(&p->p_addr));

  char name[64];
  snprintf(name, sizeof(name), "BT Peer %s", net_addr_str(&p->p_addr));

  p->p_connection = asyncio_connect(name, &p->p_addr,
				    peer_error_cb,
				    peer_read_cb,
				    p, 5000);
}


/**
 *
 */
void
torrent_add_peer(torrent_t *to, const net_addr_t *na)
{
  peer_t *p;
  LIST_FOREACH(p, &to->to_peers, p_link)
    if(!net_addr_cmp(&p->p_addr, na))
      return; // Already know about peer

  p = calloc(1, sizeof(peer_t));
  p->p_addr = *na;
  p->p_torrent = to;

  LIST_INSERT_HEAD(&to->to_peers, p, p_link);
  
  if(to->to_active_peers >= btc.btc_max_peers_torrent ||
     active_peers        >= btc.btc_max_peers_global) {

    p->p_state = PEER_STATE_INACTIVE;
    TAILQ_INSERT_TAIL(&to->to_inactive_peers, p, p_queue_link);
    return;
  }
  

  peer_connect(p);
}


/**
 *
 */
static void
torrent_init(void)
{
  hts_mutex_init(&bittorrent_mutex);

}

INITME(INIT_GROUP_ASYNCIO, torrent_init);
