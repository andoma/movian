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
#include "networking/http.h"
#include "htsmsg/htsmsg.h"
#include "bittorrent.h"

#define TORRENT_REQ_SIZE 16384

bt_config_t btc;
HTS_MUTEX_DECL(bittorrent_mutex);
struct torrent_list torrents;
static int active_peers;
static int torrent_io_signal;

static void torrent_attempt_more_peers(torrent_t *to);
static int torrent_parse_metainfo(torrent_t *to, htsmsg_t *metainfo,
                                  char *errbuf, size_t errlen);

static void update_interest(torrent_t *to);
static void torrent_io_do_requests(torrent_t *to);
static void torrent_io_reschedule(void *aux);

static void peer_shutdown(peer_t *p, int next_state);
static void peer_update_interest(torrent_t *to, peer_t *p);

static hts_cond_t torrent_piece_complete_cond;
static hts_cond_t torrent_piece_hash_cond;

static int torrent_hash_thread_running;
//static int torrent_write_thread_running;
//static hts_thread_t torrent_read_thread;

static void bt_wakeup_hash_thread(void);
//static void bt_wakeup_write_thread(void);

#define BT_MSGID_CHOKE          0
#define BT_MSGID_UNCHOKE        1
#define BT_MSGID_INTERESTED     2
#define BT_MSGID_NOT_INTERESTED 3
#define BT_MSGID_HAVE           4
#define BT_MSGID_BITFIELD       5
#define BT_MSGID_REQUEST        6
#define BT_MSGID_PIECE          7
#define BT_MSGID_CANCEL         8
#define BT_MSGID_PORT           9


#define peer_trace(p, msg...) do { \
  if((p)->p_trace) \
    TRACE(TRACE_DEBUG, "BT", msg); \
  } while(0)


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
torrent_add_tracker(torrent_t *to, const char *url)
{
  torrent_tracker_t *tt;

  LIST_FOREACH(tt, &to->to_trackers, tt_torrent_link) {
    if(!strcmp(tt->tt_tracker->t_url, url))
      break;
  }

  if(tt == NULL) {
    tracker_t *tr = tracker_create(url);
    if(tr != NULL)
      tracker_add_torrent(tr, to);
  }
}


/**
 *
 */
torrent_t *
torrent_create(const uint8_t *info_hash, const char *title,
	       const char **trackers, htsmsg_t *metainfo)
{
  torrent_t *to;

  hts_mutex_assert(&bittorrent_mutex);

  LIST_FOREACH(to, &torrents, to_link) {
    if(!memcmp(to->to_info_hash, info_hash, 20))
      break;
  }

  if(to == NULL) {
    to = calloc(1, sizeof(torrent_t));
    memcpy(to->to_info_hash, info_hash, 20);
    LIST_INSERT_HEAD(&torrents, to, to_link);
    TAILQ_INIT(&to->to_inactive_peers);
    TAILQ_INIT(&to->to_disconnected_peers);
    TAILQ_INIT(&to->to_connect_failed_peers);
    TAILQ_INIT(&to->to_files);
    TAILQ_INIT(&to->to_root);
    TAILQ_INIT(&to->to_active_pieces);

    to->to_title = malloc(41);
    bin2hex(to->to_title, 40, info_hash, 20);
    to->to_title[40] = 0;

    asyncio_timer_init(&to->to_io_reschedule, torrent_io_reschedule, to);

  }

  to->to_refcount++;

  if(metainfo != NULL) {
    if(torrent_parse_metainfo(to, metainfo,
                              to->to_errbuf, sizeof(to->to_errbuf))) {
      TRACE(TRACE_ERROR, "BT", "Unable to parse torrent: %s",
            to->to_errbuf);
    }
  }

  if(trackers != NULL) {
    for(;*trackers; trackers++) {
      torrent_add_tracker(to, *trackers);
    }
  }

  return to;
}


/**
 *
 */
void
torrent_release(torrent_t *t)
{
  printf("Release not implemnted\n");
}

/**
 *
 */
static void
peer_arm_ka_timer(peer_t *p)
{
  asyncio_timer_arm(&p->p_ka_send_timer, async_now + 60 * 1000000);
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
  memset(handshake + 1 + 19, 0, 8);
  memcpy(handshake + 1 + 19 + 8, to->to_info_hash, 20);
  memcpy(handshake + 1 + 19 + 8 + 20, btc.btc_peer_id, 20);
  asyncio_send(p->p_connection, handshake, sizeof(handshake), 0);
  peer_arm_ka_timer(p);
}


/**
 *
 */
static void
send_msgid(peer_t *p, int msgid)
{
  uint8_t buf[5] = {0,0,0,1,msgid};
  asyncio_send(p->p_connection, buf, sizeof(buf), 0);
  peer_arm_ka_timer(p);
}

/**
 *
 */
