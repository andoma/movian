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
#include "bencode.h"
#include "misc/minmax.h"
#include "usage.h"

#define TORRENT_REQ_SIZE 16384

//----------------------------------------------------------------

static asyncio_timer_t torrent_periodic_timer;
bt_global_t btg;
HTS_MUTEX_DECL(bittorrent_mutex);
struct torrent_list torrents;
static int torrent_pendings_signal;
static int torrent_boot_periodic_signal;
static int torrent_metainfo_signal;
static int torrent_hash_thread_running;

hts_cond_t torrent_piece_hash_needed_cond;
hts_cond_t torrent_piece_io_needed_cond;
hts_cond_t torrent_piece_verified_cond;
hts_cond_t torrent_metainfo_available_cond;

//----------------------------------------------------------------

static int torrent_parse_torrentfile(torrent_t *to, htsmsg_t *metainfo,
                                     char *errbuf, size_t errlen);

static void update_interest(torrent_t *to);

static void torrent_piece_destroy(torrent_t *to, torrent_piece_t *tp);

//----------------------------------------------------------------

static void
torrent_trace(const torrent_t *t, const char *msg, ...)
  attribute_printf(2, 3);

static void
torrent_trace(const torrent_t *t, const char *msg, ...)
{
  if(!gconf.enable_torrent_debug)
    return;

  va_list ap;
  char buf[256];
  va_start(ap, msg);
  vsnprintf(buf, sizeof(buf), msg, ap);
  va_end(ap);

  TRACE(TRACE_DEBUG, "BITTORRENT", "%s: %s", t->to_title, buf);
}



/**
 *
 */
