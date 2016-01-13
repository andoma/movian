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
#include "misc/sha.h"
#include "misc/bytestream.h"
#include "networking/http.h"
#include "htsmsg/htsmsg.h"
#include "bittorrent.h"
#include "misc/minmax.h"
#include "bencode.h"

#define BT_MSGID_CHOKE          0x0
#define BT_MSGID_UNCHOKE        0x1
#define BT_MSGID_INTERESTED     0x2
#define BT_MSGID_NOT_INTERESTED 0x3
#define BT_MSGID_HAVE           0x4
#define BT_MSGID_BITFIELD       0x5
#define BT_MSGID_REQUEST        0x6
#define BT_MSGID_PIECE          0x7
#define BT_MSGID_CANCEL         0x8
#define BT_MSGID_PORT           0x9
#define BT_MSGID_HAVE_ALL       0xe
#define BT_MSGID_HAVE_NONE      0xf
#define BT_MSGID_REJECT         0x10
#define BT_MSGID_ALLOWED_FAST   0x11
#define BT_MSGID_EXTENSION      0x14

#define EXTENSION_MSGID_HANDSHAKE 0
#define EXTENSION_MSGID_METADATA  2

static void peer_shutdown(peer_t *p, int next_state, int resched);

static void peer_cancel_orphaned_requests(peer_t *p, torrent_request_t *skip);

static void peer_read_cb(void *opaque, htsbuf_queue_t *q);

static void peer_send_msgid(peer_t *p, int msgid);

static void peer_send_cancel(peer_t *p, const torrent_request_t *tr);

static void peer_parse_extension(peer_t *p, htsmsg_t *msg, uint8_t msgid,
                                 const uint8_t *data, size_t len);

static void peer_send_extension_handshake(peer_t *p);


#define PEER_DBG_CONN     0x1
#define PEER_DBG_DOWNLOAD 0x2
#define PEER_DBG_UPLOAD   0x4

static void
peer_trace(const peer_t *p, int type, const char *msg, ...)
  attribute_printf(3, 4);

static void
peer_trace(const peer_t *p, int type, const char *msg, ...)
{
  switch(type) {
  case PEER_DBG_CONN:
    if(!gconf.enable_torrent_peer_connection_debug)
      return;
    break;
  case PEER_DBG_DOWNLOAD:
    if(!gconf.enable_torrent_peer_download_debug)
      return;
    break;
  case PEER_DBG_UPLOAD:
    if(!gconf.enable_torrent_peer_upload_debug)
      return;
    break;
  }

  va_list ap;
  char buf[256];
  va_start(ap, msg);
  vsnprintf(buf, sizeof(buf), msg, ap);
  va_end(ap);

  TRACE(TRACE_DEBUG, "BITTORRENT", "%s: %s", p->p_name, buf);
}


static void
peer_set_state(peer_t *p, int state)
{
  p->p_state_ = state;
  p->p_state_change_time = async_current_time();
}


static const char *peer_state_tab[] = {
  [PEER_STATE_INACTIVE]       = "Inactive",
  [PEER_STATE_CONNECTING]     = "Connecting",
  [PEER_STATE_CONNECT_FAIL]   = "Connect fail",
  [PEER_STATE_WAIT_HANDSHAKE] = "Wait Handshake",
  [PEER_STATE_RUNNING]        = "Running",
};


/**
 *
 */
const char *
peer_state_txt(unsigned int state)
{
  if(state >= PEER_STATE_num)
    return "???";
  return peer_state_tab[state];
}


/**
 *
 */
static void
peer_arm_ka_timer(peer_t *p)
{
  asyncio_timer_arm_delta_sec(&p->p_ka_send_timer, 60);
}


/**
 *
 */
static void
send_handshake(peer_t *p)
{
  const torrent_t *to = p->p_torrent;
  uint8_t handshake[1 + 19 + 8 + 20 + 20];

  handshake[0] = 19;
  strcpy((char *)handshake + 1, "BitTorrent protocol");

  uint8_t *reserved = handshake + 1 + 19;
  memset(reserved, 0, 8);
  reserved[7] = 0x04;
  reserved[5] = 0x10;

  memcpy(handshake + 1 + 19 + 8, to->to_info_hash, 20);
  memcpy(handshake + 1 + 19 + 8 + 20, btg.btg_peer_id, 20);
  asyncio_send(p->p_connection, handshake, sizeof(handshake), 0);
  peer_arm_ka_timer(p);
}


/**
 *
 */
static void
peer_send_keepalive(void *aux)
{
  peer_t *p = aux;
  uint8_t buf[4] = {0};
  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}


/**
 *
 */
