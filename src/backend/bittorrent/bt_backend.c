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

#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "networking/http.h"

#include "fileaccess/fileaccess.h"

#include "bittorrent.h"
#include "misc/sha.h"

// http://www.bittorrent.org/beps/bep_0009.htm


static void
do_torrent_release(void *opaque, prop_sub_t *s)
{
  torrent_release(opaque);
  prop_unsubscribe(s);
}


/**
 *
 */
static void
torrent_release_on_prop_destroy(prop_t *p, torrent_t *to)
{
  prop_subscribe(PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_ROOT, p,
                 PROP_TAG_CALLBACK_DESTROYED, do_torrent_release, to,
                 PROP_TAG_MUTEX, &bittorrent_mutex,
                 NULL);
}

#if 0

/**
 *
 */
static void
torrent_browse(prop_t *page, torrent_t *to, const char *path)
{
  if(to->to_errbuf[0]) {
    nav_open_error(page, to->to_errbuf);
    return;
  }

  prop_t *model = prop_create_r(page, "model");
  prop_t *nodes = prop_create_r(model, "nodes");
  prop_t *metadata = prop_create_r(page, "metadata");

  const struct torrent_file_queue *tfq;
  const torrent_file_t *tf;

  if(path == NULL) {
    tfq = &to->to_root;
  } else {
    TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link)
      if(!strcmp(tf->tf_fullpath, path))
        break;

    if(tf == NULL) {
      nav_open_error(page, "No such file or directory");
      hts_mutex_unlock(&bittorrent_mutex);
      return;
    }
    tfq = &tf->tf_files;
  }

  char hashstr[41];
  bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);
  hashstr[40] = 0;

  TAILQ_FOREACH(tf, tfq, tf_parent_link) {
    prop_t *item = prop_create_root(NULL);
    prop_t *item_metadata = prop_create(item, "metadata");

    char url[1024];


    if(tf->tf_size)
      snprintf(url, sizeof(url), "torrentfile://%s/%s",
               hashstr, tf->tf_fullpath);
    else
      snprintf(url, sizeof(url), "infohash:%s/%s", hashstr, tf->tf_fullpath);

    prop_set(item, "url", PROP_SET_STRING, url);
    prop_set(item, "type", PROP_SET_STRING, tf->tf_size ? "file" : "directory");

    prop_set(item_metadata, "title", PROP_SET_STRING, tf->tf_name);

    if(prop_set_parent(item, nodes))
      prop_destroy(item);
  }


  prop_set(model, "type", PROP_SET_STRING, "directory");

  prop_ref_dec(model);
  prop_ref_dec(nodes);
  prop_ref_dec(metadata);
}
#endif


#if 0
/**
 *
 */
static int
magnet_open2(prop_t *page, struct http_header_list *list, int sync)
{
  const char *dn = http_header_get(list, "dn");

  const char *xt = http_header_get(list, "xt");
  if(xt == NULL)
    return nav_open_errorf(page, _("No 'xt' in magnet link"));

  const char *hash = mystrbegins(xt, "urn:btih:");
  if(hash == NULL)
    return nav_open_errorf(page, _("Unknown hash scheme: %s"), xt);

  uint8_t infohash[20];

  if(hex2bin(infohash, sizeof(infohash), hash) != sizeof(infohash))
    return nav_open_errorf(page, _("Invalid SHA-1 hash: %s"), hash);

  TRACE(TRACE_DEBUG, "MAGNET", "Opening magnet for hash %s -- %s",
	hash, dn ?: "<unknown name>");

  const char *trackers[20];
  int num_trackers = 0;
  http_header_t *hh;

  LIST_FOREACH(hh, list, hh_link) {
    if(num_trackers < 19 && !strcmp(hh->hh_key, "tr"))
      trackers[num_trackers++] = hh->hh_value;
  }
  trackers[num_trackers] = NULL;

  hts_mutex_lock(&bittorrent_mutex);
  torrent_t *to = torrent_create(infohash, dn, trackers, NULL);
  torrent_browse(page, to, NULL);
  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}