static void
torrent_add_tracker(torrent_t *to, const char *url)
{
  tracker_torrent_t *tt;

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
void
torrent_extract_info_hash(void *opaque, const char *name,
                          const void *data, size_t len)
{
  if(strcmp(name, "info"))
    return;

  sha1_decl(shactx);
  sha1_init(shactx);
  sha1_update(shactx, data, len);
  sha1_final(shactx, opaque);
}


/**
 *
 */
torrent_t *
torrent_find_by_hash(const uint8_t *info_hash)
{
  torrent_t *to;

  LIST_FOREACH(to, &torrents, to_link)
    if(!memcmp(to->to_info_hash, info_hash, 20))
      return to;

  return NULL;
}


/**
 *
 */
static torrent_t *
torrent_create(const uint8_t *info_hash, const char *initiator)
{
  if(LIST_FIRST(&torrents) == NULL)
    asyncio_wakeup_worker(torrent_boot_periodic_signal);

  torrent_t *to = calloc(1, sizeof(torrent_t));
  memcpy(to->to_info_hash, info_hash, 20);
  LIST_INSERT_HEAD(&torrents, to, to_link);
  TAILQ_INIT(&to->to_inactive_peers);
  TAILQ_INIT(&to->to_disconnected_peers);
  TAILQ_INIT(&to->to_connect_failed_peers);
  TAILQ_INIT(&to->to_files);
  TAILQ_INIT(&to->to_root);
  TAILQ_INIT(&to->to_active_pieces);

  to->to_title = malloc(41);
  bin2hex(to->to_title, 41, info_hash, 20);
  to->to_title[40] = 0;

  usage_event("Open Torrent", 1,
              USAGE_SEG("initiator", initiator));
  return to;
}


/**
 *
 */
torrent_t *
torrent_create_from_hash(const uint8_t *info_hash, const char *initiator)
{
  torrent_t *to;
  char errbuf[512];

  LIST_FOREACH(to, &torrents, to_link)
    if(!memcmp(to->to_info_hash, info_hash, 20))
      break;

  if(to == NULL)
    to = torrent_create(info_hash, initiator);

  if(to->to_metainfo == NULL) {
    buf_t *b = torrent_diskio_load_infofile_from_hash(info_hash);
    if(b != NULL) {

      torrent_trace(to, "Trying to initialize torrent from disk cache");

      htsmsg_t *doc = bencode_deserialize(buf_cstr(b),
                                          buf_cstr(b) + buf_size(b),
                                          errbuf, sizeof(errbuf),
                                          NULL, NULL, NULL);

      if(doc != NULL) {

        if(!torrent_parse_torrentfile(to, doc, errbuf, sizeof(errbuf))) {
          to->to_metainfo = b; // ownership tranfered
          b = NULL;
          torrent_diskio_open(to);
          torrent_trace(to, "Torrent initialized from disk cache");
        } else {
          torrent_trace(to, "Failed to decode on-disk metainfo -- %s",
                        errbuf);
        }
        htsmsg_release(doc);
      } else {
        torrent_trace(to, "Failed to decode on-disk metainfo -- %s",
                      errbuf);
      }
      buf_release(b);
    }
  }
  return to;
}


/**
 *
 */
torrent_t *
torrent_create_from_infofile(buf_t *b, char *errbuf, size_t errlen)
{
  torrent_t *to;
  uint8_t info_hash[20] = {0};

  htsmsg_t *doc = bencode_deserialize(buf_cstr(b), buf_cstr(b) + buf_size(b),
                                      errbuf, errlen,
                                      torrent_extract_info_hash, info_hash,
                                      NULL);

  if(doc == NULL)
    return NULL;

  LIST_FOREACH(to, &torrents, to_link)
    if(!memcmp(to->to_info_hash, info_hash, 20))
      break;

  if(to == NULL)
    to = torrent_create(info_hash, "infofile");

  if(to->to_metainfo == NULL) {
    if(torrent_parse_torrentfile(to, doc, errbuf, errlen)) {
      htsmsg_release(doc);
      return NULL; // Torrent will be garbage collected later
    }

    to->to_metainfo = buf_retain(b);
    torrent_diskio_open(to);
  }

  htsmsg_release(doc);
  return to;
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
void
torrent_retain(torrent_t *to)
{
  to->to_refcount++;
}


/**
 *
 */
void
torrent_release(torrent_t *to)
{
  assert(to->to_refcount > 0);
  to->to_refcount--;

  /**
   * Actual destruction will take place in torrent_periodic()
   * since it must happen on the async io thread
   */
}


/**
 *
 */
static void
torrent_destroy_files(torrent_t *to)
{
  torrent_file_t *tf, *next;
  for(tf = TAILQ_FIRST(&to->to_files); tf != NULL; tf = next) {
    next = TAILQ_NEXT(tf, tf_torrent_link);
    free(tf->tf_fullpath);
    free(tf->tf_name);
    assert(LIST_FIRST(&tf->tf_fhs) == NULL);
    free(tf);
  }
}


/**
 *
 */
static void
torrent_destroy(torrent_t *to)
{
  torrent_trace(to, "Torrent destroyed");

  // Clean up files

  assert(LIST_FIRST(&to->to_fhs) == NULL);
  torrent_destroy_files(to);

  // Shutdown peers

  peer_shutdown_all(to);
  assert(LIST_FIRST(&to->to_running_peers) == NULL);
  assert(LIST_FIRST(&to->to_unchoked_peers) == NULL);
  assert(TAILQ_FIRST(&to->to_inactive_peers) == NULL);
  assert(TAILQ_FIRST(&to->to_disconnected_peers) == NULL);
  assert(TAILQ_FIRST(&to->to_connect_failed_peers) == NULL);


  // Flush all active pieces

  torrent_piece_t *tp;

  while((tp = TAILQ_FIRST(&to->to_active_pieces)) != NULL) {

    torrent_block_t *tb;
    while((tb = LIST_FIRST(&tp->tp_waiting_blocks)) != NULL)
      block_destroy(tb);
    while((tb = LIST_FIRST(&tp->tp_sent_blocks)) != NULL)
      block_destroy(tb);

    torrent_piece_destroy(to, tp);
  }

  assert(to->to_active_pieces_mem == 0);
  assert(to->to_num_active_pieces == 0);
  assert(LIST_FIRST(&to->to_serve_order) == NULL);

  // Unannounce on trackers

  tracker_remove_torrent(to);

  // Close file

  torrent_diskio_close(to);

  // Final cleanup

  LIST_REMOVE(to, to_link);

  buf_release(to->to_metainfo);
  free(to->to_cachefile_piece_map);
  free(to->to_cachefile_piece_map_inv);
  free(to->to_piece_hashes);
  free(to->to_title);
  free(to);
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

    peer_cancel_request(tr);
  }
}

/**
 *
 */
static void
add_contributor(torrent_piece_t *tp, peer_t *p)
{
  piece_peer_t *pp;
  LIST_FOREACH(pp, &tp->tp_peers, pp_piece_link)
    if(pp->pp_peer == p)
      return;

  pp = malloc(sizeof(piece_peer_t));
  pp->pp_bad = 0;
  pp->pp_tp = tp;
  pp->pp_peer = p;
  LIST_INSERT_HEAD(&tp->tp_peers, pp, pp_piece_link);
  LIST_INSERT_HEAD(&p->p_pieces, pp, pp_peer_link);
}


/**
 *
 */
void
torrent_piece_peer_destroy(piece_peer_t *pp)
{
  LIST_REMOVE(pp, pp_piece_link);
  LIST_REMOVE(pp, pp_peer_link);
  free(pp);
}


/**
 *
 */
static void
torrent_piece_remove_contributors(torrent_piece_t *tp, int hash_ok)
{
  piece_peer_t *pp;
  while((pp = LIST_FIRST(&tp->tp_peers)) != NULL)
    torrent_piece_peer_destroy(pp);
}


/**
 *
 */
static void
torrent_piece_mark_contributors(torrent_piece_t *tp)
{
  piece_peer_t *pp;
  LIST_FOREACH(pp, &tp->tp_peers, pp_piece_link)
    pp->pp_bad = 1;
}




/**
 *
 */
void
torrent_receive_block(torrent_block_t *tb, const void *buf,
                      int begin, int len, torrent_t *to, peer_t *p)
{
  int second = async_current_time() / 1000000;

  torrent_piece_t *tp = tb->tb_piece;

  tp->tp_downloaded_bytes += len;
  average_fill(&tp->tp_download_rate, second, tp->tp_downloaded_bytes);

  memcpy(tp->tp_data + begin, buf, len);

  add_contributor(tp, p);

  // If there are any other requests for this block, cancel them
  block_cancel_requests(tb);
  block_destroy(tb);

  if(LIST_FIRST(&tp->tp_waiting_blocks) == NULL &&
     LIST_FIRST(&tp->tp_sent_blocks) == NULL) {

    // Piece complete

    tp->tp_complete = 1;
    torrent_hash_wakeup();
  }
  torrent_io_do_requests(to);
}



/**
 *
 */
void
torrent_attempt_more_peers(torrent_t *to)
{
  if(to->to_active_peers  >= btg.btg_max_peers_torrent ||
     btg.btg_active_peers >= btg.btg_max_peers_global)
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
int
torrent_parse_infodict(torrent_t *to, htsmsg_t *info,
                       char *errbuf, size_t errlen)
{
  if(gconf.enable_torrent_debug) {
    TRACE(TRACE_DEBUG, "BITTORRENT", "%s: Decoded metadata", to->to_title);
    htsmsg_print("BITTORRENT", info);
  }

  const char *name = htsmsg_get_str(info, "name");
  if(name != NULL)
    mystrset(&to->to_title, name);

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
  if(to->to_piece_length < 32768 || to->to_piece_length > 16777216) {
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
  const int mapsize = to->to_num_pieces * sizeof(uint32_t);
  to->to_cachefile_piece_map = malloc(mapsize);
  to->to_cachefile_piece_map_inv = malloc(mapsize);
  memset(to->to_cachefile_piece_map, 0xff, mapsize);
  memset(to->to_cachefile_piece_map_inv, 0xff, mapsize);

  to->to_piece_hashes = malloc(pieces_size);
  memcpy(to->to_piece_hashes, pieces_data, pieces_size);

  return 0;
}


/**
 *
 */
static int
torrent_parse_torrentfile(torrent_t *to, htsmsg_t *metainfo,
                          char *errbuf, size_t errlen)
{
  htsmsg_t *al = htsmsg_get_list(metainfo, "announce-list");
  if(al != NULL) {
    htsmsg_field_t *f;
    HTSMSG_FOREACH(f, al) {
      htsmsg_t *l = f->hmf_childs;
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

  htsmsg_t *info = htsmsg_get_map(metainfo, "info");
  if(info == NULL) {
    snprintf(errbuf, errlen, "Missing info dict");
    return 1;
  }

  return torrent_parse_infodict(to, info, errbuf, errlen);
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
static void
torrent_piece_enqueue_requests(torrent_t *to, torrent_piece_t *tp)
{
  assert(LIST_FIRST(&tp->tp_waiting_blocks) == NULL);
  assert(LIST_FIRST(&tp->tp_sent_blocks) == NULL);

  for(int i = 0; i < tp->tp_piece_length; i += TORRENT_REQ_SIZE) {
    torrent_block_t *tb = calloc(1, sizeof(torrent_block_t));
    tb->tb_piece = tp;
    LIST_INSERT_HEAD(&tp->tp_waiting_blocks, tb, tb_piece_link);
    tb->tb_begin = i;
    tb->tb_length = MIN(TORRENT_REQ_SIZE, tp->tp_piece_length - i);
  }

  to->to_need_updated_interest = 1;
  asyncio_wakeup_worker(torrent_pendings_signal);
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
  tp->tp_refcount = 1;
  tp->tp_index = piece_index;
  TAILQ_INSERT_TAIL(&to->to_active_pieces, tp, tp_link);
  tp->tp_deadline = INT64_MAX;
  LIST_INSERT_SORTED(&to->to_serve_order, tp, tp_serve_link, tp_deadline_cmp,
                     torrent_piece_t);

  tp->tp_data = malloc(to->to_piece_length);
  tp->tp_piece_length = to->to_piece_length;

  if(piece_index == to->to_num_pieces - 1) {
    // Last piece, truncate piece length
    tp->tp_piece_length = to->to_total_length % to->to_piece_length;
  }
  to->to_active_pieces_mem += tp->tp_piece_length;


  if(to->to_cachefile_piece_map[piece_index] != -1) {
    // We have this piece on disk, signal that we want to load it
    tp->tp_load_req = 1;
    // and wakeup diskio thread
    torrent_diskio_wakeup();
    return tp;
  }

  torrent_piece_enqueue_requests(to, tp);

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
  LIST_INSERT_SORTED(&to->to_serve_order, tp, tp_serve_link, tp_deadline_cmp,
                     torrent_piece_t);
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

    LIST_INSERT_HEAD(&tp->tp_active_fh, tfh, tfh_piece_link);

    piece_update_deadline(to, tp);

    if(!tp->tp_hash_computed) {
      asyncio_wakeup_worker(torrent_pendings_signal);

      while(!tp->tp_hash_ok && !tfh->tfh_cancelled)
        hts_cond_wait(&torrent_piece_verified_cond, &bittorrent_mutex);
    }

    LIST_REMOVE(tfh, tfh_piece_link);
    piece_update_deadline(to, tp);

    if(tfh->tfh_cancelled)
      return -1;

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
update_interest(torrent_t *to)
{
  peer_t *p;

  LIST_FOREACH(p, &to->to_running_peers, p_running_link)
    peer_update_interest(to, p);
}


/**
 *
 */
static void
add_request(torrent_block_t *tb, peer_t *p, int64_t now)
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

  tr->tr_send_time = now;
  tr->tr_req_num = tb->tb_req_tally;
  tb->tb_req_tally++;

  LIST_INSERT_HEAD(&tb->tb_requests, tr, tr_block_link);
  peer_send_request(p, tb);

  tr->tr_peer = p;
  tr->tr_qdepth = p->p_active_requests;

  if(LIST_FIRST(&p->p_requests) == NULL)
    p->p_torrent->to_peers_with_outstanding_requests++;

  LIST_INSERT_HEAD(&p->p_requests, tr, tr_peer_link);
  p->p_active_requests++;

  p->p_last_send = now;
}


/**
 *
 */
static int
check_peer_bad(const torrent_piece_t *tp, const peer_t *p)
{
  const piece_peer_t *pp;
  LIST_FOREACH(pp, &tp->tp_peers, pp_piece_link)
    if(pp->pp_bad && pp->pp_peer == p)
      return 1;
  return 0;
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

    if(check_peer_bad(tp, p))
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

    if(check_peer_bad(tp, p))
      continue;

    if(p->p_active_requests < p->p_maxq / 2)
      return p;
  }
  return NULL;
}


#if 0
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
serve_waiting_blocks(torrent_t *to, torrent_piece_t *tp, int optimal,
                     int64_t now)
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

      add_request(tb, p, now);

    } else {
      peer_t *p = find_any_peer(to, tp);
      if(p == NULL)
	break;

      add_request(tb, p, now);

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
		 int64_t eta_to_beat, int64_t now)
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

    int64_t t = now + p->p_block_delay * 2;

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
		      int64_t deadline, int64_t now)
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
    if(eta < now) {
      // Didn't arrive on time, assume the delay will worse
      // the longer it takes
      delay = now - eta;
      eta += delay * 2;
    }

    if(eta < deadline)
      continue; // Nothing to worry about

    // Now, let's see if we can find a peer that we think can beat
    // the current (offsetted) ETA for this block

    peer_t *p = find_faster_peer(to, tb, eta, now);
    if(p == NULL)
      continue;

#if 0
    if(0)
    printf("Block %s: Added dup request on peer %s bd:%d "
	   "computed ETA:%ld delay:%ld\n",
	   block_name(tb), p->p_name, p->p_block_delay,
	   eta - async_now, delay);
#endif

    add_request(tb, p, now);

    int new_delay = now - cur->tr_send_time;
    if(new_delay > curpeer->p_block_delay) {
      curpeer->p_block_delay = (curpeer->p_block_delay * 7 + new_delay) / 8;
    }
  }
}


/**
 *
 */
void
torrent_piece_release(torrent_piece_t *tp)
{
  tp->tp_refcount--;
  if(tp->tp_refcount > 0)
    return;

  torrent_piece_remove_contributors(tp, 0);

  free(tp->tp_data);
  free(tp);
}


/**
 *
 */
static void
torrent_piece_destroy(torrent_t *to, torrent_piece_t *tp)
{
  assert(LIST_FIRST(&tp->tp_active_fh) == NULL);
  assert(LIST_FIRST(&tp->tp_waiting_blocks) == NULL);
  assert(LIST_FIRST(&tp->tp_sent_blocks) == NULL);
  to->to_active_pieces_mem -= tp->tp_piece_length;
  to->to_num_active_pieces--;

  TAILQ_REMOVE(&to->to_active_pieces, tp, tp_link);
  LIST_REMOVE(tp, tp_serve_link);

  torrent_piece_release(tp);
}


/**
 *
 */
static void
flush_active_pieces(torrent_t *to)
{
  torrent_piece_t *tp, *next;

  for(tp = TAILQ_FIRST(&to->to_active_pieces); tp != NULL; tp = next) {
    next = TAILQ_NEXT(tp, tp_link);

    if(to->to_active_pieces_mem <= 32 * 1024 * 1024)
      break;

    if(tp->tp_load_req)
      continue;

    if(LIST_FIRST(&tp->tp_active_fh) != NULL)
      continue;

    if(LIST_FIRST(&tp->tp_waiting_blocks) != NULL)
      continue;

    if(LIST_FIRST(&tp->tp_sent_blocks) != NULL)
      continue;

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
    assert(pid < to->to_num_pieces);
    LIST_FOREACH(p, &to->to_running_peers, p_running_link) {

      if(p->p_piece_flags == NULL)
        p->p_piece_flags = calloc(1, to->to_num_pieces);

      if(p->p_piece_flags[pid] & PIECE_NOTIFIED)
        continue;
      peer_send_have(p, pid);
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
  LIST_FOREACH(p, &to->to_running_peers, p_running_link) {

    int choke = 1;

    if(p->p_num_pieces_have != to->to_num_pieces &&
       p->p_peer_interested)
      choke = 0;

    peer_choke(p, choke);
  }
}


/**
 *
 */
void
torrent_io_do_requests(torrent_t *to)
{
  torrent_piece_t *tp;
  //  int64_t deadline = INT64_MAX;

  int64_t now = async_current_time();

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
    check_active_requests(to, tp, tp->tp_deadline, now);
  }

  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link) {
    //    deadline = MIN(tp->tp_deadline, deadline);
    serve_waiting_blocks(to, tp, 1, now);
  }

  LIST_FOREACH(tp, &to->to_serve_order, tp_serve_link)
    serve_waiting_blocks(to, tp, 0, now);
}


/**
 *
 */
static void
torrent_reload_corrupt_pieces(torrent_t *to)
{
  torrent_piece_t *tp;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(tp->tp_hash_computed && !tp->tp_hash_ok) {

      /**
       * Setting hash_computed to 0 again basically means that
       * the piece is not verified so noone will annouce it, etc
       */
      tp->tp_hash_computed = 0;

      if(tp->tp_on_disk) {
        tp->tp_on_disk = 0;
        /**
         * This is pretty easy, we just create and enqueue the requests
         * (we never did this in the first place if we figured we
         * had the piece on disk
         */

        torrent_trace(to, "Got corrupt piece %d from disk", tp->tp_index);
      } else {
        torrent_trace(to, "Got corrupt piece %d from network", tp->tp_index);
      }
      torrent_piece_enqueue_requests(to, tp);
    }
  }
}