static void
peer_abort_requests(peer_t *p)
{
  torrent_request_t *tr;
  torrent_sendreq_t *ts;

  if(p->p_state_ != PEER_STATE_RUNNING)
    return;

  asyncio_timer_disarm(&p->p_data_recv_timer);

  if(LIST_FIRST(&p->p_download_requests) != NULL)
    p->p_torrent->to_peers_with_outstanding_requests--;

  while((ts = TAILQ_FIRST(&p->p_sendreqs)) != NULL) {
    torrent_sendreq_destroy(ts);
  }

  while((tr = LIST_FIRST(&p->p_download_requests)) != NULL) {
    torrent_block_t *tb = tr->tr_block;

    assert(tr->tr_peer == p);

    LIST_REMOVE(tr, tr_peer_link);

    if(tb != NULL) {

      LIST_REMOVE(tr, tr_block_link);
      if(LIST_FIRST(&tb->tb_requests) == NULL) {
        torrent_piece_t *tp = tb->tb_piece;
        // Put block back on waiting list
        LIST_REMOVE(tb, tb_piece_link);
        LIST_INSERT_HEAD(&tp->tp_waiting_blocks, tb, tb_piece_link);
      }
    }
    free(tr);
  }
  p->p_active_requests = 0;
}




/**
 *
 */
static void
peer_connect_cb(void *opaque, const char *error)
{
  peer_t *p = opaque;

  hts_mutex_lock(&bittorrent_mutex);

  if(error == NULL) {
    p->p_am_choking = 1;
    p->p_am_interested = 0;
    p->p_peer_choking = 1;
    p->p_peer_interested = 0;

    send_handshake(p);
    asyncio_set_timeout_delta_sec(p->p_connection, 15);
    peer_set_state(p, PEER_STATE_WAIT_HANDSHAKE);
    p->p_choked_time = p->p_state_change_time;
    peer_trace(p, PEER_DBG_CONN, "Connected");
  } else {

    peer_trace(p, PEER_DBG_CONN, "%s in state %s",
               error, peer_state_txt(p->p_state_));

    peer_shutdown(p, p->p_state_ == PEER_STATE_RUNNING ?
                  PEER_STATE_DISCONNECTED :
                  PEER_STATE_CONNECT_FAIL, 1);
  }
  hts_mutex_unlock(&bittorrent_mutex);

}


/**
 *
 */
static void
peer_destroy_all_metainfo_requests(peer_t *p)
{
  metainfo_request_t *mr;
  if(LIST_FIRST(&p->p_metainfo_requests) == NULL)
    return;

  while((mr = LIST_FIRST(&p->p_metainfo_requests)) != NULL) {
    LIST_REMOVE(mr, mr_peer_link);
    LIST_REMOVE(mr, mr_query_link);
    free(mr->mr_data);
    free(mr);
  }
  hts_cond_broadcast(&torrent_metainfo_available_cond);
}


/**
 *
 */
static void
peer_free_pieces(peer_t *p)
{
  piece_peer_t *pp;
  while((pp = LIST_FIRST(&p->p_pieces)) != NULL)
    torrent_piece_peer_destroy(pp);
}


/**
 *
 */
static void
peer_destroy_request(torrent_request_t *tr)
{
  peer_t *p = tr->tr_peer;
  assert(p->p_active_requests > 0);
  p->p_active_requests--;
  LIST_REMOVE(tr, tr_peer_link);

  if(LIST_FIRST(&p->p_download_requests) == NULL)
    p->p_torrent->to_peers_with_outstanding_requests--;

  free(tr);
}


/**
 *
 */
static void
peer_shutdown(peer_t *p, int next_state, int resched)
{
  torrent_t *to = p->p_torrent;

  if(p->p_state_ != PEER_STATE_INACTIVE) {
    to->to_active_peers--;
    btg.btg_active_peers--;
    if(resched)
      torrent_attempt_more_peers(to);
  }

  if(p->p_connection != NULL) {
    asyncio_del_fd(p->p_connection);
    p->p_connection = NULL;
  }

  if(p->p_pending_bitfield != NULL) {
    free(p->p_pending_bitfield);
    p->p_pending_bitfield = NULL;
  }

  asyncio_timer_disarm(&p->p_ka_send_timer);
  asyncio_timer_disarm(&p->p_data_recv_timer);

  free(p->p_piece_flags);
  p->p_piece_flags = NULL;

  // Do stuff depending on current (old) state

  switch(p->p_state_) {

  case PEER_STATE_DISCONNECTED:
    TAILQ_REMOVE(&to->to_disconnected_peers, p, p_queue_link);
    break;

  case PEER_STATE_CONNECT_FAIL:
    TAILQ_REMOVE(&to->to_connect_failed_peers, p, p_queue_link);
    break;

  case PEER_STATE_INACTIVE:
    TAILQ_REMOVE(&to->to_inactive_peers, p, p_queue_link);
    break;

  case PEER_STATE_RUNNING:
    LIST_REMOVE(p, p_running_link);
    peer_destroy_all_metainfo_requests(p);
    if(p->p_peer_choking == 0) {
      LIST_REMOVE(p, p_unchoked_link);
      p->p_peer_choking = 1;
    }
    break;

  case PEER_STATE_WAIT_HANDSHAKE:
  case PEER_STATE_CONNECTING:
    break;

  default:
    printf("Cant shutdown peer in state %d\n", p->p_state_);
    abort();
  }

  peer_abort_requests(p);

  peer_set_state(p, next_state);

  // Do stuff depending on next (new) state
  switch(p->p_state_) {
  default:
    printf("Can't shutdown peer to state %d\n", p->p_state_);
    abort();

  case PEER_STATE_CONNECT_FAIL:
    p->p_fail_time = async_current_time();
    p->p_connect_fail++;
    if(p->p_connect_fail == 5)
      goto destroy;
    TAILQ_INSERT_TAIL(&to->to_connect_failed_peers, p, p_queue_link);
    break;

  case PEER_STATE_DISCONNECTED:
    p->p_fail_time = async_current_time();
    p->p_disconnected++;
    if(p->p_disconnected == 5)
      goto destroy;
    TAILQ_INSERT_TAIL(&to->to_disconnected_peers, p, p_queue_link);
    break;

  case PEER_STATE_DESTROYED:
  destroy:
    LIST_REMOVE(p, p_link);
    free(p->p_name);
    to->to_num_peers--;
    peer_free_pieces(p);
    free(p);
    break;
  }
  if(resched)
    torrent_io_do_requests(to);
}