static void
send_request(peer_t *p, const torrent_block_t *tb)
{
  uint32_t piece  = tb->tb_piece->tp_index;
  uint32_t offset = tb->tb_begin;
  uint32_t length = tb->tb_length;

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
send_cancel(peer_t *p, const torrent_request_t *tr)
{
  uint32_t piece  = tr->tr_piece;
  uint32_t offset = tr->tr_begin;
  uint32_t length = tr->tr_length;

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
static void
send_have(peer_t *p, uint32_t piece)
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
static void
send_keepalive(void *aux)
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

  if(p->p_state != PEER_STATE_RUNNING)
    return;

  asyncio_timer_disarm(&p->p_data_recv_timer);

  while((tr = LIST_FIRST(&p->p_requests)) != NULL) {
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
peer_error_cb(void *opaque, const char *error)
{
  peer_t *p = opaque;

  if(error == NULL) {
    p->p_am_choking = 1;
    p->p_am_interested = 0;
    p->p_peer_choking = 1;
    p->p_peer_interested = 0;

    send_handshake(p);
    asyncio_set_timeout(p->p_connection, async_now + 15 * 1000000);
    p->p_state = PEER_STATE_WAIT_HANDSHAKE;
#if 0
    peer_trace(p, "%s: Connected", p->p_name);
#endif
    return;
  }

#if 0
  peer_trace(p, "%s: %s in state %s",
             p->p_name, error, peer_state_txt(p->p_state));
#endif

  peer_shutdown(p, p->p_state == PEER_STATE_RUNNING ? 
		PEER_STATE_DISCONNECTED :
		PEER_STATE_CONNECT_FAIL);
}


/**
 *
 */
static void
peer_shutdown(peer_t *p, int next_state)
{
  torrent_t *to = p->p_torrent;

  if(p->p_state != PEER_STATE_INACTIVE) {
    to->to_active_peers--;
    active_peers--;
    torrent_attempt_more_peers(to);
  }

  if(p->p_connection != NULL) {
    asyncio_del_fd(p->p_connection);
    p->p_connection = NULL;
  }

  asyncio_timer_disarm(&p->p_ka_send_timer);
  asyncio_timer_disarm(&p->p_data_recv_timer);

  free(p->p_piece_flags);
  p->p_piece_flags = NULL;

  // Do stuff depending on current (old) state

  switch(p->p_state) {

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
    if(p->p_peer_choking == 0) {
      LIST_REMOVE(p, p_unchoked_link);
      p->p_peer_choking = 1;
    }
    break;

  case PEER_STATE_WAIT_HANDSHAKE:
  case PEER_STATE_CONNECTING:
    break;

  default:
    printf("Cant shutdown peer in state %d\n", p->p_state);
    abort();
  }

  peer_abort_requests(p);

  p->p_state = next_state;


  // Do stuff depending on next (new) state
  switch(p->p_state) {
  default:
    printf("Can't shutdown peer to state %d\n", p->p_state);
    abort();

  case PEER_STATE_CONNECT_FAIL:
    p->p_fail_time = async_now;
    p->p_connect_fail++;
    if(p->p_connect_fail == 5)
      goto destroy;
    TAILQ_INSERT_TAIL(&to->to_connect_failed_peers, p, p_queue_link);
    break;

  case PEER_STATE_DISCONNECTED:
    p->p_fail_time = async_now;
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
    free(p);
    break;
  }
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

  peer_trace(p, "%s: Disconnected by us: %s", p->p_name, buf);
  peer_shutdown(p, PEER_STATE_DESTROYED);
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

  if(memcmp(msg + 1 + 19 + 8, p->p_torrent->to_info_hash, 20)) {
    peer_disconnect(p, "Invalid info hash");
    return 1;
  }

  memcpy(p->p_id, msg + 1 + 19 + 8 + 20, 20);
  p->p_id[20] = 0;

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
}

/**
 *
 */
static int
recv_bitfield(peer_t *p, const uint8_t *data, unsigned int len)
{
  if(len == 0)
    return 0;

  torrent_t *to = p->p_torrent;

  if((to->to_num_pieces + 7) / 8 != len) {
    peer_disconnect(p, "Invalid 'bitfield' length got:%d, pieces:%d",
                    len, to->to_num_pieces);
    return 1;
  }

  if(p->p_piece_flags == NULL)
    p->p_piece_flags = calloc(1, to->to_num_pieces);

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
recv_have(peer_t *p, const uint8_t *data, unsigned int len)
{
  torrent_t *to = p->p_torrent;

  if(len != 4) {
    peer_disconnect(p, "Invalid 'have' length: %d", len);
    return 1;
  }

  unsigned int pid =
    (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

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
cancel_request(torrent_request_t *tr)
{
  send_cancel(tr->tr_peer, tr);
  tr->tr_peer->p_active_requests--;
  LIST_REMOVE(tr, tr_peer_link);
  free(tr);
}


/**
 *
 */
static void
peer_cancel_orphaned_requests(peer_t *p, torrent_request_t *skip)
{
  torrent_request_t *tr, *next;
  for(tr = LIST_FIRST(&p->p_requests); tr != NULL; tr = next) {
    next = LIST_NEXT(tr, tr_peer_link);

    if(tr->tr_block != NULL || tr == skip)
      continue;

    cancel_request(tr);
  }
}

/**
 *
 */
static void
block_cancel_requests(torrent_block_t *tb)
{
  torrent_request_t *tr;
  while((tr = LIST_FIRST(&tb->tb_requests)) != NULL) {
    assert(tr->tr_block == tb);
    LIST_REMOVE(tr, tr_block_link);
    tr->tr_block = NULL;

    // If we don't know the response delay yet, then keep the request
    // around just for measurement purposes
    if(tr->tr_peer->p_block_delay == 0)
      continue;

    cancel_request(tr);
  }
}


/**
 *
 */
static void
block_destroy(torrent_block_t *tb)
{
  LIST_REMOVE(tb, tb_piece_link);
  assert(LIST_FIRST(&tb->tb_requests) == NULL);
  free(tb);
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

  LIST_FOREACH(tr, &p->p_requests, tr_peer_link) {
    if(tr->tr_piece == index && tr->tr_begin  == begin &&  tr->tr_length == len)
      break;
  }

  if(tr == NULL) {
    to->to_wasted_bytes += len;
    p->p_num_waste++;
    peer_trace(p,
               "%s: Got block we didn't ask for. Piece %d, offset %d, %d bytes",
               p->p_name, index, begin, len);
    return 0;
  }

  to->to_downloaded_bytes += len;
  p->p_bytes_received += len;

  int second = async_now / 1000000;

  average_fill(&to->to_download_rate, second, to->to_downloaded_bytes);
  average_fill(&p->p_download_rate,   second, p->p_bytes_received);


  int delay = MIN(60000000, async_now - tr->tr_send_time);

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

  torrent_block_t *tb = tr->tr_block;
  if(tb != NULL) {
    LIST_REMOVE(tr, tr_block_link);

    torrent_piece_t *tp = tb->tb_piece;

    tp->tp_downloaded_bytes += len;
    average_fill(&tp->tp_download_rate, second, tp->tp_downloaded_bytes);

    memcpy(tp->tp_data + begin, buf, len);

    // If there are any other requests for this block, cancel them
    block_cancel_requests(tb);
    block_destroy(tb);


    if(LIST_FIRST(&tp->tp_waiting_blocks) == NULL &&
       LIST_FIRST(&tp->tp_sent_blocks) == NULL) {

      // Piece complete

      tp->tp_complete = 1;
      hts_cond_broadcast(&torrent_piece_complete_cond);

      bt_wakeup_hash_thread();
    }
    torrent_io_do_requests(to);
  }


  assert(p->p_active_requests > 0);
  p->p_active_requests--;
  LIST_REMOVE(tr, tr_peer_link);
  free(tr);

  return 0;
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
  printf("Got request for piece %d 0x%x +0x%x from %s\n",
         piece, offset, length, p->p_name);
  torrent_piece_t *tp;
  torrent_t *to = p->p_torrent;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link)
    if(tp->tp_index == piece)
      break;

  if(tp == NULL)
    return 0;

  if(!tp->tp_hash_ok)
    return 0;

  if(offset + length > tp->tp_piece_length) {
    peer_disconnect(p, "Request piece %d 0x%x+0x%x out of range",
                    piece, offset, length);
    return 1;
  }

  uint8_t out[13];

  wr32_be(out, 13 + length);
  out[4] = BT_MSGID_PIECE;
  wr32_be(out + 5, piece);
  wr32_be(out + 9, offset);

  asyncio_send(p->p_connection, out, sizeof(out), 1);
  asyncio_send(p->p_connection, tp->tp_data + offset, length, 0);
  printf("Sent stuff to %s\n", p->p_name);
  return 0;
}


/**
 *
 */
static int
recv_message(peer_t *p, htsbuf_queue_t *q)
{
  uint32_t len;
  uint8_t msgid;

  if(htsbuf_peek(q, &len, sizeof(len)) != sizeof(len))
    return 1;
  len = ntohl(len);

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

    int n = htsbuf_read(q, data, len);
    assert(n == len);
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

    peer_trace(p, "%s: Unchoked us, we are %sinterested", p->p_name,
               p->p_am_interested ? "" : "not ");
    p->p_peer_choking = 0;
    LIST_INSERT_HEAD(&p->p_torrent->to_unchoked_peers, p, p_unchoked_link);
    p->p_maxq = 1;
    torrent_io_do_requests(p->p_torrent);
    break;

  case BT_MSGID_CHOKE:
    if(p->p_peer_choking)
      break;

    peer_trace(p, "%s: Choked us, we are %sinterested", p->p_name,
               p->p_am_interested ? "" : "not ");
    p->p_peer_choking = 1;
    LIST_REMOVE(p, p_unchoked_link);
    peer_abort_requests(p);
    torrent_io_do_requests(p->p_torrent);
    break;

  case BT_MSGID_PIECE:
    recv_piece(p, data, len);
    break;

  case BT_MSGID_INTERESTED:
    peer_trace(p, "%s: Is interested", p->p_name);
    p->p_peer_interested = 1;
    break;

  case BT_MSGID_NOT_INTERESTED:
    peer_trace(p, "%s: Is not interested", p->p_name);
    p->p_peer_interested = 0;
    break;

  case BT_MSGID_REQUEST:
    r = recv_request(p, data, len);
    break;

  default:
    peer_trace(p, "%s: Can't handle message id %d (yet)", p->p_name, msgid);
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

  switch(p->p_state) {

  default:
    abort();

  case PEER_STATE_WAIT_HANDSHAKE:
    if(recv_handshake(p, q))
      return;
    p->p_state = PEER_STATE_RUNNING;
    // FALLTHRU
    p->p_connect_fail = 0;
    p->p_disconnected = 0;

  case PEER_STATE_RUNNING:
    while(1) {
      if(recv_message(p, q))
        break;
    }

    timeout = 300;

    asyncio_set_timeout(p->p_connection, async_now + timeout * 1000000);
    break;
  }
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

  to->to_num_peers++;
  p = calloc(1, sizeof(peer_t));
  p->p_trace = 1;
  p->p_addr = *na;
  p->p_torrent = to;
  p->p_name = strdup(net_addr_str(na));
  asyncio_timer_init(&p->p_ka_send_timer, send_keepalive, p);

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
torrent_attempt_more_peers(torrent_t *to)
{
  if(to->to_active_peers >= btc.btc_max_peers_torrent ||
     active_peers        >= btc.btc_max_peers_global)
    return;

  peer_t *p;

  p = TAILQ_FIRST(&to->to_inactive_peers);
  if(p != NULL) {
    TAILQ_REMOVE(&to->to_inactive_peers, p, p_queue_link);
    peer_connect(p);
    return;
  }

  p = TAILQ_FIRST(&to->to_disconnected_peers);
  if(p != NULL) {
    TAILQ_REMOVE(&to->to_disconnected_peers, p, p_queue_link);
    peer_connect(p);
    return;
  }

  p = TAILQ_FIRST(&to->to_connect_failed_peers);
  if(p != NULL) {
    TAILQ_REMOVE(&to->to_connect_failed_peers, p, p_queue_link);
    peer_connect(p);
    return;
  }
}


/**
 *
 */
static int
torrent_parse_metainfo(torrent_t *to, htsmsg_t *metainfo,
                       char *errbuf, size_t errlen)
{
  htsmsg_t *info = htsmsg_get_map(metainfo, "info");
  if(info == NULL) {
    snprintf(errbuf, errlen, "Missing info dict");
    return 1;
  }

  const char *name = htsmsg_get_str(info, "name");
  if(name != NULL)
    mystrset(&to->to_title, name);


  htsmsg_t *al = htsmsg_get_list(metainfo, "announce-list");
  if(al != NULL) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, al) {
      htsmsg_t *l = htsmsg_get_list_by_field(f);
      if(l == NULL)
        continue;
      htsmsg_field_t *ff;
      HTSMSG_FOREACH(ff, l) {
        const char *t = htsmsg_field_get_string(ff);
        if(t != NULL) {
          torrent_add_tracker(to, t);
        }
      }
    }
  } else {
    const char *announce = htsmsg_get_str(metainfo, "announce");
    if(announce != NULL)
      torrent_add_tracker(to, announce);
  }

  htsmsg_t *files = htsmsg_get_list(info, "files");

  uint64_t offset = 0;

  if(files != NULL) {
    // Multi file torrent

    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, files) {
      htsmsg_t *file = htsmsg_get_map_by_field(f);
      if(file == NULL) {
        snprintf(errbuf, errlen, "File is not a dict");
        return 1;
      }
      int64_t length;
      if(htsmsg_get_s64(file, "length", &length)) {
        snprintf(errbuf, errlen, "Missing file length");
        return 1;
      }

      if(length < 0) {
        snprintf(errbuf, errlen, "Invalid file length");
        return 1;
      }

      htsmsg_t *paths = htsmsg_get_list(file, "path");

      torrent_file_t *tf = NULL;

      htsmsg_field_t *ff;
      char *filename = NULL;

      HTSMSG_FOREACH(ff, paths) {
        const char *path = htsmsg_field_get_string(ff);
        if(path == NULL) {
          snprintf(errbuf, errlen, "Path component is not a string");
          return 1;
        }

        if(filename != NULL)
          strappend(&filename, "/");
        strappend(&filename, path);

        struct torrent_file_queue *tfq = tf ? &tf->tf_files : &to->to_root;
        TAILQ_FOREACH(tf, tfq, tf_parent_link) {
          if(!strcmp(tf->tf_name, path))
            break;
        }
        if(tf == NULL) {
          tf = calloc(1, sizeof(torrent_file_t));
          tf->tf_torrent = to;
          TAILQ_INIT(&tf->tf_files);
          tf->tf_name = strdup(path);
          TAILQ_INSERT_TAIL(&to->to_files, tf, tf_torrent_link);
          TAILQ_INSERT_TAIL(tfq, tf, tf_parent_link);
          tf->tf_fullpath = strdup(filename);
        }
      }

      tf->tf_offset = offset;
      tf->tf_size = length;
      offset += length;
      free(filename);
    }
  } else {
    const char *name = htsmsg_get_str(info, "name");

    if(name == NULL) {
      snprintf(errbuf, errlen, "Missing file name");
      return 1;
    }

    int64_t length;

    if(htsmsg_get_s64(info, "length", &length)) {
      snprintf(errbuf, errlen, "Missing file length");
      return 1;
    }

    if(length < 0) {
      snprintf(errbuf, errlen, "Invalid file length");
      return 1;
    }

    torrent_file_t *tf = calloc(1, sizeof(torrent_file_t));
    tf->tf_torrent = to;
    TAILQ_INIT(&tf->tf_files);
    tf->tf_name = strdup(name);
    TAILQ_INSERT_TAIL(&to->to_files, tf, tf_torrent_link);
    TAILQ_INSERT_TAIL(&to->to_root, tf, tf_parent_link);

    char *filename = NULL;
    strappend(&filename, "/");
    strappend(&filename, name);

    tf->tf_offset = 0;
    tf->tf_size = length;
    offset = length;
    tf->tf_fullpath = filename;
  }

  to->to_total_length = offset;

  to->to_piece_length = htsmsg_get_u32_or_default(info, "piece length", 0);
  if(to->to_piece_length < 65536 || to->to_piece_length > 8388608) {
    snprintf(errbuf, errlen, "Invalid piece length: %d", to->to_piece_length);
    return 1;
  }

  const void *pieces_data;
  size_t pieces_size;
  if(htsmsg_get_bin(info, "pieces", &pieces_data, &pieces_size)) {
    snprintf(errbuf, errlen, "No hash list");
    return 1;
  }

  if(pieces_size % 20) {
    snprintf(errbuf, errlen, "Invalid hash list size: %zd", pieces_size);
    return 1;
  }

  to->to_num_pieces = pieces_size / 20;
  to->to_piece_flags = calloc(1, to->to_num_pieces);

  to->to_piece_hashes = malloc(pieces_size);
  memcpy(to->to_piece_hashes, pieces_data, pieces_size);

  return 0;
}



/**
 *
 */
static int
tp_deadline_cmp(const torrent_piece_t *a, const torrent_piece_t *b)
{
  if(a->tp_deadline < b->tp_deadline)
    return -1;
  return a->tp_deadline >= b->tp_deadline;
}

/**
 *
 */
static torrent_piece_t *
torrent_piece_find(torrent_t *to, int piece_index)
{
  torrent_piece_t *tp;
  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(tp->tp_index == piece_index) {
      TAILQ_REMOVE(&to->to_active_pieces, tp, tp_link);
      TAILQ_INSERT_TAIL(&to->to_active_pieces, tp, tp_link);
      return tp;
    }
  }

  to->to_num_active_pieces++;
  tp = calloc(1, sizeof(torrent_piece_t));
  tp->tp_index = piece_index;
  TAILQ_INSERT_TAIL(&to->to_active_pieces, tp, tp_link);
  tp->tp_deadline = INT64_MAX;
  LIST_INSERT_SORTED(&to->to_serve_order, tp, tp_serve_link, tp_deadline_cmp);

  tp->tp_data = malloc(to->to_piece_length);

  // Create and enqueue requests

  tp->tp_piece_length = to->to_piece_length;

  if(piece_index == to->to_num_pieces - 1) {
    // Last piece, truncate piece length
    tp->tp_piece_length = to->to_total_length % to->to_piece_length;
  }

  for(int i = 0; i < tp->tp_piece_length; i += TORRENT_REQ_SIZE) {
    torrent_block_t *tb = calloc(1, sizeof(torrent_block_t));
    tb->tb_piece = tp;
    LIST_INSERT_HEAD(&tp->tp_waiting_blocks, tb, tb_piece_link);
    tb->tb_begin = i;
    tb->tb_length = MIN(TORRENT_REQ_SIZE, tp->tp_piece_length - i);
  }

  update_interest(to);

  return tp;
}

/**
 *
 */
static void
piece_update_deadline(torrent_t *to, torrent_piece_t *tp)
{
  int64_t deadline = INT64_MAX;
  torrent_fh_t *tfh;
  LIST_FOREACH(tfh, &tp->tp_active_fh, tfh_piece_link)
    deadline = MIN(tfh->tfh_deadline, deadline);

  if(tp->tp_deadline == deadline)
    return;

  tp->tp_deadline = deadline;

  LIST_REMOVE(tp, tp_serve_link);
  LIST_INSERT_SORTED(&to->to_serve_order, tp, tp_serve_link, tp_deadline_cmp);
}



/**
 *
 */
int
torrent_load(torrent_t *to, void *buf, uint64_t offset, size_t size,
	     torrent_fh_t *tfh)
{
  int rval = size;
  // First figure out which pieces we need

  int piece = offset        / to->to_piece_length;
  int piece_offset = offset % to->to_piece_length;

  // Poor mans read-ahead
  if(piece + 1 < to->to_num_pieces)
    torrent_piece_find(to, piece + 1);
  if(piece + 2 < to->to_num_pieces)
    torrent_piece_find(to, piece + 2);
  while(size > 0) {

    torrent_piece_t *tp = torrent_piece_find(to, piece);

    tp->tp_refcount++;

    LIST_INSERT_HEAD(&tp->tp_active_fh, tfh, tfh_piece_link);

    piece_update_deadline(to, tp);

    if(!tp->tp_hash_computed) {
      asyncio_wakeup_worker(torrent_io_signal);

      while(!tp->tp_hash_computed)
        hts_cond_wait(&torrent_piece_hash_cond, &bittorrent_mutex);
    }

    LIST_REMOVE(tfh, tfh_piece_link);

    piece_update_deadline(to, tp);

    tp->tp_refcount--;

    int copy = MIN(size, to->to_piece_length - piece_offset);

    memcpy(buf, tp->tp_data + piece_offset, copy);

    piece++;
    piece_offset = 0;
    size -= copy;
    buf += copy;
  }

  return rval;
}


/**
 *
 */
static void
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

    if(!(p->p_piece_flags[tp->tp_index] & PIECE_HAVE))
      continue;

    interested = 1;
    break;
  }

  if(p->p_state != PEER_STATE_RUNNING)
    return;

  if(p->p_am_interested != interested) {
    p->p_am_interested = interested;
    peer_trace(p, "%s: Sending %sinterested",
               p->p_name, interested ? "" : "not ");
    send_msgid(p, interested ? BT_MSGID_INTERESTED : BT_MSGID_NOT_INTERESTED);
  }
}


