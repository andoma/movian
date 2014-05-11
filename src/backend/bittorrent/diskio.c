#if 0
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
#include <netinet/in.h>

#include "showtime.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "misc/sha.h"
#include "misc/bytestream.h"
#include "htsmsg/htsmsg.h"
#include "bittorrent.h"

static int torrent_write_thread_running;
static hts_thread_t torrent_read_thread;



/**
 *
 */
static void
torrent_write_to_disk(torrent_t *to, torrent_piece_t *tp)
{
  torrent_file_t *tf;

  uint64_t piece_offset = tp->tp_index * to->to_piece_length;
  

  TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link) {
    

  }

}

/**
 *
 */
static void *
bt_write_thread(void *aux)
{
  torrent_t *to;

  hts_mutex_lock(&bittorrent_mutex);

  while(1) {

  restart:

    LIST_FOREACH(to, &torrents, to_link) {
      torrent_piece_t *tp;
      TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
	if(tp->tp_hash_ok && !tp->tp_on_disk && !tp->tp_disk_fail) {
	  torrent_write_to_disk(to, tp);
	  goto restart;
	}
      }
    }

    if(hts_cond_wait_timeout(&torrent_piece_hashed_cond,
			     &bittorrent_mutex, 60000))
      break;
  }

  torrent_hash_thread_running = 0;
  hts_mutex_unlock(&bittorrent_mutex);
  return NULL;
}


/**
 *
 */
static void
bt_wakeup_write_thread(void)
{
  if(!torrent_write_thread_running) {
    torrent_write_thread_running = 1;
    hts_thread_create_detached("btwriteer", bt_write_thread, NULL,
			       THREAD_PRIO_BGTASK);
  }
}

#endif