/**
 *
 */
static void
peer_disconnect(peer_t *p, const char *fmt, ...)
{
  va_list ap;

  char buf[512];

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  peer_trace(p, PEER_DBG_CONN, "Disconnected by us: %s", buf);
  peer_shutdown(p, PEER_STATE_DESTROYED, 1);
}


/**
 *
 */
static void
send_initial_set(peer_t *p)
{
  torrent_t *to = p->p_torrent;

  int bitfield_len = (to->to_num_pieces + 7) / 8;
  int something = 0;
  uint8_t *bitfield = calloc(1, bitfield_len);

  torrent_piece_t *tp;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(!tp->tp_hash_ok)
      continue;

    int i = tp->tp_index;

    bitfield[i / 8] |= 0x80 >> (i & 0x7);
    something = 1;
  }

  for(int i = 0; i < to->to_num_pieces; i++) {
    if(to->to_cachefile_piece_map[i] != -1) {
      bitfield[i / 8] |= 0x80 >> (i & 0x7);
      something = 1;
    }
  }

  if(something) {
    uint8_t buf[5] = {0,0,0,0,BT_MSGID_BITFIELD};
    wr32_be(buf, bitfield_len + 1);
    asyncio_send(p->p_connection, buf, sizeof(buf), 1);
    asyncio_send(p->p_connection, bitfield, bitfield_len, 0);
  }

  free(bitfield);
}


/**
 *
 */
static int
recv_handshake(peer_t *p, htsbuf_queue_t *q)
{
  uint8_t msg[1 + 19 + 8 + 20 + 20];

  if(htsbuf_peek(q, msg, sizeof(msg)) != sizeof(msg))
    return 1;

  htsbuf_drop(q, sizeof(msg));

  if(msg[0] != 19 || memcmp(msg+1, "BitTorrent protocol", 19)) {
    peer_disconnect(p, "Wrong protocol");
    return 1;
  }

  const uint8_t *reserved = msg + 1 + 19;

  if(reserved[7] & 0x4)
    p->p_fast_ext = 1;
  if(reserved[5] & 0x10)
    p->p_ext_prot = 1;

  if(memcmp(msg + 1 + 19 + 8, p->p_torrent->to_info_hash, 20)) {
    peer_disconnect(p, "Invalid info hash");
    return 1;
  }

  memcpy(p->p_id, msg + 1 + 19 + 8 + 20, 20);
  p->p_id[20] = 0;

  peer_trace(p, PEER_DBG_CONN, "Handshake received%s%s",
             p->p_fast_ext ? ", Fast extensions" : "",
             p->p_ext_prot ? ", Extension Protocol" : "");

  send_initial_set(p);
  if(p->p_ext_prot)
    peer_send_extension_handshake(p);
  return 0;
}


/**
 *
 */
static void
peer_have_piece(peer_t *p, uint32_t pid)
{
  if(p->p_piece_flags[pid] & PIECE_HAVE)
    return;
  p->p_piece_flags[pid] |= PIECE_HAVE;
  p->p_num_pieces_have++;
  assert(p->p_num_pieces_have <= p->p_torrent->to_num_pieces);
}


/**
 *
 */
static int
parse_bitfield(peer_t *p, const uint8_t *data, unsigned int len)
{
  torrent_t *to = p->p_torrent;

  if((to->to_num_pieces + 7) / 8 != len)
    return 1;

  if(p->p_piece_flags == NULL)
    p->p_piece_flags = calloc(1, to->to_num_pieces);

  p->p_num_pieces_have = 0;
  for(int i = 0; i < to->to_num_pieces; i++) {
    if(data[i / 8] & (0x80 >> (i & 0x7)))
      peer_have_piece(p, i);
  }
  peer_update_interest(to, p);
  if(p->p_peer_choking == 0)
    torrent_io_do_requests(to);
  return 0;
}


/**
 *
 */
static int
recv_bitfield(peer_t *p, const uint8_t *data, unsigned int len)
{
  if(len == 0 || len > 8192)
    return 0;

  torrent_t *to = p->p_torrent;

  if(to->to_metainfo == NULL) {

    free(p->p_pending_bitfield);
    p->p_pending_bitfield = malloc(len);
    memcpy(p->p_pending_bitfield, data, len);
    p->p_pending_bitfield_size = len;
    peer_trace(p, PEER_DBG_CONN, "Initial bitfield message stashed");
    return 0;
  }

  if(parse_bitfield(p, data, len)) {
    peer_disconnect(p, "Invalid 'bitfield' length got:%d, pieces:%d",
                    len, to->to_num_pieces);
    return 1;
  }
  return 0;

}