/**
 *
 */
static int
magnet_open(prop_t *page, const char *url0, int sync)
{
  if(*url0 == '?')
    url0++;

  char *url = mystrdupa(url0);

  struct http_header_list list = {};

  http_parse_uri_args(&list, url, 1);

  int r = magnet_open2(page, &list, sync);
  http_headers_free(&list);
  return r;
}


/**
 *
 */
static void
get_info_hash(void *opaque, const char *name, const void *data, size_t len)
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
static int
torrent_open(prop_t *page, const char *url0, int sync)
{
  char errbuf[512];
  buf_t *b = fa_load(url0,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     NULL);

  if(b == NULL)
    return nav_open_errorf(page, _("Unable to open torrent: %s"), errbuf);

  uint8_t infohash[20] = {0};

  htsmsg_t *doc = bencode_deserialize(buf_cstr(b), buf_cstr(b) + buf_size(b),
                                      errbuf, sizeof(errbuf),
                                      get_info_hash, infohash);

  if(doc == NULL) {
    buf_release(b);
    return nav_open_errorf(page, _("Unable to open torrent: %s"), errbuf);
  }

  buf_release(b);

  hts_mutex_lock(&bittorrent_mutex);
  torrent_t *to = torrent_create(infohash, NULL, NULL, doc);
  torrent_browse(page, to, NULL);
  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);

  htsmsg_release(doc);
  return 0;
}

#endif

/**
 *
 */
static int
torrent_movie_open(prop_t *page, const char *url0, int sync)
{
  char errbuf[512];
  buf_t *b = fa_load(url0,
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     NULL);

  if(b == NULL)
    return nav_open_errorf(page, _("Unable to load torrent: %s"), errbuf);

  hts_mutex_lock(&bittorrent_mutex);
  torrent_t *to = torrent_create(b, errbuf, sizeof(errbuf));
  buf_release(b);

  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return nav_open_errorf(page, _("Unable to parse torrent: %s"), errbuf);
  }

  // Find biggest file and redirect to that

  torrent_file_t *tf, *best = NULL;
  TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link)
    if(best == NULL || best->tf_size < tf->tf_size)
      best = tf;

  if(best == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return nav_open_errorf(page, _("No files in torrent"));
  }

  char url[1024];
  char hashstr[41];
  bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);
  hashstr[40] = 0;

  snprintf(url, sizeof(url), "torrentfile://%s/%s",
           hashstr, best->tf_fullpath);

  prop_t *eventsink = prop_create_r(page, "eventSink");
  event_t *e = event_create_str(EVENT_REDIRECT, url);
  prop_send_ext_event(eventsink, e);
  event_release(e);
  prop_ref_dec(eventsink);


  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}


#if 0
/**
 *
 */
static int
infohash_open(prop_t *page, const char *url, int sync)
{
  uint8_t infohash[20] = {0};

  if(strlen(url) < 40)
    return nav_open_error(page, "Short hash");

  const char *path = NULL;

  char *x = mystrdupa(url);

  if(x[40] != 0) {
    path = x + 40;
    if(*path != '/')
      path = NULL;
    path++;
    x[40] = 0;
  }

  if(hex2bin(infohash, sizeof(infohash), x) != 20)
    return nav_open_error(page, "Invalid hash");

  hts_mutex_lock(&bittorrent_mutex);
  torrent_t *to = torrent_create(infohash, NULL, NULL, NULL);
  torrent_browse(page, to, path);
  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}

#endif

/**
 *
 */
static int
bt_open(prop_t *page, const char *url, int sync)
{
  const char *u;

  if((u = mystrbegins(url, "torrent:movie:")) != NULL) {
    return torrent_movie_open(page, u, sync);
  }

  return 0;
}


/**
 *
 */
static int
bt_canhandle(const char *url)
{
  return mystrbegins(url, "magnet:") || mystrbegins(url, "torrent:");
}


/**
 *
 */
static backend_t be_bittorrent = {
  .be_canhandle = bt_canhandle,
  .be_open      = bt_open,
};

BE_REGISTER(bittorrent);
