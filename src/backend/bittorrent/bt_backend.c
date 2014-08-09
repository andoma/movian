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
#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"

#include "bittorrent.h"
#include "misc/sha.h"

// http://www.bittorrent.org/beps/bep_0009.htm

/**
 *
 */
torrent_t *
torrent_open_url(const char **urlp, char *errbuf, size_t errlen)
{
  uint8_t infohash[20];
  torrent_t *to;
  const char *url = *urlp;

  if(hex2binl(infohash, 20, url, 40) == 20 && url[40] == '/') {
    hts_mutex_lock(&bittorrent_mutex);
    to = torrent_find_by_hash(infohash);
    if(to == NULL) {
      snprintf(errbuf, errlen, "Torrent not found");
    } else {
      to->to_refcount++;
      *urlp = url + 41;
    }
  } else {

    *urlp = NULL;

    char *u = mystrdupa(url);

    int n = strlen(u);
    while(n > 0 && u[n - 1] == '/')
      u[--n] = 0;

    buf_t *b = fa_load(u,
                       FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                       NULL);

    hts_mutex_lock(&bittorrent_mutex);

    if(b == NULL)
      return NULL;


    to = torrent_create(b, errbuf, sizeof(errbuf));
    buf_release(b);
  }
  return to;
}


/**
 *
 */
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


    snprintf(url, sizeof(url), "torrentfile://%s/%s",
             hashstr, tf->tf_fullpath);

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


/**
 *
 */
static int
torrent_browse_open(prop_t *page, const char *url, int sync)
{
  char errbuf[512];
  torrent_t *to;

  to = torrent_open_url(&url, errbuf, sizeof(errbuf));
  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return nav_open_errorf(page, _("Unable to open torrent: %s"), errbuf);
  }
  torrent_browse(page, to, url);

  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);
  return 0;
}



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

  // Find biggest file and use that as movie source

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

  // Create videoparams message

  htsmsg_t *vp = htsmsg_create_map();
  snprintf(url, sizeof(url), "torrent:video:%s", hashstr);
  htsmsg_add_str(vp, "canonicalUrl", url);

  htsmsg_add_str(vp, "title", to->to_title);


  snprintf(url, sizeof(url), "torrentfile://%s/%s",
           hashstr, best->tf_fullpath);

  htsmsg_t *src = htsmsg_create_map();
  htsmsg_add_str(src, "url", url);


  htsmsg_t *sources = htsmsg_create_list();
  htsmsg_add_msg(sources, NULL, src);
  htsmsg_add_msg(vp, "sources", sources);


  prop_set(page, "directClose", PROP_SET_INT, 1);

  rstr_t *rstr = htsmsg_json_serialize_to_rstr(vp, "videoparams:");
  prop_set(page, "source", PROP_ADOPT_RSTRING, rstr);

  prop_t *m = prop_create_r(page, "model");
  prop_set(m, "type", PROP_SET_STRING, "video");
  prop_ref_dec(m);

  torrent_release_on_prop_destroy(page, to);
  hts_mutex_unlock(&bittorrent_mutex);

  return 0;
}


/**
 *
 */
static int
bt_open(prop_t *page, const char *url, int sync)
{
  const char *u;

  if((u = mystrbegins(url, "torrent:video:")) != NULL) {
    return torrent_movie_open(page, u, sync);
  } else if((u = mystrbegins(url, "torrent:browse:")) != NULL) {
    return torrent_browse_open(page, u, sync);
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