/**
 *
 */
static int
recv_have(peer_t *p, const uint8_t *data, unsigned int len)
{
  torrent_t *to = p->p_torrent;

  if(len != 4) {
    peer_disconnect(p, "Invalid 'have' length: %d", len);
    return 1;
  }

  unsigned int pid =
    (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

  if(to->to_metainfo == NULL) {

    if(p->p_pending_bitfield != NULL) {
      if(pid / 8 < p->p_pending_bitfield_size)
        p->p_pending_bitfield[pid / 8] |= 0x80 >> (pid & 0x7);
    }

    return 0;
  }

  if(pid >= to->to_num_pieces) {
    peer_disconnect(p, "Excessive piece index %d / %d", pid, to->to_num_pieces);
    return 1;
  }

  if(p->p_piece_flags == NULL)
    p->p_piece_flags = calloc(1, to->to_num_pieces);

  peer_have_piece(p, pid);

  peer_update_interest(to, p);
  if(p->p_peer_choking == 0)
    torrent_io_do_requests(to);
  return 0;
}


/**
 *
 */
static void
recv_have_all(peer_t *p)
{
  torrent_t *to = p->p_torrent;

  if(to->to_metainfo == NULL) {
    p->p_pending_have_all = 1;
    return;
  }

  if(p->p_piece_flags == NULL)
    p->p_piece_flags = calloc(1, to->to_num_pieces);

  for(int i = 0; i < to->to_num_pieces; i++) {
    p->p_piece_flags[i] |= PIECE_HAVE;
  }
  p->p_num_pieces_have = to->to_num_pieces;

  peer_update_interest(to, p);
  if(p->p_peer_choking == 0)
    torrent_io_do_requests(to);
}


/**
 *
 */
static int
recv_piece(peer_t *p, const uint8_t *buf, size_t len)
{
  torrent_t *to = p->p_torrent;
  if(len < 8) {
    peer_disconnect(p, "Bad piece header length");
    return 1;
  }

#if 0
  static int dropper = 0;
  dropper++;
  if(dropper == 10) {
    dropper = 0;
    return 0;
  }
#endif

  uint32_t index = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
  uint32_t begin = (buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7];

  len -= 8;
  buf += 8;

  torrent_request_t *tr;

  LIST_FOREACH(tr, &p->p_download_requests, tr_peer_link) {
    if(tr->tr_piece == index && tr->tr_begin  == begin &&  tr->tr_length == len)
      break;
  }

  if(tr == NULL) {
    to->to_wasted_bytes += len;
    p->p_num_waste++;
    peer_trace(p, PEER_DBG_DOWNLOAD,
               "Got data not asked for: %d:0x%x+0x%x",
               index, begin, (int)len);
    return 0;
  }

  to->to_downloaded_bytes += len;
  p->p_bytes_received += len;

  const int64_t now = async_current_time();
  const int second = now / 1000000;

  average_fill(&to->to_download_rate, second, to->to_downloaded_bytes);
  average_fill(&p->p_download_rate,   second, p->p_bytes_received);


  int delay = MIN(60000000, now - tr->tr_send_time);

  if(p->p_block_delay) {
    p->p_block_delay = (p->p_block_delay * 7 + delay) / 8;
  } else {
    p->p_block_delay = delay;
    peer_cancel_orphaned_requests(p, tr);
  }
  p->p_maxq = 10;

  assert(tr->tr_qdepth < 10);
  if(p->p_bd[tr->tr_qdepth]) {
    p->p_bd[tr->tr_qdepth] = (p->p_bd[tr->tr_qdepth] * 7 + delay) / 8;
  } else {
    p->p_bd[tr->tr_qdepth] = delay;
  }

  if(tr->tr_block != NULL) {
    LIST_REMOVE(tr, tr_block_link);
    torrent_receive_block(tr->tr_block, buf, begin, len, to, p);
  }

  peer_destroy_request(tr);
  return 0;
}


/**
 *
 */
static int
recv_reject(peer_t *p, const uint8_t *buf, size_t len)
{
  if(len != 12) {
    peer_disconnect(p, "Bad reject header length");
    return 1;
  }

  uint32_t piece  = rd32_be(buf);
  uint32_t offset = rd32_be(buf + 4);
  uint32_t length = rd32_be(buf + 8);

  torrent_request_t *tr;

  LIST_FOREACH(tr, &p->p_download_requests, tr_peer_link) {
    if(tr->tr_piece  == piece &&
       tr->tr_begin  == offset &&
       tr->tr_length == length)
      break;
  }

  if(tr == NULL)
    /**
     * Request not sent (or have already been cancelled)
     * Some peers send reject when as a response to a cancel
     * so this can be quite common
     */
    return 0;

  peer_trace(p, PEER_DBG_DOWNLOAD,
             "Rejected request for %d:0x%x+0x%x linked:%s",
             piece, offset, length, tr->tr_block ? "YES" : "NO");

  torrent_block_t *tb = tr->tr_block;
  if(tb != NULL) {
    torrent_piece_t *tp = tb->tb_piece;
    LIST_REMOVE(tr, tr_block_link);

    /**
     * If no more requests are pending, we need to put back block on
     * wait list.  Next torrent_io_do_requests() will take care of
     * sending out new requests again.
     */

    if(LIST_FIRST(&tb->tb_requests) == NULL) {
      LIST_REMOVE(tb, tb_piece_link);
      LIST_INSERT_HEAD(&tp->tp_waiting_blocks, tb, tb_piece_link);
    }
  }


  torrent_t *to = p->p_torrent;

  if(p->p_piece_flags == NULL)
    p->p_piece_flags = calloc(1, to->to_num_pieces);

  p->p_piece_flags[tr->tr_piece] |= PIECE_REJECTED;


  peer_destroy_request(tr);
  return 0;
}



/**
 *
 */
void
peer_send_piece(peer_t *p, torrent_piece_t *tp, uint32_t offset, uint32_t length)
{
  uint8_t out[13];

  wr32_be(out, 9 + length);
  out[4] = BT_MSGID_PIECE;
  wr32_be(out + 5, tp->tp_index);
  wr32_be(out + 9, offset);

  p->p_torrent->to_uploaded_bytes += length;
  p->p_bytes_sent += length;

  asyncio_send(p->p_connection, out, sizeof(out), 1);
  asyncio_send(p->p_connection, tp->tp_data + offset, length, 0);
}


/**
 *
 */
void
peer_send_reject(peer_t *p, uint32_t piece, uint32_t offset, uint32_t length)
{
  uint8_t buf[17] = {0,0,0,13,BT_MSGID_REJECT,
                     piece >> 24,
                     piece >> 16,
                     piece >> 8,
                     piece,
                     offset >> 24,
                     offset >> 16,
                     offset >> 8,
                     offset,
                     length >> 24,
                     length >> 16,
                     length >> 8,
                     length};
  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
}


/**
 *
 */
static int
recv_request(peer_t *p, const uint8_t *buf, size_t len)
{
  if(len != 12) {
    peer_disconnect(p, "Bad request packet length");
    return 1;
  }

  uint32_t piece  = rd32_be(buf);
  uint32_t offset = rd32_be(buf + 4);
  uint32_t length = rd32_be(buf + 8);
  torrent_piece_t *tp;
  torrent_t *to = p->p_torrent;

  peer_trace(p, PEER_DBG_UPLOAD,
             "Got request for piece %d:0x%x+0x%x",
             piece, offset, length);

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link)
    if(tp->tp_index == piece)
      break;

  if(p->p_am_choking)
    return 0; // Don't send to peer which we have choked

  if(tp == NULL) {

    if(to->to_cachefile_piece_map[piece] != -1) {

      tp = torrent_piece_create(to, piece);
      tp->tp_load_req = 1;
      torrent_diskio_wakeup();

    } else {
      peer_trace(p, PEER_DBG_UPLOAD,
                 "Got request for piece %d:0x%x+0x%x -- Not in cache",
                 piece, offset, length);
      return 0;
    }
  }

  if(offset + length > tp->tp_piece_length) {
    peer_disconnect(p, "Request piece %d:0x%x+0x%x out of range",
                    piece, offset, length);
    return 1;
  }

  int delay_xmit = 0;
  if(to->to_output_rate_tokens < length) {
    if(!asyncio_timer_is_armed(&to->to_output_rate_timer))
      asyncio_timer_arm(&to->to_output_rate_timer,
                        to->to_output_rate_refill_time + 100000);
    delay_xmit = 1;
  } else {
    delay_xmit = !tp->tp_hash_ok;
  }

  if(delay_xmit) {

    torrent_sendreq_t *ts = calloc(1, sizeof(torrent_sendreq_t));

    if(TAILQ_FIRST(&p->p_sendreqs) == NULL)
      TAILQ_INSERT_TAIL(&to->to_have_sendreq_peers, p, p_have_sendreq_link);

    TAILQ_INSERT_TAIL(&p->p_sendreqs, ts, ts_peer_link);
    ts->ts_peer = p;
    LIST_INSERT_HEAD(&tp->tp_sendreqs, ts, ts_piece_link);
    ts->ts_piece = tp;

    ts->ts_offset = offset;
    ts->ts_length = length;
    return 0;
  }
  to->to_output_rate_tokens -= length;
  peer_send_piece(p, tp, offset, length);
  return 0;
}


