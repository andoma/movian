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
#pragma once
#include "fileaccess/fileaccess.h"
#include "networking/asyncio.h"
#include "misc/average.h"
#include "misc/cancellable.h"

#define PIECE_HAVE     0x1
#define PIECE_NOTIFIED 0x2
#define PIECE_REJECTED 0x4

struct htsmsg;

extern hts_mutex_t bittorrent_mutex; // Protects most of these data structures
extern struct torrent_list torrents;
extern hts_cond_t torrent_piece_hash_needed_cond;
extern hts_cond_t torrent_piece_io_needed_cond;
extern hts_cond_t torrent_piece_verified_cond;
extern hts_cond_t torrent_metainfo_available_cond;
extern struct tracker_list trackers;

LIST_HEAD(tracker_torrent_list, tracker_torrent);
LIST_HEAD(torrent_list, torrent);
LIST_HEAD(tracker_list, tracker);
LIST_HEAD(peer_list, peer);
TAILQ_HEAD(peer_queue, peer);
TAILQ_HEAD(torrent_file_queue, torrent_file);
LIST_HEAD(torrent_fh_list, torrent_fh);
LIST_HEAD(torrent_request_list, torrent_request);
TAILQ_HEAD(torrent_request_queue, torrent_request);
TAILQ_HEAD(torrent_piece_queue, torrent_piece);
LIST_HEAD(torrent_block_list, torrent_block);
LIST_HEAD(torrent_piece_list, torrent_piece);
LIST_HEAD(torrent_sendreq_list, torrent_sendreq);
TAILQ_HEAD(torrent_sendreq_queue, torrent_sendreq);
LIST_HEAD(piece_peer_list, piece_peer);
LIST_HEAD(metainfo_request_list, metainfo_request);

typedef struct bt_global {
  int btg_max_peers_global;
  int btg_max_peers_torrent;
  int btg_in_flight_requests;
  int btg_active_peers;
  uint8_t btg_peer_id[21];

  int btg_enabled;

  rstr_t *btg_cache_path;
  prop_t *btg_torrent_status;
  prop_t *btg_disk_status;

  int btg_free_space_percentage;
  int btg_max_send_speed;

  uint64_t btg_total_bytes_inactive;
  uint64_t btg_total_bytes_active;
  uint64_t btg_cache_limit;

  uint64_t btg_disk_avail;

} bt_global_t;

extern bt_global_t btg;



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

  net_addr_t t_addr;

  struct tracker_torrent_list t_torrents;
  uint32_t t_conn_txid;

  int t_conn_attempt;

  uint64_t t_conn_id;

  void (*t_announce)(struct tracker_torrent *tt, int event);

  void (*t_destroy)(struct tracker_torrent *tt);

  struct asyncio_dns_req *t_adr; // Pending Async DNS Request

} tracker_t;


/**
 *
 */
typedef struct piece_peer {
  LIST_ENTRY(piece_peer) pp_piece_link;
  struct torrent_piece *pp_tp;

  LIST_ENTRY(piece_peer) pp_peer_link;
  struct peer *pp_peer;

  int pp_bad;

} piece_peer_t;


/**
 *
 */
typedef struct metainfo_request {
  LIST_ENTRY(metainfo_request) mr_peer_link;
  struct peer *mr_peer;

  LIST_ENTRY(metainfo_request) mr_query_link;

  void *mr_data;

  enum {
    MR_PENDING_SEND,
    MR_SENT,
    MR_RECEIVED,
    MR_REJECTED,
  } mr_state;

  int mr_size;
  int mr_piece;
  int mr_total_size;

} metainfo_request_t;


/**
 *
 */