/**
 *
 */
static void
update_interest(torrent_t *to)
{
  peer_t *p;

  LIST_FOREACH(p, &to->to_peers, p_link)
    if(p->p_state == PEER_STATE_RUNNING)
      peer_update_interest(to, p);
}


/**
 *
 */
static void
add_request(torrent_block_t *tb, peer_t *p)
{
  torrent_request_t *tr;
  LIST_FOREACH(tr, &p->p_requests, tr_peer_link) {
    if(tr->tr_block == tb)
      return; // Request already sent
  }

  tr = calloc(1, sizeof(torrent_request_t));

  tr->tr_piece  = tb->tb_piece->tp_index;
  tr->tr_begin  = tb->tb_begin;
  tr->tr_length = tb->tb_length;

  tr->tr_block = tb;

  tr->tr_send_time = async_now;
  tr->tr_req_num = tb->tb_req_tally;
  tb->tb_req_tally++;

  LIST_INSERT_HEAD(&tb->tb_requests, tr, tr_block_link);
  send_request(p, tb);

  tr->tr_peer = p;
  tr->tr_qdepth = p->p_active_requests;

  LIST_INSERT_HEAD(&p->p_requests, tr, tr_peer_link);
  p->p_active_requests++;

  p->p_last_send = async_now;
}