/**
 *
 */
static int
recv_allowed_fast(peer_t *p, const uint8_t *buf, size_t len)
{
  if(len != 4) {
    peer_disconnect(p, "Bad request packet length");
    return 1;
  }

  return 0;
}


/**
 *
 */
static int
recv_extension(peer_t *p, const uint8_t *buf, size_t len)
{
  char errbuf[256];

  if(len < 1) {
    peer_disconnect(p, "Bad request packet length");
    return 1;
  }
  uint8_t msgid = buf[0];
  int consumed = 0;
  buf++;
  len--;
  const char *d = (const char *)buf;
  htsmsg_t *msg = bencode_deserialize(d, d + len,
                                      errbuf, sizeof(errbuf),
                                      NULL, NULL, &consumed);
  if(msg == NULL) {
    peer_disconnect(p, "Bad extension message -- %s", errbuf);
    return 1;
  }
  peer_parse_extension(p, msg, msgid, buf + consumed, len - consumed);

  htsmsg_release(msg);
  return 0;
}


/**
 *
 */
void
peer_connect(peer_t *p)
{
  torrent_t *to = p->p_torrent;

  assert(p->p_connection == NULL);

  peer_set_state(p, PEER_STATE_CONNECTING);

  to->to_active_peers++;
  btg.btg_active_peers++;

  char name[64];
  snprintf(name, sizeof(name), "BT Peer %s", net_addr_str(&p->p_addr));

  p->p_connection = asyncio_connect(name, &p->p_addr,
				    peer_connect_cb,
				    peer_read_cb,
				    p, 5000);
}


