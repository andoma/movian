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
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "networking/http_server.h"
#include "htsmsg/htsmsg.h"
#include "bittorrent.h"

/**
 *
 */
static void
torrent_dump_files(const struct torrent_file_queue *q,
                   htsbuf_queue_t *out, int indent)
{
  const torrent_file_t *tf;
  TAILQ_FOREACH(tf, q, tf_parent_link) {

    if(tf->tf_size) {
      htsbuf_qprintf(out, "%*.s%s  [%"PRIu64" bytes @ %"PRIu64"]\n", indent, "",
             tf->tf_name, tf->tf_size, tf->tf_offset);
    } else {
      htsbuf_qprintf(out, "%*.s%s/\n", indent, "", tf->tf_name);
      torrent_dump_files(&tf->tf_files, out, indent + 2);
    }
  }
}


/**
 *
 */
static void
torrent_dump(const torrent_t *to, htsbuf_queue_t *q, int show_requests)
{
  char str[41];

  int64_t now = async_current_time();

  bin2hex(str, sizeof(str), to->to_info_hash, 20);

  htsbuf_qprintf(q, "Infohash: %s  %d pieces (%d in RAM using %d bytes) "
                 "refcount:%d\n", str,
                 to->to_num_pieces, to->to_num_active_pieces,
                 to->to_active_pieces_mem, to->to_refcount);
  htsbuf_qprintf(q, "\nOpen files\n");

  torrent_fh_t *tfh;
  LIST_FOREACH(tfh, &to->to_fhs, tfh_torrent_link) {
    htsbuf_qprintf(q, "%s\n", tfh->tfh_file->tf_fullpath);
  }

  htsbuf_qprintf(q, "\nFiles in torrent\n");
  torrent_dump_files(&to->to_root, q, 4);

  htsbuf_qprintf(q, "\n%d active peers out of %d known peers\n",
		 to->to_active_peers, to->to_num_peers);

  htsbuf_qprintf(q, "%-30s %-15s %7s  %6s Snd Rcv Queue  %10s %10s Delay Requests Cancels Waste  ConFail Discon ChokTim\n",
                 "Remote", "Status", "StTime", "Pieces", "Recv (kB)","Sent (kB)");

  const peer_t *p;
  LIST_FOREACH(p, &to->to_peers, p_link) {

    if(!(p->p_state_ == PEER_STATE_RUNNING ||
         p->p_state_ == PEER_STATE_WAIT_HANDSHAKE ||
         p->p_state_ == PEER_STATE_CONNECTING))
      continue;

    if(p->p_peer_choking && 0)
      continue;

    int num_have = p->p_num_pieces_have;

    htsbuf_qprintf(q, "%-30s %-15s %7ds %6d %3s %3s  %2d/%-2d %10d %10d %5d %8d %7d %6d %7d %6d %7d\n",
                   p->p_name,
                   peer_state_txt(p->p_state_),
                   (int)((now - p->p_state_change_time) / 1000000),
                   num_have,
                   (p->p_peer_interested && !p->p_am_choking) ? "Yes" :
                   (p->p_peer_interested) ? "Req" : "",
                   (p->p_am_interested && !p->p_peer_choking) ? "Yes" :
                   (p->p_am_interested) ? "Req" : "",
                   p->p_active_requests,
		   p->p_maxq,
                   p->p_bytes_received / 1000,
                   p->p_bytes_sent / 1000,
                   p->p_block_delay / 1000,
                   p->p_num_requests,
                   p->p_num_cancels,
                   p->p_num_waste,
		   p->p_connect_fail,
		   p->p_disconnected,
                   (int)((now - p->p_choked_time) / 1000000));
    
#if 0
    htsbuf_qprintf(q, "               delays: %5d %5d %5d %5d %5d %5d %5d %5d %5d %5d\n",
                   p->p_bd[0] / 1000,
                   p->p_bd[1] / 1000,
                   p->p_bd[2] / 1000,
                   p->p_bd[3] / 1000,
                   p->p_bd[4] / 1000,
                   p->p_bd[5] / 1000,
                   p->p_bd[6] / 1000,
                   p->p_bd[7] / 1000,
                   p->p_bd[8] / 1000,
                   p->p_bd[9] / 1000);
#endif

    if(show_requests) {

      const torrent_request_t *tr;
      LIST_FOREACH(tr, &p->p_download_requests, tr_peer_link) {

        char deadline[32];

        if(tr->tr_block == NULL) {
          snprintf(deadline, sizeof(deadline), "---");
        } else if(tr->tr_block->tb_piece->tp_deadline == INT64_MAX) {
          snprintf(deadline, sizeof(deadline), "Unset");
        } else {
          int64_t delta = tr->tr_block->tb_piece->tp_deadline - now;
          snprintf(deadline, sizeof(deadline), "%1.1fs", delta / 1000000.0);
        }

        htsbuf_qprintf(q, "  piece:%-4d offset:0x%-8x length:0x%-5x age:%2.2fs ETA:%-6d req:%d %s  piece deadline:%s\n",
                       tr->tr_piece, tr->tr_begin, tr->tr_length,
                       (int)(now - tr->tr_send_time) / 1000000.0f,
                       (int)((tr->tr_send_time + p->p_block_delay) - now) / 1000,
                       tr->tr_req_num,
                       tr->tr_block == NULL ? "ORPHANED " : "",
                       deadline);
      }
    }
  }
  int waiting_blocks = 0;
  int sent_blocks = 0;

  const torrent_piece_t *tp;
  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    const torrent_block_t *tb;
    LIST_FOREACH(tb, &tp->tp_waiting_blocks, tb_piece_link)
      waiting_blocks++;

    LIST_FOREACH(tb, &tp->tp_sent_blocks, tb_piece_link) {
      sent_blocks++;
#if 0
      htsbuf_qprintf(q, "Piece:%-5d offset:%-8d length:%-5d\n",
                     tb->tb_piece->tp_index, tb->tb_begin, tb->tb_length);
      const torrent_request_t *tr;
      LIST_FOREACH(tr, &tb->tb_requests, tr_block_link) {
        htsbuf_qprintf(q, "  Requested at %-30s age:%2.2fs est_delay:%d\n",
                       tr->tr_peer ? tr->tr_peer->p_name : "???",
                       (int)(async_now - tr->tr_send_time) / 1000000.0f,
                       tr->tr_est_delay);
      }
#endif
    }
  }

  htsbuf_qprintf(q, "%d request queued, %d in-flight\n",
                 waiting_blocks, sent_blocks);

  htsbuf_qprintf(q, "%"PRId64" bytes downloaded, %"PRId64" bytes wasted\n",
		 to->to_downloaded_bytes,
		 to->to_wasted_bytes);


}

static int
torrent_dump_http(http_connection_t *hc, const char *remain, void *opaque,
                  http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  const torrent_t *to;

  const int show_requests = atoi(http_arg_get_req(hc, "peerrequests") ?: "0");

  hts_mutex_lock(&bittorrent_mutex);

  LIST_FOREACH(to, &torrents, to_link)
    torrent_dump(to, &out, show_requests);

  hts_mutex_unlock(&bittorrent_mutex);

  return http_send_reply(hc, 0,
                         "text/plain; charset=utf-8", NULL, NULL, 0, &out);
}

/**
 *
 */
static void
torrent_stats_init(void)
{
  http_path_add("/api/torrents", NULL, torrent_dump_http, 1);
}

INITME(INIT_GROUP_API, torrent_stats_init, NULL, 0);