/**
 *
 */
static peer_t *
find_optimal_peer(torrent_t *to, const torrent_piece_t *tp)
{
  int best_score = INT32_MAX;
  peer_t *best = NULL;
  peer_t *p;

  LIST_FOREACH(p, &to->to_unchoked_peers, p_unchoked_link) {

    if(p->p_piece_flags == NULL ||
       !(p->p_piece_flags[tp->tp_index] & PIECE_HAVE))
      continue;

    int score;

    if(p->p_block_delay == 0) {
      // Delay not known yet
      
      if(p->p_active_requests) {
	// We have a request already, skip this peer
	continue;
      }

      score = 0; // Assume it's super fast
    } else {

      score = p->p_block_delay;
    }

    if(best == NULL || score < best_score) {
      best = p;
      best_score = score;
    }
  }
  return best;
}


/**
 *
 */
static peer_t *
find_any_peer(torrent_t *to, const torrent_piece_t *tp)
{
  peer_t *p;

  LIST_FOREACH(p, &to->to_unchoked_peers, p_unchoked_link) {

    if(p->p_piece_flags == NULL ||
       !(p->p_piece_flags[tp->tp_index] & PIECE_HAVE))
      continue;

    if(p->p_active_requests < p->p_maxq / 2)
      return p;
  }
  return NULL;
}