/**
 *
 */
static int
recv_message(peer_t *p, htsbuf_queue_t *q)
{
  uint8_t d4[4];
  uint8_t msgid;

  if(htsbuf_peek(q, d4, sizeof(d4)) != sizeof(d4))
    return 1;
  unsigned len = rd32_be(d4);

  if(len > 0x100000) { // Arbitrary
    peer_disconnect(p, "Bad message length");
    return 1;
  }

  if(q->hq_size < len + 4)
    return 1; // Not enoguh bytes in buffer yet

  htsbuf_drop(q, 4);

  if(len == 0) {
    return 0; // Keep alive
  }
  htsbuf_read(q, &msgid, 1);
  len--;

  void *data = NULL;

  if(len) {
    data = mymalloc(len);
    if(data == NULL) {
      peer_disconnect(p, "Out of memory");
      return 1;
    }

    htsbuf_read(q, data, len);
  }

  int r = 0;

  switch(msgid) {
  case BT_MSGID_BITFIELD:
    r = recv_bitfield(p, data, len);
    break;

  case BT_MSGID_HAVE:
    r = recv_have(p, data, len);
    break;

  case BT_MSGID_UNCHOKE:
    if(!p->p_peer_choking)
      break;

    peer_trace(p, PEER_DBG_DOWNLOAD,
               "Unchoked us, we are %sinterested",
               p->p_am_interested ? "" : "not ");
    p->p_peer_choking = 0;
    LIST_INSERT_HEAD(&p->p_torrent->to_unchoked_peers, p, p_unchoked_link);
    p->p_maxq = 1;
    torrent_io_do_requests(p->p_torrent);
    break;

  case BT_MSGID_CHOKE:
    if(p->p_peer_choking)
      break;

    peer_trace(p, PEER_DBG_DOWNLOAD,
               "Choked us, we are %sinterested",
               p->p_am_interested ? "" : "not ");
    p->p_peer_choking = 1;
    p->p_choked_time = async_current_time();
    LIST_REMOVE(p, p_unchoked_link);
    peer_abort_requests(p);
    torrent_io_do_requests(p->p_torrent);
    break;

  case BT_MSGID_PIECE:
    recv_piece(p, data, len);
    break;

  case BT_MSGID_INTERESTED:
    peer_trace(p, PEER_DBG_UPLOAD, "Is interested");
    p->p_peer_interested = 1;
    break;

  case BT_MSGID_NOT_INTERESTED:
    peer_trace(p, PEER_DBG_UPLOAD, "Is not interested");
    p->p_peer_interested = 0;
    break;

  case BT_MSGID_REQUEST:
    r = recv_request(p, data, len);
    break;

  case BT_MSGID_HAVE_ALL:
    recv_have_all(p);
    break;

  case BT_MSGID_HAVE_NONE:
    break;

  case BT_MSGID_ALLOWED_FAST:
    r = recv_allowed_fast(p, data, len);
    break;

  case BT_MSGID_REJECT:
    r = recv_reject(p, data, len);
    break;
  case BT_MSGID_EXTENSION:
    r = recv_extension(p, data, len);
    break;

  default:
#if 0
    TRACE(TRACE_ERROR, "BITTORRENT",
          "%s: Can't handle message id 0x%x (yet)", p->p_name, msgid);
    hexdump("DUMP", data, len);
#endif
    break;
  }

  free(data);
  return r;
}


/**
 *
 */
static void
peer_read_cb(void *opaque, htsbuf_queue_t *q)
{
  peer_t *p = opaque;
  int timeout;

  hts_mutex_lock(&bittorrent_mutex);

  switch(p->p_state_) {

  default:
    abort();

  case PEER_STATE_WAIT_HANDSHAKE:
    if(recv_handshake(p, q))
      break;
    LIST_INSERT_HEAD(&p->p_torrent->to_running_peers, p, p_running_link);
    peer_set_state(p, PEER_STATE_RUNNING);
    // FALLTHRU
    p->p_connect_fail = 0;
    p->p_disconnected = 0;

  case PEER_STATE_RUNNING:
    while(1) {
      if(recv_message(p, q))
        break;
    }

    if(p->p_connection != NULL) {
      timeout = 300;
      asyncio_set_timeout_delta_sec(p->p_connection, timeout);
    }
    break;
  }
  hts_mutex_unlock(&bittorrent_mutex);
}