typedef struct peer {
  struct torrent *p_torrent;

  LIST_ENTRY(peer) p_link;
  LIST_ENTRY(peer) p_running_link;
  LIST_ENTRY(peer) p_unchoked_link;

  TAILQ_ENTRY(peer) p_have_sendreq_link;

  char *p_name;
  net_addr_t p_addr;

  struct asyncio_timer p_timer;
  uint64_t p_choked_time;

  asyncio_fd_t *p_connection;

  int p_connect_fail;
  int p_disconnected;
  uint64_t p_fail_time;

  TAILQ_ENTRY(peer) p_queue_link; // Depends on state

  enum {
    PEER_STATE_INACTIVE,
    PEER_STATE_CONNECTING,
    PEER_STATE_CONNECT_FAIL,
    PEER_STATE_WAIT_HANDSHAKE,
    PEER_STATE_RUNNING,
    PEER_STATE_DISCONNECTED,
    PEER_STATE_DESTROYED,
    PEER_STATE_num,
  } p_state_;

  uint64_t p_state_change_time;

  char p_am_choking : 1;
  char p_am_interested : 1;
  char p_peer_choking : 1;
  char p_peer_interested : 1;
  char p_fast_ext : 1;
  char p_ext_prot : 1;
  char p_pending_have_all : 1;

  char p_id[21];


  int p_num_pieces_have;
  uint8_t *p_piece_flags;

  /**
   * If don't know the size of the torrent just yet we just keep
   * the initial bitfield here until we can verify that it matches
   * against the actual torrent size
   */
  uint8_t *p_pending_bitfield;
  int p_pending_bitfield_size;

  struct torrent_request_list p_download_requests;
  struct torrent_sendreq_queue p_sendreqs;

  int p_active_requests;

  uint64_t p_last_send;

  uint64_t p_bytes_received;
  uint64_t p_bytes_sent;

  int p_block_delay;

  int p_bd[10];

  asyncio_timer_t p_ka_send_timer;
  asyncio_timer_t p_data_recv_timer;

  int p_maxq;

  int p_num_requests;
  int p_num_cancels;
  int p_num_waste;

  average_t p_download_rate;

  struct piece_peer_list p_pieces;

  /**
   * Extension mapping,
   * 0 == no support
   */

  uint8_t p_ext_ut_metadata;

  struct metainfo_request_list p_metainfo_requests;

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
  TAILQ_ENTRY(torrent_piece) tp_link;
  LIST_ENTRY(torrent_piece) tp_serve_link;

  struct torrent_block_list tp_waiting_blocks;
  struct torrent_block_list tp_sent_blocks;
  struct torrent_sendreq_list tp_sendreqs;

  uint8_t *tp_data;

  struct piece_peer_list tp_peers; // Peers that have contributed

  int tp_piece_length;
  int tp_refcount;
  int tp_index;

  uint8_t tp_complete      : 1;
  uint8_t tp_hash_computed : 1;
  uint8_t tp_hash_ok       : 1;
  uint8_t tp_on_disk       : 1;
  uint8_t tp_disk_fail     : 1;
  uint8_t tp_load_req      : 1;
  uint8_t tp_loadfail      : 1;

  struct torrent_fh_list tp_active_fh;

  int64_t tp_deadline;

  average_t tp_download_rate;

  int tp_downloaded_bytes;

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
  uint8_t tb_req_tally;

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

  uint8_t tr_req_num;
  uint8_t tr_qdepth;

} torrent_request_t;





/**
 * Represent an upload request received from a peer.
 * If a piece is loaded into memory we can satisfy the request immediately
 * otherwise we need to load it in, during that load, this struct
 * holds the request
 */
typedef struct torrent_sendreq {

  TAILQ_ENTRY(torrent_sendreq) ts_peer_link;
  peer_t *ts_peer;

  LIST_ENTRY(torrent_sendreq) ts_piece_link;
  torrent_piece_t *ts_piece;

  uint32_t ts_offset;
  uint32_t ts_length;

} torrent_sendreq_t;


/**
 *
 */
typedef struct torrent {
  LIST_ENTRY(torrent) to_link;

  char *to_title;

  int to_refcount;

  uint8_t to_info_hash[20];

  uint64_t to_downloaded_bytes;
  uint64_t to_uploaded_bytes;
  uint64_t to_wasted_bytes;
  uint64_t to_total_length;

  struct tracker_torrent_list to_trackers;
  struct peer_list to_peers;
  struct peer_list to_running_peers;
  struct peer_list to_unchoked_peers;

  int to_active_peers;
  int to_num_peers;
  int to_peers_with_outstanding_requests;

  struct peer_queue to_inactive_peers;
  struct peer_queue to_disconnected_peers;
  struct peer_queue to_connect_failed_peers;
  struct peer_queue to_have_sendreq_peers;

  int to_last_unchoke_check;
  int to_piece_length;
  int to_num_pieces;

  uint8_t *to_piece_hashes;

  struct torrent_file_queue to_files;

  struct torrent_file_queue to_root;

  unsigned int to_num_active_pieces;
  unsigned int to_active_pieces_mem;

  struct torrent_piece_queue to_active_pieces;
  struct torrent_piece_list to_serve_order;

  struct torrent_fh_list to_fhs;

  char to_new_valid_piece;
  char to_need_updated_interest;
  char to_corrupt_piece;
  char to_loadfail;
  char to_loading_metadata;

  char to_errbuf[256];

  average_t to_download_rate;

  buf_t *to_metainfo;

  fa_handle_t *to_cachefile;

  int to_cachefile_map_offset;
  int to_cachefile_store_offset;

  int32_t *to_cachefile_piece_map;
  int32_t *to_cachefile_piece_map_inv;
  int to_next_disk_block;
  int to_total_disk_blocks;

  struct asyncio_timer to_output_rate_timer;
  int64_t to_output_rate_refill_time;
  int to_output_rate_tokens;

} torrent_t;



/**
 *
 */