#if 1
static const char *
block_name(const torrent_block_t *tb)
{
  static char buf[128];
  snprintf(buf, sizeof(buf), "piece:%d block:0x%x+0x%x",
           tb->tb_piece->tp_index, tb->tb_begin, tb->tb_length);
  return buf;
}
#endif


/**
 *
 */
static void
serve_waiting_blocks(torrent_t *to, torrent_piece_t *tp, int optimal)
{
  torrent_block_t *tb, *next;

  // First take a round and figure out the best peer for where
  // to schedule a read

  for(tb = LIST_FIRST(&tp->tp_waiting_blocks); tb != NULL; tb = next) {
    next = LIST_NEXT(tb, tb_piece_link);

    if(optimal) {
      peer_t *p = find_optimal_peer(to, tp);

      if(p == NULL || p->p_active_requests >= p->p_maxq)
	break;

      add_request(tb, p);

    } else {
      peer_t *p = find_any_peer(to, tp);
      if(p == NULL)
	break;

      add_request(tb, p);

    }
    LIST_REMOVE(tb, tb_piece_link);
    LIST_INSERT_HEAD(&tp->tp_sent_blocks, tb, tb_piece_link);
  }
}




/**
 *
 */
static peer_t *
find_faster_peer(torrent_t *to, const torrent_block_t *tb,
		 int64_t eta_to_beat)
{
  peer_t *p, *best = NULL;
  const torrent_piece_t *tp = tb->tb_piece;
  assert(tp != NULL);

  LIST_FOREACH(p, &to->to_unchoked_peers, p_unchoked_link) {

    if(p->p_piece_flags == NULL ||
       !(p->p_piece_flags[tp->tp_index] & PIECE_HAVE))
      continue;

    if(p->p_block_delay == 0) {
      // Delay not known yet
      continue;
    }

    if(p->p_active_requests >= p->p_maxq)
      continue; // Peer is fully queued


    const torrent_request_t *tr;
    LIST_FOREACH(tr, &tb->tb_requests, tr_block_link) {
      if(tr->tr_peer == p)
	break;
    }
    if(tr != NULL)
      continue;

    int64_t t = async_now + p->p_block_delay * 2;

    if(t < eta_to_beat) {
      eta_to_beat = t;
      best = p;
    }
  }
  return best;
}