/**
 *
 */
void
peer_update_interest(torrent_t *to, peer_t *p)
{
  int interested = 0;

  torrent_piece_t *tp;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(LIST_FIRST(&tp->tp_waiting_blocks) == NULL &&
       LIST_FIRST(&tp->tp_sent_blocks) == NULL)
      continue;

    if(p->p_piece_flags == NULL)
      continue;

    assert(tp->tp_index < to->to_num_pieces);
    if(!(p->p_piece_flags[tp->tp_index] & PIECE_HAVE))
      continue;

    interested = 1;
    break;
  }

  if(p->p_state_ != PEER_STATE_RUNNING)
    return;

  if(p->p_am_interested != interested) {
    p->p_am_interested = interested;
    peer_trace(p, PEER_DBG_DOWNLOAD, "Sending %sinterested",
               interested ? "" : "not ");
    peer_send_msgid(p, interested ? BT_MSGID_INTERESTED : BT_MSGID_NOT_INTERESTED);
  }
}



/**
 *
 */
void
peer_cancel_request(torrent_request_t *tr)
{
  peer_send_cancel(tr->tr_peer, tr);
  peer_destroy_request(tr);
}


/**
 *
 */
static void
peer_cancel_orphaned_requests(peer_t *p, torrent_request_t *skip)
{
  torrent_request_t *tr, *next;
  for(tr = LIST_FIRST(&p->p_download_requests); tr != NULL; tr = next) {
    next = LIST_NEXT(tr, tr_peer_link);

    if(tr->tr_block != NULL || tr == skip)
      continue;

    peer_cancel_request(tr);
  }
}



/**
 *
 */
static void
peer_send_msgid(peer_t *p, int msgid)
{
  uint8_t buf[5] = {0,0,0,1,msgid};
  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}

/**
 *
 */
void
peer_send_request(peer_t *p, const torrent_block_t *tb)
{
  uint32_t piece  = tb->tb_piece->tp_index;
  uint32_t offset = tb->tb_begin;
  uint32_t length = tb->tb_length;


  peer_trace(p, PEER_DBG_DOWNLOAD,
             "Requesting %d:0x%x+0x%x",
             piece, offset, length);

  p->p_num_requests++;

  uint8_t buf[17] = {0,0,0,13,BT_MSGID_REQUEST,
                     piece >> 24,
                     piece >> 16,
                     piece >> 8,
                     piece,
                     offset >> 24,
                     offset >> 16,
                     offset >> 8,
                     offset,
                     length >> 24,
                     length >> 16,
                     length >> 8,
                     length};

  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}


/**
 *
 */
static void
peer_send_cancel(peer_t *p, const torrent_request_t *tr)
{
  uint32_t piece  = tr->tr_piece;
  uint32_t offset = tr->tr_begin;
  uint32_t length = tr->tr_length;

  peer_trace(p, PEER_DBG_DOWNLOAD,
             "Canceling %d:0x%x+0x%x",
             piece, offset, length);

  p->p_num_cancels++;

  uint8_t buf[17] = {0,0,0,13,BT_MSGID_CANCEL,
                     piece >> 24,
                     piece >> 16,
                     piece >> 8,
                     piece,
                     offset >> 24,
                     offset >> 16,
                     offset >> 8,
                     offset,
                     length >> 24,
                     length >> 16,
                     length >> 8,
                     length};

  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}


/**
 *
 */
void
peer_send_have(peer_t *p, uint32_t piece)
{
  uint8_t buf[9] = {0,0,0,5,BT_MSGID_HAVE,
                     piece >> 24,
                     piece >> 16,
                     piece >> 8,
                     piece};

  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}



/**
 *
 */
void
peer_choke(peer_t *p, int choke)
{
  if(p->p_am_choking != choke) {
    p->p_am_choking = choke;

    peer_trace(p, PEER_DBG_UPLOAD, "Sending %s",
               choke ? "choke" : "unchoke");
    peer_send_msgid(p, choke ? BT_MSGID_CHOKE : BT_MSGID_UNCHOKE);
  }
}


/**
 *
 */
void
peer_add(torrent_t *to, const net_addr_t *na)
{
  peer_t *p;
  LIST_FOREACH(p, &to->to_peers, p_link)
    if(!net_addr_cmp(&p->p_addr, na))
      return; // Already know about peer

  to->to_num_peers++;
  p = calloc(1, sizeof(peer_t));

  p->p_addr = *na;
  p->p_torrent = to;
  p->p_name = strdup(net_addr_str(na));
  asyncio_timer_init(&p->p_ka_send_timer, peer_send_keepalive, p);

  TAILQ_INIT(&p->p_sendreqs);

  LIST_INSERT_HEAD(&to->to_peers, p, p_link);
  if(to->to_active_peers  >= btg.btg_max_peers_torrent ||
     btg.btg_active_peers >= btg.btg_max_peers_global) {

    peer_set_state(p, PEER_STATE_INACTIVE);
    TAILQ_INSERT_TAIL(&to->to_inactive_peers, p, p_queue_link);
    return;
  }
  
  peer_connect(p);
}

