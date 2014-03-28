#pragma once

#include "networking/asyncio.h"


#define PIECE_HAVE 0x1


struct htsmsg;

extern hts_mutex_t bittorrent_mutex; // Protects most of these data structures
extern struct torrent_list torrents;

LIST_HEAD(torrent_tracker_list, torrent_tracker);
LIST_HEAD(torrent_list, torrent);
LIST_HEAD(tracker_list, tracker);
LIST_HEAD(peer_list, peer);
TAILQ_HEAD(peer_queue, peer);
TAILQ_HEAD(torrent_file_queue, torrent_file);
LIST_HEAD(torrent_fh_list, torrent_fh);
LIST_HEAD(torrent_request_list, torrent_request);
TAILQ_HEAD(torrent_request_queue, torrent_request);
LIST_HEAD(torrent_piece_list, torrent_piece);
LIST_HEAD(torrent_block_list, torrent_block);

#define TRACKER_PROTO_UDP 1

typedef struct bt_config {
  int btc_max_peers_global;
  int btc_max_peers_torrent;
  int btc_in_flight_requests;
  uint8_t btc_peer_id[21];
} bt_config_t;

extern bt_config_t btc;

/**
 *
 */
typedef struct tracker {
  LIST_ENTRY(tracker) t_link;

  struct asyncio_timer t_timer;

  enum  {
    TRACKER_STATE_PENDING_DNS_RESOLVE,
    TRACKER_STATE_CONNECTING,
    TRACKER_STATE_CONNECTED,
    TRACKER_STATE_ERROR,

  } t_state;

  char *t_url;
  uint16_t t_port;
  uint8_t t_proto;
  
  net_addr_t t_addr;

  struct torrent_tracker_list t_torrents;
  uint32_t tr_conn_txid;

  int t_conn_attempt;

  uint64_t tr_conn_id;

} tracker_t;

/**
 *
 */
typedef struct peer {
  struct torrent *p_torrent;

  LIST_ENTRY(peer) p_link;
  LIST_ENTRY(peer) p_unchoked_link;

  char *p_name;
  net_addr_t p_addr;

  struct asyncio_timer p_timer;
  
  asyncio_fd_t *p_connection;

  TAILQ_ENTRY(peer) p_queue_link; // Depends on state

  enum {
    PEER_STATE_INACTIVE,
    PEER_STATE_CONNECTING,
    PEER_STATE_CONNECT_FAIL,
    PEER_STATE_WAIT_HANDSHAKE,
    PEER_STATE_RUNNING,
    PEER_STATE_DESTROYED,
    PEER_STATE_num,
  } p_state;

  char p_am_choking;
  char p_am_interested;
  char p_peer_choking;
  char p_peer_interested;
  char p_trace;

  char p_id[21];

  uint8_t *p_piece_flags;

  struct torrent_request_list p_requests;

  int p_active_requests;

  uint64_t p_bytes_received;

  int p_block_delay;

  int p_bd[10];

  asyncio_timer_t p_ka_send_timer;
  asyncio_timer_t p_data_recv_timer;

  int p_maxq;

  int p_num_requests;
  int p_num_cancels;
  int p_num_waste;

} peer_t;



/**
 *
 */
typedef struct torrent_file {
  TAILQ_ENTRY(torrent_file) tf_torrent_link;
  TAILQ_ENTRY(torrent_file) tf_parent_link;

  uint64_t tf_offset;
  uint64_t tf_size;

  char *tf_fullpath;
  char *tf_name;

  struct torrent_file_queue tf_files;

  struct torrent *tf_torrent;

  struct torrent_fh_list tf_fhs;

} torrent_file_t;




/**
 * Represents a torrent piece that's in-flight or not backed
 * by disk. For a complete piece that we've saved to disk we will
 * have the PIECE_HAVE bit set in our piece array
 */

typedef struct torrent_piece {
  LIST_ENTRY(torrent_piece) tp_link;

  int tp_access;

  int tp_index;

  struct torrent_block_list tp_waiting_blocks;
  struct torrent_block_list tp_sent_blocks;

  uint8_t *tp_data;

} torrent_piece_t;


/**
 * Represent a block (part of a piece) that we want to request
 */
typedef struct torrent_block {

  LIST_ENTRY(torrent_block) tb_piece_link;
  torrent_piece_t *tb_piece;

  struct torrent_request_list tb_requests;

  uint32_t tb_begin;
  uint32_t tb_length;

} torrent_block_t;


/**
 * Represent a request sent to a peer
 */
typedef struct torrent_request {

  LIST_ENTRY(torrent_request) tr_peer_link;
  peer_t *tr_peer;

  LIST_ENTRY(torrent_request) tr_block_link;
  torrent_block_t *tr_block;

  int64_t tr_send_time;

  uint32_t tr_piece;
  uint32_t tr_begin;
  uint32_t tr_length;

  int tr_est_delay;

  uint8_t tr_dup;
  uint8_t tr_qdepth;

} torrent_request_t;


/**
 *
 */
typedef struct torrent {
  LIST_ENTRY(torrent) to_link;

  char *to_title;

  int to_refcount;

  uint8_t to_info_hash[20];

  uint64_t to_downloaded_bytes;
  uint64_t to_remaining_bytes;
  uint64_t to_uploaded_bytes;
  uint64_t to_total_length;

  struct torrent_tracker_list to_trackers;
  struct peer_list to_peers;
  struct peer_list to_unchoked_peers;

  int to_active_peers;

  struct peer_queue to_inactive_peers;

  struct peer_queue to_failed_peers;


  int to_piece_length;
  int to_num_pieces;

  uint8_t *to_piece_flags;
  uint8_t *to_piece_hashes;

  struct torrent_file_queue to_files;

  struct torrent_file_queue to_root;

  struct torrent_piece_list to_active_pieces;

  asyncio_timer_t to_io_reschedule;

  char to_errbuf[256];

} torrent_t;



/**
 *
 */
typedef struct torrent_tracker {

  enum  {
    TT_STATE_CREATED,
    TT_STATE_ANNOUNCING,
    TT_STATE_ANNOUNCED,
    TT_STATE_ERROR,

  } tt_state;

  LIST_ENTRY(torrent_tracker) tt_tracker_link;
  LIST_ENTRY(torrent_tracker) tt_torrent_link;
  tracker_t *tt_tracker;
  torrent_t *tt_torrent;
  
  uint32_t tt_txid;

  uint32_t tt_interval;
  uint32_t tt_leechers;
  uint32_t tt_seeders;

  struct asyncio_timer tt_timer;

} torrent_tracker_t;



torrent_t *torrent_create(const uint8_t *info_hash, const char *title,
			  const char **trackers, struct htsmsg *metainfo);

void torrent_release(torrent_t *t);

tracker_t *tracker_create(const char *url);

void tracker_add_torrent(tracker_t *tr, torrent_t *t);

void torrent_add_peer(torrent_t *to, const net_addr_t *na);

const char *peer_state_txt(unsigned int state);

int torrent_load(torrent_t *to, void *buf, uint64_t offset, size_t size);

void torrent_announce_all(torrent_t *to, int event);
