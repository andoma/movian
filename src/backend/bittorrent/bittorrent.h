#pragma once

#include "networking/asyncio.h"

extern hts_mutex_t bittorrent_mutex; // Protects most of these data structures

LIST_HEAD(torrent_tracker_list, torrent_tracker);
LIST_HEAD(torrent_list, torrent);
LIST_HEAD(tracker_list, tracker);
LIST_HEAD(peer_list, peer);
TAILQ_HEAD(peer_queue, peer);

#define TRACKER_PROTO_UDP 1

typedef struct bt_config {
  int btc_max_peers_global;
  int btc_max_peers_torrent;
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
  LIST_ENTRY(peer) p_link;
  struct torrent *p_torrent;
  
  net_addr_t p_addr;

  struct asyncio_timer p_timer;
  
  asyncio_fd_t *p_connection;

  TAILQ_ENTRY(peer) p_queue_link; // Depends on state

  enum {
    PEER_STATE_INACTIVE,
    PEER_STATE_CONNECTING,
    PEER_STATE_CONNECT_FAIL,

  } p_state;

  char p_am_choking;
  char p_am_interested;
  char p_peer_choking;
  char p_peer_interested;

  

} peer_t;



/**
 *
 */
typedef struct torrent {
  LIST_ENTRY(torrent) to_link;

  int to_refcount;

  uint8_t to_info_hash[20];
  
  struct torrent_tracker_list to_trackers;
  struct peer_list to_peers;

  int to_active_peers;

  struct peer_queue to_inactive_peers;

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

} torrent_tracker_t;



torrent_t *torrent_create(const uint8_t *info_hash, const char *title,
			  const char **trackers);

tracker_t *tracker_create(const char *url);

void tracker_add_torrent(tracker_t *tr, torrent_t *t);

void torrent_add_peer(torrent_t *to, const net_addr_t *na);