/**
 *
 */
static void
check_active_requests(torrent_t *to, torrent_piece_t *tp,
		      int64_t deadline)
{
  torrent_block_t *tb, *next;

  for(tb = LIST_FIRST(&tp->tp_sent_blocks); tb != NULL; tb = next) {
    next = LIST_NEXT(tb, tb_piece_link);
    /*
     * The most recent request for the block is always first in
     * this list. It's the only one we will compare with since
     * we are only gonna add duplicate requests if we think we can
     * outrun the currently enqueued requests
     */
    torrent_request_t *cur = LIST_FIRST(&tb->tb_requests);
    assert(cur != NULL);

    peer_t *curpeer = cur->tr_peer;
    assert(curpeer != NULL);

    int64_t eta, delay = 0;

    eta = cur->tr_send_time + curpeer->p_block_delay;
    if(eta < async_now) {
      // Didn't arrive on time, assume the delay will worse
      // the longer it takes
      delay = async_now - eta;
      eta += delay * 2;
    }

    if(eta < deadline)
      continue; // Nothing to worry about

    // Now, let's see if we can find a peer that we think can beat
    // the current (offsetted) ETA for this block

    peer_t *p = find_faster_peer(to, tb, eta);
    if(p == NULL)
      continue;

    if(0)
    printf("Block %s: Added dup request on peer %s bd:%d "
	   "computed ETA:%ld delay:%ld\n",
	   block_name(tb), p->p_name, p->p_block_delay,
	   eta - async_now, delay);

    add_request(tb, p);

    int new_delay = async_now - cur->tr_send_time;
    if(new_delay > curpeer->p_block_delay) {
      curpeer->p_block_delay = (curpeer->p_block_delay * 7 + new_delay) / 8;
    }
  }
}