typedef struct torrent_fh {
  fa_handle_t h;
  LIST_ENTRY(torrent_fh) tfh_torrent_file_link;
  LIST_ENTRY(torrent_fh) tfh_torrent_link;
  LIST_ENTRY(torrent_fh) tfh_piece_link; /* Linked during access to
					    an active piece */
  uint64_t tfh_fpos;
  torrent_file_t *tfh_file;

  prop_t *tfh_fa_stats;
  prop_t *tfh_torrent_seeders;
  prop_t *tfh_torrent_leechers;
  prop_t *tfh_known_peers;
  prop_t *tfh_connected_peers;
  prop_t *tfh_recv_peers;

  int64_t tfh_deadline;

  struct cancellable *tfh_cancellable;
  char tfh_cancelled;

} torrent_fh_t;


/**
 *
 */
typedef struct tracker_torrent {

  LIST_ENTRY(tracker_torrent) tt_tracker_link;
  LIST_ENTRY(tracker_torrent) tt_torrent_link;
  tracker_t *tt_tracker;
  torrent_t *tt_torrent;

  struct asyncio_timer tt_timer;

  uint32_t tt_txid;

  uint32_t tt_interval;
  uint32_t tt_leechers;
  uint32_t tt_seeders;
  uint8_t tt_attempt;
  uint8_t tt_tentative;


  char *tt_trackerid;
  struct asyncio_http_req *tt_http_req;

} tracker_torrent_t;


/**
 * Protocol definitions
 */

torrent_t *torrent_create_from_hash(const uint8_t *info_hash,
                                    const char *initiator);

torrent_t *torrent_create_from_infofile(buf_t *metainfo,
                                        char *errbuf, size_t errlen);

torrent_t *torrent_find_by_hash(const uint8_t *infohash);

void torrent_release(torrent_t *t);

void torrent_retain(torrent_t *t);

torrent_piece_t *torrent_piece_create(torrent_t *to, int piece_index);

void torrent_piece_release(torrent_piece_t *tp);

int torrent_load(torrent_t *to, void *buf, uint64_t offset, size_t size,
		 torrent_fh_t *tfh);

void torrent_announce_all(torrent_t *to);

void torrent_attempt_more_peers(torrent_t *to);

void torrent_io_do_requests(torrent_t *to);

void torrent_receive_block(torrent_block_t *tb, const void *buf,
                           int begin, int len, torrent_t *to, peer_t *p);

void torrent_hash_wakeup(void);

int torrent_parse_infodict(torrent_t *to, struct htsmsg *info,
                           char *errbuf, size_t errlen);

/**
 * Peer functions
 */

void peer_add(torrent_t *to, const net_addr_t *na);

void peer_connect(peer_t *p);

const char *peer_state_txt(unsigned int state);

void peer_cancel_request(torrent_request_t *tr);

void peer_send_reject(peer_t *p, uint32_t piece, uint32_t offset, uint32_t length);

void peer_send_piece(peer_t *p, torrent_piece_t *tp,
                     uint32_t offset, uint32_t length);

void peer_send_request(peer_t *p, const torrent_block_t *tb);

void peer_send_have(peer_t *p, uint32_t piece);

void peer_choke(peer_t *p, int choke);

void peer_update_interest(torrent_t *to, peer_t *p);

void peer_shutdown_all(torrent_t *to);

void peer_activate_pending_data(torrent_t *to);

/**
 * Disk IO
 */

void torrent_diskio_wakeup(void);

void torrent_diskio_open(torrent_t *to);

void torrent_diskio_close(torrent_t *to);

int torrent_diskio_scan(int force_flush);

void torrent_diskio_cache_clear(void);

buf_t *torrent_diskio_load_infofile_from_hash(const uint8_t *hash);

/**
 * Tracker
 */

void tracker_remove_torrent(torrent_t *to);

tracker_t *tracker_create(const char *url);

void tracker_add_torrent(tracker_t *tr, torrent_t *t);

void tracker_torrent_destroy(tracker_torrent_t *tt);

void tracker_trace(const tracker_t *t, const char *msg, ...)
  attribute_printf(2, 3);

tracker_t *tracker_udp_create(const char *hostname, int port);

tracker_t *tracker_http_create(void);

/**
 * Misc helpers
 */

void torrent_extract_info_hash(void *opaque, const char *name,
                               const void *data, size_t len);

void torrent_settings_init(void);

void torrent_piece_peer_destroy(piece_peer_t *pp);

torrent_t *torrent_open_url(const char **urlp, char *errbuf, size_t errlen);

void torrent_sendreq_destroy(torrent_sendreq_t *ts);

/**
 * Magnet
 */
torrent_t *magnet_open(const char *url0, char *errbuf, size_t errlen);

void peer_send_metainfo_request(peer_t *p, metainfo_request_t *mr);

void torrent_wakeup_for_metadata_requests(void);