/**
 *
 */
static void
torrent_reload_loadfail_pieces(torrent_t *to)
{
  torrent_piece_t *tp;

  TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {
    if(tp->tp_loadfail) {
      tp->tp_loadfail = 0;
      torrent_piece_enqueue_requests(to, tp);
    }
  }
}


/**
 *
 */
static void
torrent_check_pendings(void)
{
  hts_mutex_lock(&bittorrent_mutex);

  torrent_t *to;
  LIST_FOREACH(to, &torrents, to_link) {

    if(to->to_new_valid_piece) {
      to->to_new_valid_piece = 0;
      torrent_send_have(to);
    }

    if(to->to_need_updated_interest) {
      to->to_need_updated_interest = 0;
      update_interest(to);
    }

    if(to->to_corrupt_piece) {
      to->to_corrupt_piece = 0;
      torrent_reload_corrupt_pieces(to);
    }

    if(to->to_loadfail) {
      to->to_loadfail = 0;
      torrent_reload_loadfail_pieces(to);
    }

    torrent_io_do_requests(to);
  }
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 * Run every second for each torrent
 */
static void
torrent_periodic_one(torrent_t *to, int second)
{
  int rate = average_read(&to->to_download_rate, second) / 125;

  torrent_fh_t *tfh;

  const tracker_torrent_t *tt;
  int seeders = 0;
  int leechers = 0;

  LIST_FOREACH(tt, &to->to_trackers, tt_torrent_link) {
    seeders  = MAX(seeders,  tt->tt_seeders);
    leechers = MAX(leechers, tt->tt_leechers);
  }

  LIST_FOREACH(tfh, &to->to_fhs, tfh_torrent_link) {
    if(tfh->tfh_fa_stats != NULL) {
      prop_set(tfh->tfh_fa_stats, "bitrate", PROP_SET_INT, rate);
      prop_set_int(tfh->tfh_known_peers, to->to_num_peers);
      prop_set_int(tfh->tfh_connected_peers, to->to_active_peers);
      prop_set_int(tfh->tfh_torrent_seeders, seeders);
      prop_set_int(tfh->tfh_torrent_leechers, leechers);
      prop_set_int(tfh->tfh_recv_peers, to->to_peers_with_outstanding_requests);
    }
  }

  flush_active_pieces(to);

  if(to->to_last_unchoke_check + 5 < second) {
    to->to_last_unchoke_check = second;
    torrent_unchoke_peers(to);
  }
}


/**
 * Run every second
 */
static void
torrent_periodic(void *aux)
{
  const int second = async_current_time() / 1000000;

  hts_mutex_lock(&bittorrent_mutex);

  torrent_t *to, *next;
  for(to = LIST_FIRST(&torrents); to != NULL; to = next) {
    next = LIST_NEXT(to, to_link);
    if(to->to_refcount == 0) {
      torrent_destroy(to);
      continue;
    }

    torrent_io_do_requests(to);
    torrent_periodic_one(to, second);
  }

  if(LIST_FIRST(&torrents) != NULL)
    asyncio_timer_arm_delta_sec(&torrent_periodic_timer, 1);
  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 * Start the periodic timer (we only want it to run when we actually
 * serve torrents). And we create torrents on non-asyncio thread
 * so we need this helper to arm the timer on the correct thread
 */
static void
torrent_boot_periodic(void)
{
  hts_mutex_lock(&bittorrent_mutex);
  if(LIST_FIRST(&torrents) != NULL &&
     !asyncio_timer_is_armed(&torrent_periodic_timer)) {
    asyncio_timer_arm_delta_sec(&torrent_periodic_timer, 1);
  }
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

  torrent_retain(to);
  tp->tp_refcount++;

  hts_mutex_unlock(&bittorrent_mutex);
  int64_t ts = arch_get_ts();
  sha1_init(shactx);
  sha1_update(shactx, tp->tp_data, tp->tp_piece_length);
  sha1_final(shactx, digest);
  ts = arch_get_ts() - ts;
  hts_mutex_lock(&bittorrent_mutex);

  tp->tp_hash_computed = 1;


  const uint8_t *piecehash = to->to_piece_hashes + tp->tp_index * 20;
  tp->tp_hash_ok = !memcmp(piecehash, digest, 20);
  torrent_trace(to, "Hash check on piece %d %s",
                tp->tp_index, tp->tp_hash_ok ? "OK" : "FAIL");

  if(tp->tp_hash_ok) {
    to->to_new_valid_piece = 1;
    torrent_piece_remove_contributors(tp, 1);
  } else {
    torrent_piece_mark_contributors(tp);
    to->to_corrupt_piece = 1;
    tp->tp_complete = 0;
  }

  asyncio_wakeup_worker(torrent_pendings_signal);

  if(tp->tp_hash_ok && to->to_cachefile != NULL)
    torrent_diskio_wakeup();

  torrent_piece_release(tp);
  torrent_release(to);

  hts_cond_broadcast(&torrent_piece_verified_cond);

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
          /**
           * 'to' may be invalid here because we have unlocked so restart
           * from begining
           */
	  goto restart;
	}
      }
    }

    if(hts_cond_wait_timeout(&torrent_piece_hash_needed_cond,
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
void
torrent_hash_wakeup(void)
{
  if(!torrent_hash_thread_running) {
    torrent_hash_thread_running = 1;
    hts_thread_create_detached("bthasher", bt_hash_thread, NULL,
			       THREAD_PRIO_BGTASK);
  }
  hts_cond_signal(&torrent_piece_hash_needed_cond);
}


/**
 *
 */
static void
torrent_check_metainfo(void)
{
  torrent_t *to;
  peer_t *p;
  metainfo_request_t *mr;

  hts_mutex_lock(&bittorrent_mutex);

  LIST_FOREACH(to, &torrents, to_link) {
    LIST_FOREACH(p, &to->to_running_peers, p_running_link) {
      LIST_FOREACH(mr, &p->p_metainfo_requests, mr_peer_link) {
        if(mr->mr_state == MR_PENDING_SEND)
          peer_send_metainfo_request(p, mr);
      }
    }
  }

  hts_mutex_unlock(&bittorrent_mutex);
}


/**
 *
 */
void
torrent_wakeup_for_metadata_requests(void)
{
  asyncio_wakeup_worker(torrent_metainfo_signal);
}

/**
 *
 */
static void
torrent_early_init(void)
{
  btg.btg_max_peers_global = 200;
  btg.btg_max_peers_torrent = 50;

  asyncio_timer_init(&torrent_periodic_timer, torrent_periodic, NULL);

  torrent_pendings_signal = asyncio_add_worker(torrent_check_pendings);
  torrent_boot_periodic_signal = asyncio_add_worker(torrent_boot_periodic);
  torrent_metainfo_signal = asyncio_add_worker(torrent_check_metainfo);

  hts_cond_init(&torrent_piece_hash_needed_cond, &bittorrent_mutex);
  hts_cond_init(&torrent_piece_io_needed_cond, &bittorrent_mutex);
  hts_cond_init(&torrent_piece_verified_cond, &bittorrent_mutex);
  hts_cond_init(&torrent_metainfo_available_cond, &bittorrent_mutex);
}

INITME(INIT_GROUP_NET, torrent_early_init, NULL, 0);


/**
 *
 */
static void
torrent_asyncio_init(void)
{
  hts_mutex_lock(&bittorrent_mutex);
  torrent_settings_init();
  hts_mutex_unlock(&bittorrent_mutex);

}

INITME(INIT_GROUP_ASYNCIO, torrent_asyncio_init, NULL, 0);