/**
 *
 */
static void
torrent_piece_destroy(torrent_t *to, torrent_piece_t *tp)
{
  assert(LIST_FIRST(&tp->tp_active_fh) == NULL);
  to->to_num_active_pieces--;

  free(tp->tp_data);
  TAILQ_REMOVE(&to->to_active_pieces, tp, tp_link);
  LIST_REMOVE(tp, tp_serve_link);
  free(tp);
}


/**
 *
 */
static void
flush_active_pieces(torrent_t *to)
{
  while(to->to_num_active_pieces > 20) {
    torrent_piece_t *tp = TAILQ_FIRST(&to->to_active_pieces);
    assert(tp != NULL);

    if(tp->tp_refcount)
      break;

    if(LIST_FIRST(&tp->tp_waiting_blocks) != NULL)
      break;

    if(LIST_FIRST(&tp->tp_sent_blocks) != NULL)
      break;

    torrent_piece_destroy(to, tp);
  }
}


/**
 *
 */
static void
torrent_send_have(torrent_t *to)
{
  torrent_piece_t *tp;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(!tp->tp_hash_ok)
      continue;

    peer_t *p;

    const int pid = tp->tp_index;

    LIST_FOREACH(p, &to->to_peers, p_link) {

      if(p->p_state != PEER_STATE_RUNNING)
        continue;

      if(p->p_piece_flags == NULL)
        p->p_piece_flags = calloc(1, to->to_num_pieces);

      if(p->p_piece_flags[pid] & PIECE_HAVE)
        continue;

      if(p->p_piece_flags[pid] & PIECE_NOTIFIED)
        continue;
      send_have(p, pid);
      printf("Sending have for index %d to %s\n", pid, p->p_name);
      p->p_piece_flags[pid] |= PIECE_NOTIFIED;
    }
  }
}


/**
 *
 */