/**
 *
 */
void
peer_shutdown_all(torrent_t *to)
{
  peer_t *p;
  while((p = LIST_FIRST(&to->to_peers)) != NULL)
    peer_shutdown(p, PEER_STATE_DESTROYED, 0);
}


/**
 *
 */
void
peer_activate_pending_data(torrent_t *to)
{
  peer_t *p, *next;
  for(p = LIST_FIRST(&to->to_running_peers); p != NULL; p = next) {
    next = LIST_NEXT(p, p_running_link);

    if(p->p_pending_have_all) {

      if(p->p_piece_flags == NULL)
        p->p_piece_flags = calloc(1, to->to_num_pieces);

      for(int i = 0; i < to->to_num_pieces; i++) {
        p->p_piece_flags[i] |= PIECE_HAVE;
      }
      p->p_num_pieces_have = to->to_num_pieces;
    }

    if(p->p_pending_bitfield != NULL) {
      void *d = p->p_pending_bitfield;
      p->p_pending_bitfield = NULL;

      if(parse_bitfield(p, d, p->p_pending_bitfield_size)) {
        peer_disconnect(p, "Invalid 'bitfield' length got:%d, pieces:%d",
                        p->p_pending_bitfield_size, to->to_num_pieces);
      }
      free(d);
    }
  }
}


/**
 *
 */
static void
peer_parse_extension_handshake(peer_t *p, htsmsg_t *handshake)
{
  htsmsg_t *m = htsmsg_get_map(handshake, "m");

  if(m != NULL) {
    p->p_ext_ut_metadata = htsmsg_get_u32_or_default(m, "ut_metadata", 0);
    hts_cond_broadcast(&torrent_metainfo_available_cond);
  }
}


/**
 *
 */
static void
peer_parse_extension_metadata(peer_t *p, htsmsg_t *msg,
                              const uint8_t *data, size_t len)
{
  int msg_type   = htsmsg_get_u32_or_default(msg, "msg_type", -1);
  int piece      = htsmsg_get_u32_or_default(msg, "piece", -1);
  int total_size = htsmsg_get_u32_or_default(msg, "total_size", -1);
  metainfo_request_t *mr;
  if(msg_type != 1 && msg_type != 2)
    return;
  if(total_size < 1)
    return;

  LIST_FOREACH(mr, &p->p_metainfo_requests, mr_peer_link) {

    if(mr->mr_piece == piece && mr->mr_state == MR_SENT) {
      if(msg_type == 1) {
        mr->mr_size = len;
        mr->mr_data = malloc(len);
        mr->mr_total_size = total_size;
        memcpy(mr->mr_data, data, len);
        mr->mr_state = MR_RECEIVED;
      } else {
        mr->mr_state = MR_REJECTED;
      }
      hts_cond_broadcast(&torrent_metainfo_available_cond);
    }
  }
}


/**
 *
 */
static void
peer_parse_extension(peer_t *p, htsmsg_t *msg, uint8_t msgid,
                     const uint8_t *data, size_t len)
{
  switch(msgid) {
  case EXTENSION_MSGID_HANDSHAKE:
    peer_parse_extension_handshake(p, msg);
    break;
  case EXTENSION_MSGID_METADATA:
    peer_parse_extension_metadata(p, msg, data, len);
    break;
  }
}


/**
 *
 */
static void
peer_send_extension_msg(peer_t *p, htsmsg_t *msg, uint8_t msgid)
{
  buf_t *b = bencode_serialize(msg);
  htsmsg_release(msg);

  uint8_t buf[6] = {0,0,0,0,BT_MSGID_EXTENSION,msgid};
  wr32_be(buf, buf_len(b) + 2);
  //  hexdump("EXTHDR", buf, sizeof(buf));
  //  hexdump("EXTDAT", buf_cstr(b), buf_len(b));
  asyncio_send(p->p_connection, buf, sizeof(buf), 1);
  asyncio_send(p->p_connection, buf_cstr(b), buf_len(b), 0);
  buf_release(b);
}

/**
 *
 */
static void
peer_send_extension_handshake(peer_t *p)
{
  char version[128];
  htsmsg_t *handshake = htsmsg_create_map();

  htsmsg_t *m = htsmsg_create_map();
  htsmsg_add_u32(m, "ut_metadata", EXTENSION_MSGID_METADATA);
  htsmsg_add_msg(handshake, "m", m);

  snprintf(version, sizeof(version), APPNAMEUSER" %s", htsversion);
  htsmsg_add_str(handshake, "v", version);
  peer_send_extension_msg(p, handshake, EXTENSION_MSGID_HANDSHAKE);
}


/**
 *
 */
void
peer_send_metainfo_request(peer_t *p, metainfo_request_t *mr)
{
  mr->mr_state = MR_SENT;

  htsmsg_t *req = htsmsg_create_map();
  htsmsg_add_u32(req, "msg_type", 0); // 0 == request
  htsmsg_add_u32(req, "piece", mr->mr_piece);
  peer_send_extension_msg(p, req, p->p_ext_ut_metadata);
  peer_trace(p, PEER_DBG_DOWNLOAD,
             "Requesting metadata piece %d", mr->mr_piece);
}
