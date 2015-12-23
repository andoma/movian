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
#include <limits.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "misc/sha.h"
#include "misc/bytestream.h"
#include "htsmsg/htsmsg.h"
#include "bittorrent.h"
#include "bencode.h"
#include "misc/minmax.h"


static int torrent_write_thread_running;

static void
diskio_trace(const torrent_t *t, const char *msg, ...)
  attribute_printf(2, 3);

static void
diskio_trace(const torrent_t *t, const char *msg, ...)
{
  if(!gconf.enable_torrent_diskio_debug)
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
static uint64_t
cache_file_size(const torrent_t *to, int blocks)
{
  return blocks * to->to_piece_length + to->to_cachefile_store_offset;
}


/**
 *
 */
static void
update_disk_avail()
{
  fa_fsinfo_t ffi;
  rstr_t *path = rstr_dup(btg.btg_cache_path);

  hts_mutex_unlock(&bittorrent_mutex);
  if(!fa_fsinfo(rstr_get(path), &ffi)) {
    hts_mutex_lock(&bittorrent_mutex);
    btg.btg_disk_avail = ffi.ffi_avail;
  } else {
    hts_mutex_lock(&bittorrent_mutex);
  }
  rstr_release(path);
}



/**
 *
 */
static void
update_disk_usage(void)
{
  const torrent_t *to;
  uint64_t active_total = 0;
  LIST_FOREACH(to, &torrents, to_link)
    active_total += cache_file_size(to, to->to_total_disk_blocks);

  btg.btg_total_bytes_active = active_total;

  const int64_t sum = btg.btg_total_bytes_inactive + btg.btg_total_bytes_active;
  int64_t limit = (btg.btg_disk_avail + sum) *
    btg.btg_free_space_percentage / 100;

  btg.btg_cache_limit = limit ?: 1;
  if(gconf.enable_torrent_diskio_debug) {
    TRACE(TRACE_DEBUG, "BITTORRENT",
          "Disk usage %"PRId64" MB / %"PRId64" MB (%d%%)",
          sum, btg.btg_cache_limit, (int)(sum * 100 / btg.btg_cache_limit));
  }

  rstr_t *r = _("Cached torrents use %d MB out of allowed %d MB. Total free space on volume: %d MB");

  char tmp[256];
  snprintf(tmp, sizeof(tmp), rstr_get(r),
           (int)(sum / 1000000),
           (int)(limit / 1000000),
           (int)( btg.btg_disk_avail / 1000000));

  prop_set_string(btg.btg_disk_status, tmp);
  rstr_release(r);
}


/**
 *
 */
static void
torrent_write_to_disk(torrent_t *to, torrent_piece_t *tp)
{
  torrent_retain(to);
  tp->tp_refcount++;

  uint8_t mapdata[4];
  int ok = 0;

  for(int attempt = 0; attempt < 2; attempt++) {

    update_disk_usage();

    int growth = MAX(to->to_next_disk_block + 1 - to->to_total_disk_blocks, 0);

    if(btg.btg_total_bytes_active + btg.btg_total_bytes_inactive +
       growth * to->to_piece_length >= btg.btg_cache_limit) {

      diskio_trace(to, "Write would exceed cache size, need to cleanup");
      if(torrent_diskio_scan(0)) {
        // Managed to clean up something
        continue;
      }
      // Otherwise, just restart in our file 50% back
      to->to_next_disk_block /= 2;
      growth = MAX(to->to_next_disk_block + 1 - to->to_total_disk_blocks, 0);
    }


    int location = to->to_next_disk_block;
    wr32_be(mapdata, to->to_next_disk_block);
    to->to_next_disk_block++;


    if(growth > 0) {
      btg.btg_disk_avail -= growth * to->to_piece_length;
      to->to_total_disk_blocks = to->to_next_disk_block;
    }

    const int old_piece = to->to_cachefile_piece_map_inv[location];
    uint64_t old_map_offset = 0;
    if(old_piece != -1) {
      // Some other block already occupied this slot in the file
      // We need to clear that out

      old_map_offset =
        sizeof(uint32_t) * old_piece + to->to_cachefile_map_offset;

      to->to_cachefile_piece_map[old_piece] = -1;
    }

    const int old_pos = to->to_cachefile_piece_map[tp->tp_index];
    if(old_pos != -1) {
      // Piece was already written to another location.
      // We are writing again, probably due to hash corruption.
      // Clear out inverse table info
      to->to_cachefile_piece_map_inv[old_pos] = -1;
    }

    to->to_cachefile_piece_map[tp->tp_index] = location;
    to->to_cachefile_piece_map_inv[location] = tp->tp_index;


    uint64_t data_offset =
      location * to->to_piece_length + to->to_cachefile_store_offset;

    uint64_t map_offset =
      sizeof(uint32_t) * tp->tp_index + to->to_cachefile_map_offset;

    hts_mutex_unlock(&bittorrent_mutex);

    if(fa_seek(to->to_cachefile, data_offset, SEEK_SET) == data_offset) {
      if(fa_write(to->to_cachefile, tp->tp_data, tp->tp_piece_length) ==
         tp->tp_piece_length) {

        if(fa_seek(to->to_cachefile, map_offset, SEEK_SET) == map_offset) {

          if(fa_write(to->to_cachefile, mapdata, 4) == 4) {
            ok = 1;
          }
        }
      }
    }

    if(old_map_offset) {
      if(fa_seek(to->to_cachefile, old_map_offset, SEEK_SET) ==
         old_map_offset) {
        memset(mapdata, 0xff, 4);
        fa_write(to->to_cachefile, mapdata, 4);
      }
    }

    hts_mutex_lock(&bittorrent_mutex);

    diskio_trace(to, "Wrote piece %d to disk at %d (%"PRId64"). Result: %s",
                 tp->tp_index, location, data_offset, ok ? "OK" : "FAIL");
    break;
  }

  if(ok) {
    tp->tp_on_disk = 1;
  } else {
    tp->tp_disk_fail = 1;
  }

  torrent_piece_release(tp);
  torrent_release(to);
}



/**
 *
 */
static void
torrent_read_from_disk(torrent_t *to, torrent_piece_t *tp)
{
  torrent_retain(to);
  tp->tp_refcount++;

  int idx = to->to_cachefile_piece_map[tp->tp_index];
  int ok;

  if(idx >= 0) {
    uint64_t data_offset =
      idx * to->to_piece_length + to->to_cachefile_store_offset;

    hts_mutex_unlock(&bittorrent_mutex);
    fa_seek(to->to_cachefile, data_offset, SEEK_SET);
    int len = fa_read(to->to_cachefile, tp->tp_data, tp->tp_piece_length);
    hts_mutex_lock(&bittorrent_mutex);
    ok = len == tp->tp_piece_length;

    diskio_trace(to, "Load piece %d from disk: %s",
                 tp->tp_index, ok ? "OK" : "FAIL");
  } else {
    // Piece no longer exist on disk. We fail silently here and just
    // let the torrent streamer reload it
    ok = 0;
  }

  tp->tp_load_req = 0;

  if(ok) {
    tp->tp_complete = 1;
    tp->tp_on_disk = 1;
    torrent_hash_wakeup();
  } else {
    tp->tp_loadfail = 1;
    to->to_loadfail = 1;
  }
  torrent_piece_release(tp);
  torrent_release(to);
}


/**
 *
 */
static void *
bt_diskio_thread(void *aux)
{
  torrent_t *to;

  hts_mutex_lock(&bittorrent_mutex);

  while(1) {

  restart:

    update_disk_avail();

    LIST_FOREACH(to, &torrents, to_link) {
      if(to->to_cachefile == NULL)
        continue;

      torrent_piece_t *tp;
      TAILQ_FOREACH(tp, &to->to_active_pieces, tp_link) {

        if(tp->tp_load_req) {
          torrent_read_from_disk(to, tp);
          goto restart;
        }

	if(tp->tp_hash_ok && !tp->tp_on_disk && !tp->tp_disk_fail) {
	  torrent_write_to_disk(to, tp);
	  goto restart;
	}
      }
    }

    if(hts_cond_wait_timeout(&torrent_piece_io_needed_cond,
			     &bittorrent_mutex, 60000))
      break;
  }

  torrent_write_thread_running = 0;
  hts_mutex_unlock(&bittorrent_mutex);
  return NULL;
}


/**
 *
 */
void
torrent_diskio_wakeup(void)
{
  if(!torrent_write_thread_running) {
    torrent_write_thread_running = 1;
    hts_thread_create_detached("btdiskio", bt_diskio_thread, NULL,
                               THREAD_PRIO_BGTASK);
  }
  hts_cond_signal(&torrent_piece_io_needed_cond);
}





/**
 *
 */
static int
torrent_diskio_verify(torrent_t *to)
{
  fa_handle_t *fh = to->to_cachefile;

  fa_seek(fh, 0, SEEK_SET);
  int64_t size = fa_fsize(fh);

  uint8_t tmp[8];

  if(size < sizeof(tmp))
    return -1;

  if(fa_read(fh, tmp, sizeof(tmp)) != sizeof(tmp)) {
    diskio_trace(to, "Unable to read bencode size");
    return -1;
  }

  uint32_t magic = rd32_be(tmp);
  if(magic != 'bt02') {
    diskio_trace(to, "Bad magic 0x%08x", magic);
    return -1;
  }

  int bencodesize = rd32_be(tmp+4);
  diskio_trace(to, "Size of metainfo: %d", bencodesize);

  if(size < bencodesize + sizeof(tmp) || bencodesize > 1024 * 1024) {
    diskio_trace(to, "Bad bencode size %d for filesize %"PRId64,
                 bencodesize, size);
    return -1;
  }

  buf_t *b = buf_create(bencodesize);
  if(fa_read(fh, buf_str(b), bencodesize) != bencodesize) {
    diskio_trace(to, "Unable to read metainto");
    buf_release(b);
    return -1;
  }

  char errbuf[256];
  uint8_t info_hash[20] = {0};

  htsmsg_t *doc = bencode_deserialize(buf_cstr(b), buf_cstr(b) + buf_size(b),
                                      errbuf, sizeof(errbuf),
                                      torrent_extract_info_hash, info_hash,
                                      NULL);

  buf_release(b);
  if(doc == NULL) {
    diskio_trace(to, "Unable to parse metainto: %s", errbuf);
    return -1;
  }

  htsmsg_release(doc);

  if(memcmp(info_hash, to->to_info_hash, 20)) {
    diskio_trace(to, "Metainfo hash mismatch for file on disk");
    return -1;
  }

  const int mapsize = to->to_num_pieces * sizeof(uint32_t);

  if(fa_read(fh, to->to_cachefile_piece_map, mapsize) != mapsize) {
    diskio_trace(to, "Unable to read piece flags, clearing all on-disk pieces");
    memset(to->to_cachefile_piece_map, 0xff, mapsize);
  }

  memset(to->to_cachefile_piece_map_inv, 0xff, mapsize);

  int cnt = 0;
  int max_block = -1;
  for(int i = 0; i < to->to_num_pieces; i++) {
    unsigned int location = rd32_be((void *)&to->to_cachefile_piece_map[i]);

    if(location >= to->to_num_pieces)
      location = -1;

    to->to_cachefile_piece_map[i] = location;
    if(to->to_cachefile_piece_map[i] != -1) {
      cnt++;
      max_block = MAX(max_block, to->to_cachefile_piece_map[i]);
      to->to_cachefile_piece_map_inv[location] = i;
    }
  }

  to->to_next_disk_block = max_block + 1;
  to->to_total_disk_blocks = to->to_next_disk_block;

  diskio_trace(to, "%d pieces valid on disk", cnt);

  to->to_cachefile_map_offset = 8 + bencodesize;
  to->to_cachefile_store_offset =
    to->to_cachefile_map_offset + to->to_num_pieces * sizeof(uint32_t);

  return 0;
}


/**
 *
 */
void
torrent_diskio_open(torrent_t *to)
{
  char errbuf[256];
  char path[PATH_MAX];
  char str[41];
  bin2hex(str, sizeof(str), to->to_info_hash, 20);

  snprintf(path, sizeof(path), "%s/%s.tc",
           rstr_get(btg.btg_cache_path), str);

  fa_makedirs(rstr_get(btg.btg_cache_path), NULL, 0);

  assert(to->to_cachefile == NULL);

  to->to_cachefile = fa_open_ex(path, errbuf, sizeof(errbuf),
                                FA_APPEND | FA_WRITE, NULL);
  if(to->to_cachefile == NULL) {
    TRACE(TRACE_ERROR, "BITTORRENT", "Unable to open cache file %s -- %s",
          path, errbuf);
    return;
  }


  if(!torrent_diskio_verify(to)) {
    diskio_trace(to, "File %s seems valid", path);
  } else {

    fa_seek(to->to_cachefile, 0, SEEK_SET);
    uint8_t tmp[8];
    wr32_be(tmp, 'bt02');
    wr32_be(tmp + 4, buf_size(to->to_metainfo));

    if(fa_write(to->to_cachefile, tmp, 8) != 8)
      goto err;

    int bencodesize = buf_size(to->to_metainfo);

    if(fa_write(to->to_cachefile, buf_cstr(to->to_metainfo),
                bencodesize) != bencodesize)
      goto err;

    const int mapsize = to->to_num_pieces * sizeof(uint32_t);

    void *ff = malloc(mapsize);
    memset(ff, 0xff, mapsize);

    int err = fa_write(to->to_cachefile, ff, mapsize) != mapsize;
    free(ff);
    if(err)
      goto err;

    to->to_cachefile_map_offset = 8 + bencodesize;
    to->to_cachefile_store_offset =
      to->to_cachefile_map_offset + to->to_num_pieces * sizeof(uint32_t);

    to->to_next_disk_block = 0;
    diskio_trace(to, "New disk cache initialized at %s", path);
  }
  diskio_trace(to, "Disk offsets: map:0x%x store:0x%x next block stored at %d",
               to->to_cachefile_map_offset,
               to->to_cachefile_store_offset,
               to->to_next_disk_block);
  return;

 err:
  TRACE(TRACE_ERROR, "BITTORRENT",
        "%s: Unable to create cachefile, running without diskcache",
        to->to_title);
  fa_close(to->to_cachefile);
  to->to_cachefile = NULL;
}


/**
 *
 */
void
torrent_diskio_close(torrent_t *to)
{
  if(to->to_cachefile == NULL)
    return;

  fa_close(to->to_cachefile);
  to->to_cachefile = NULL;
}

LIST_HEAD(scanned_file_list, scanned_file);

typedef struct scanned_file {
  LIST_ENTRY(scanned_file) sf_link;
  rstr_t *sf_url;
  uint8_t sf_info_hash[20];

  int64_t sf_size;
  time_t sf_mtime;

  int sf_active;

} scanned_file_t;


static int
sf_cmp(const scanned_file_t *a, const scanned_file_t *b)
{
  return a->sf_mtime - b->sf_mtime;
}

/**
 *
 */
int
torrent_diskio_scan(int force_flush)
{
  scanned_file_t *sf, *next;
  char tmp[41];
  char errbuf[256];
  struct scanned_file_list sfl;
  LIST_INIT(&sfl);

  if(btg.btg_cache_path == NULL)
    return 0;

  update_disk_avail();

  rstr_t *path = rstr_dup(btg.btg_cache_path);

  hts_mutex_unlock(&bittorrent_mutex);


  fa_dir_t *fd = fa_scandir(rstr_get(path), errbuf, sizeof(errbuf));
  tmp[40] = 0;
  if(fd != NULL) {
    fa_dir_entry_t *fde;
    RB_FOREACH(fde, &fd->fd_entries, fde_link) {
      const char *fname = rstr_get(fde->fde_filename);
      if(strlen(fname) != 43) // [40char sha].tc
        continue;

      if(strcmp(fname + 40, ".tc"))
        continue;
      memcpy(tmp, fname, 40);
      sf = calloc(1, sizeof(scanned_file_t));
      if(hex2bin(sf->sf_info_hash, 20, tmp) != 20) {
        free(sf);
        continue;
      }

      if(!fde->fde_statdone)
        fa_stat(rstr_get(fde->fde_url), &fde->fde_stat, NULL, 0);

      sf->sf_size  = fde->fde_stat.fs_size;
      sf->sf_mtime = fde->fde_stat.fs_mtime;
      sf->sf_url = rstr_dup(fde->fde_url);
      LIST_INSERT_SORTED(&sfl, sf, sf_link, sf_cmp, scanned_file_t);
    }

    fa_dir_free(fd);
  }

  hts_mutex_lock(&bittorrent_mutex);

  if(fd == NULL) {
    rstr_release(path);
    return 0;
  }

  btg.btg_total_bytes_inactive = 0;

  LIST_FOREACH(sf, &sfl, sf_link) {

    torrent_t *to = torrent_find_by_hash(sf->sf_info_hash);
    if(to != NULL) {
      sf->sf_active = 1;
    } else {
      btg.btg_total_bytes_inactive += sf->sf_size;
    }

    if(gconf.enable_torrent_diskio_debug)
      TRACE(TRACE_DEBUG, "BITTORRENT",
            "%s: %"PRId64" bytes %d seconds old\n",
            rstr_get(sf->sf_url),
            sf->sf_size, (int)(time(NULL) - sf->sf_mtime));
  }

  if(gconf.enable_torrent_diskio_debug) {
    TRACE(TRACE_DEBUG, "BITTORRENT",
          "Disk usage: Active: %"PRId64" MB, Inactive: %"PRId64" MB\n",
          btg.btg_total_bytes_active / 1000000,
          btg.btg_total_bytes_inactive / 1000000);

  }

  update_disk_usage();
  int rval = 0;
  for(sf = LIST_FIRST(&sfl); sf != NULL; sf = next) {
    next = LIST_NEXT(sf, sf_link);

    // Delete inactive torrents that exceed max limit

    if(!sf->sf_active) {

      if(force_flush ||
         btg.btg_total_bytes_active + btg.btg_total_bytes_inactive >=
         btg.btg_cache_limit) {
        if(fa_unlink(rstr_get(sf->sf_url), errbuf, sizeof(errbuf))) {
          TRACE(TRACE_ERROR, "BITTORRENT",
                "Unable to delete %s from cache -- %s",
                rstr_get(sf->sf_url), errbuf);
          rval = 1;
        } else {
          btg.btg_total_bytes_inactive -= sf->sf_size;


          if(gconf.enable_torrent_diskio_debug) {
            TRACE(TRACE_DEBUG, "BITTORRENT",
                  "Removed %s (%"PRId64" bytes) from cache",
                  rstr_get(sf->sf_url), sf->sf_size);
          }
        }
      }
    }

    rstr_release(sf->sf_url);
    free(sf);
  }
  rstr_release(path);
  update_disk_usage();
  return rval;
}



void
torrent_diskio_cache_clear(void)
{
  torrent_diskio_scan(1);
}


/**
 *
 */
buf_t *
torrent_diskio_load_infofile_from_hash(const uint8_t *req_hash)
{
  char path[PATH_MAX];
  char str[41];
  bin2hex(str, sizeof(str), req_hash, 20);

  snprintf(path, sizeof(path), "%s/%s.tc",
           rstr_get(btg.btg_cache_path), str);

  fa_handle_t *fh = fa_open(path, NULL, 0);

  if(fh == NULL)
    return NULL;

  uint8_t tmp[8];

  if(fa_read(fh, tmp, sizeof(tmp)) != sizeof(tmp))
    goto bad;

  uint32_t magic = rd32_be(tmp);
  if(magic != 'bt02')
    goto bad;

  unsigned int bencodesize = rd32_be(tmp+4);

  if(bencodesize > 1024 * 1024)
    goto bad;

  buf_t *b = buf_create(bencodesize);
  if(fa_read(fh, buf_str(b), bencodesize) != bencodesize)
    goto bad2;

  uint8_t disk_hash[20] = {0};

  htsmsg_t *doc = bencode_deserialize(buf_cstr(b), buf_cstr(b) + buf_size(b),
                                      NULL, 0,
                                      torrent_extract_info_hash, disk_hash,
                                      NULL);

  if(doc == NULL)
    goto bad2;

  htsmsg_release(doc);

  if(memcmp(req_hash, disk_hash, 20))
    goto bad2;

  return b;

 bad2:
  buf_release(b);
 bad:
  fa_close(fh);
  return NULL;
}