static void
torrent_unchoke_peers(torrent_t *to)
{
  peer_t *p;
  LIST_FOREACH(p, &to->to_peers, p_link) {
    if(p->p_state != PEER_STATE_RUNNING)
      continue;

    int choke = 1;

    if(p->p_num_pieces_have != to->to_num_pieces &&
       p->p_peer_interested)
      choke = 0;

    if(p->p_am_choking != choke) {
      p->p_am_choking = choke;

      peer_trace(p, "%s: Sending %s",
               p->p_name, choke ? "choke" : "unchoke");
      send_msgid(p, choke ? BT_MSGID_CHOKE : BT_MSGID_UNCHOKE);
    }
  }
}


/**
 *
 */
static void
torrent_io_do_requests(torrent_t *to)
{
  torrent_piece_t *tp;
#if 0
  printf("----------------------------------------\n");
  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link) {
    printf("Processing piece %d  deadline:%ld files:%s\n",
	   tp->tp_index, tp->tp_deadline,
	   LIST_FIRST(&tp->tp_active_fh) ? "YES" : "NO");
  }
  printf("----------------------------------------\n");
#endif

  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link) {
    if(tp->tp_deadline == INT64_MAX)
      break;
    check_active_requests(to, tp, tp->tp_deadline);
  }

  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link)
    serve_waiting_blocks(to, tp, 1);

  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link)
    serve_waiting_blocks(to, tp, 0);

  int second = async_now / 1000000;

  int rate = average_read(&to->to_download_rate, second) / 125;

  torrent_fh_t *tfh;
  LIST_FOREACH(tfh, &to->to_fhs, tfh_torrent_link) {
    if(tfh->tfh_fa_stats != NULL) {
      prop_set(tfh->tfh_fa_stats, "bitrate", PROP_SET_INT, rate);
    }
  }

  flush_active_pieces(to);

  asyncio_timer_arm(&to->to_io_reschedule, async_now + 1000000);

  if(to->to_last_unchoke_check + 5 < second) {
    to->to_last_unchoke_check = second;
    torrent_unchoke_peers(to);
  }
}


/**
 *
 */
static void
torrent_io_check_pendings(void)
{
  hts_mutex_lock(&bittorrent_mutex);

  torrent_t *to;
  LIST_FOREACH(to, &torrents, to_link) {

    if(to->to_new_valid_piece) {
      to->to_new_valid_piece = 0;
      torrent_send_have(to);
    }

    torrent_io_do_requests(to);
  }

  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
static void
torrent_io_reschedule(void *aux)
{
  hts_mutex_lock(&bittorrent_mutex);
  torrent_io_do_requests(aux);
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
static void
torrent_piece_verify_hash(torrent_t *to, torrent_piece_t *tp)
{
  uint8_t digest[20];
  sha1_decl(shactx);

  tp->tp_refcount++;
  hts_mutex_unlock(&bittorrent_mutex);
  int64_t ts = showtime_get_ts();

  sha1_init(shactx);
  sha1_update(shactx, tp->tp_data, tp->tp_piece_length);
  sha1_final(shactx, digest);

  ts = showtime_get_ts() - ts;

  hts_mutex_lock(&bittorrent_mutex);

  tp->tp_hash_computed = 1;

  const uint8_t *piecehash = to->to_piece_hashes + tp->tp_index * 20;
  tp->tp_hash_ok = !memcmp(piecehash, digest, 20);
  tp->tp_refcount--;
  if(!tp->tp_hash_ok) {
    TRACE(TRACE_ERROR, "BITTORRENT", "Received corrupt piece %d",
	  tp->tp_index);
  } else {
    to->to_new_valid_piece = 1;
    asyncio_wakeup_worker(torrent_io_signal);
  }

  hts_cond_broadcast(&torrent_piece_hash_cond);

  //  bt_wakeup_write_thread();
}


/**
 *
 */
static void *
bt_hash_thread(void *aux)
{
  torrent_t *to;

  hts_mutex_lock(&bittorrent_mutex);

  while(1) {

  restart:

    LIST_FOREACH(to, &torrents, to_link) {
      torrent_piece_t *tp;
      TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
	if(tp->tp_complete && !tp->tp_hash_computed) {
	  torrent_piece_verify_hash(to, tp);
	  goto restart;
	}
      }
    }

    if(hts_cond_wait_timeout(&torrent_piece_complete_cond,
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
bt_wakeup_hash_thread(void)
{
  if(!torrent_hash_thread_running) {
    torrent_hash_thread_running = 1;
    hts_thread_create_detached("bthasher", bt_hash_thread, NULL,
			       THREAD_PRIO_BGTASK);
  }
}

#if 0

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

    if(hts_cond_wait_timeout(&torrent_piece_hash_cond,
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



/**
 *
 */
static void
torrent_io_init(void)
{
  btc.btc_max_peers_global = 200;
  btc.btc_max_peers_torrent = 50;

  torrent_io_signal = asyncio_add_worker(torrent_io_check_pendings);
  hts_cond_init(&torrent_piece_complete_cond, &bittorrent_mutex);
  hts_cond_init(&torrent_piece_hash_cond, &bittorrent_mutex);

}

INITME(INIT_GROUP_ASYNCIO, torrent_io_init);
