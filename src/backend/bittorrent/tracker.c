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

static int tracker_new_torrent_signal;

struct tracker_list trackers;

void
tracker_trace(const tracker_t *t, const char *msg, ...)
{
  if(!gconf.enable_torrent_tracker_debug)
    return;

  va_list ap;
  char buf[256];
  va_start(ap, msg);
  vsnprintf(buf, sizeof(buf), msg, ap);
  va_end(ap);

  TRACE(TRACE_DEBUG, "TRACKER", "%s: %s", t->t_url, buf);
}


/**
 *
 */
static void
tracker_destroy(tracker_t *t)
{
  tracker_trace(t, "Destroyed");
  if(t->t_adr != NULL)
    asyncio_dns_cancel(t->t_adr);

  asyncio_timer_disarm(&t->t_timer);
  LIST_REMOVE(t, t_link);
  free(t->t_url);
  free(t);
}


/**
 *
 */
void
tracker_torrent_destroy(tracker_torrent_t *tt)
{
  if(tt->tt_tracker->t_destroy != NULL)
    tt->tt_tracker->t_destroy(tt);

  asyncio_timer_disarm(&tt->tt_timer);

  if(tt->tt_torrent != NULL)
    LIST_REMOVE(tt, tt_torrent_link);

  LIST_REMOVE(tt, tt_tracker_link);
  tracker_t *t = tt->tt_tracker;
  if(LIST_FIRST(&t->t_torrents) == NULL)
    tracker_destroy(t);

  free(tt);
}




/**
 *
 */
void
tracker_remove_torrent(torrent_t *to)
{
  tracker_torrent_t *tt;

  while((tt = LIST_FIRST(&to->to_trackers)) != NULL) {

    if(tt->tt_tracker->t_state == TRACKER_STATE_CONNECTED) {
      tt->tt_tracker->t_announce(tt, 3);
      LIST_REMOVE(tt, tt_torrent_link);
      tt->tt_torrent = NULL;
    } else {
      tracker_torrent_destroy(tt);
    }
  }
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

  url_split(protostr, sizeof(protostr), NULL, 0,
	    hostname, sizeof(hostname),
	    &port, NULL, 0, url);

  if(!strcmp(protostr, "udp")) {
    if(port == -1)
      port = 6969;

    t = tracker_udp_create(hostname, port);
  } else if(!strcmp(protostr, "http") || !strcmp(protostr, "https")) {
    t = tracker_http_create();

  } else {
    return NULL;
  }

  t->t_url = strdup(url);
  LIST_INSERT_HEAD(&trackers, t, t_link);
  tracker_trace(t, "New tracker added");
  return t;
}



/**
 * This timer callback is used either to reannounce periodically
 * or to un-announce (remove us) from tracker.
 * The latter is indicated when tt->tt_torrent no longer is pointed to
 */
static void
tracker_torrent_periodic(void *aux)
{
  tracker_torrent_t *tt = aux;

  if(tt->tt_torrent == NULL) {
    tt->tt_attempt++;
    if(tt->tt_attempt == 5) {
      tracker_torrent_destroy(tt);
    } else {
      // Resend stop request
      asyncio_timer_arm_delta_sec(&tt->tt_timer, 5);
    }
    return;
  }

  tt->tt_tracker->t_announce(tt, 2);
}


/**
 *
 */
void
tracker_add_torrent(tracker_t *tr, torrent_t *to)
{
  tracker_torrent_t *tt = calloc(1, sizeof(tracker_torrent_t));
  tt->tt_interval = 15;
  tt->tt_tracker = tr;
  tt->tt_torrent = to;
  tt->tt_tentative = 1;
  LIST_INSERT_HEAD(&to->to_trackers, tt, tt_torrent_link);
  LIST_INSERT_HEAD(&tr->t_torrents, tt, tt_tracker_link);
  asyncio_timer_init(&tt->tt_timer, tracker_torrent_periodic, tt);
  asyncio_wakeup_worker(tracker_new_torrent_signal);
}



/**
 *
 */
static void
tracker_new_torrent(void)
{
  tracker_t *t;
  tracker_torrent_t *tt;

  LIST_FOREACH(t, &trackers, t_link)
    LIST_FOREACH(tt, &t->t_torrents, tt_tracker_link)
      if(tt->tt_tentative)
        t->t_announce(tt, 2);
}



/**
 *
 */
void
torrent_announce_all(torrent_t *to)
{
  tracker_torrent_t *tt;

  LIST_FOREACH(tt, &to->to_trackers, tt_torrent_link)
    tt->tt_tracker->t_announce(tt, 2);
}


/**
 *
 */
static void
tracker_init(void)
{
  uint32_t x = arch_get_ts();
  for(int i = 0; i < 20; i++) {
    x = x * 1664525 + 1013904223;
    btg.btg_peer_id[i] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_."[x & 0x3f];
  }

  tracker_new_torrent_signal = asyncio_add_worker(tracker_new_torrent);
}

INITME(INIT_GROUP_ASYNCIO, tracker_init, NULL, 0);
