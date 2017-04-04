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
#include <string.h>
#include <unistd.h>

#include "main.h"
#include "navigator.h"
#include "backend/backend.h"
#include "misc/str.h"
#include "networking/http.h"
#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"

#include "bittorrent.h"
#include "misc/sha.h"
#include "media/media.h"
#include "video/video_playback.h"
#include "usage.h"

// http://www.bittorrent.org/beps/bep_0009.htm

/**
 * 
 */
static torrent_t *
torrent_create_from_uri(const char *url, char *errbuf, size_t errlen)
{
  const char *magnet = mystrbegins(url, "magnet:");

  if(magnet != NULL)
    return magnet_open(magnet, errbuf, errlen);

  hts_mutex_unlock(&bittorrent_mutex);
  buf_t *b = fa_load(url, FA_LOAD_ERRBUF(errbuf, errlen), NULL);
  hts_mutex_lock(&bittorrent_mutex);

  if(b == NULL)
    return NULL;

  torrent_t *to = torrent_create_from_infofile(b, errbuf, errlen);
  buf_release(b);
  return to;
}


/**
 *
 */
torrent_t *
torrent_open_url(const char **urlp, char *errbuf, size_t errlen)
{
  uint8_t infohash[20];
  torrent_t *to;
  const char *url = *urlp;

  hts_mutex_lock(&bittorrent_mutex);

  if(hex2binl(infohash, 20, url, 40) == 20 &&
     (url[40] == '/' || url[40] == 0)) {
    to = torrent_create_from_hash(infohash, "URL");

    if(url[40] == 0) {
      *urlp = NULL;
    } else {
      url += 40;
      if(*url == 0 || !strcmp(url, "/"))
        *urlp = NULL;
      else
        *urlp = url + 1;
    }
  } else {

    *urlp = NULL;

    char *u = mystrdupa(url);

    int n = strlen(u);
    while(n > 0 && u[n - 1] == '/')
      u[--n] = 0;

    to = torrent_create_from_uri(u, errbuf, errlen);
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
  torrent_retain(to);
  prop_subscribe(PROP_SUB_TRACK_DESTROY,
                 PROP_TAG_ROOT, p,
                 PROP_TAG_CALLBACK_DESTROYED, do_torrent_release, to,
                 PROP_TAG_MUTEX, &bittorrent_mutex,
                 NULL);
}


/**
 *
 */
static int
torrent_browse_open(prop_t *page, const char *url, int sync)
{
  char errbuf[512];
  torrent_t *to;
  prop_t *model = prop_create_r(page, "model");
  prop_set(model, "loading", PROP_SET_INT, 1);

  usage_page_open(sync, "Torrent browse");

  to = torrent_open_url(&url, errbuf, sizeof(errbuf));
  if(to == NULL) {
    nav_open_errorf(page, _("Unable to open torrent: %s"), errbuf);
  } else {

    char redir[128];
    char hashstr[41];

    torrent_release_on_prop_destroy(page, to);
    prop_set(model, "loading", PROP_SET_INT, 0);

    bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);
    hashstr[40] = 0;

    snprintf(redir, sizeof(redir), "torrentfile://%s/", hashstr);
    event_t *e = event_create_str(EVENT_REDIRECT, redir);

    prop_t *sink = prop_create_r(page, "eventSink");
    prop_send_ext_event(sink, e);
    prop_ref_dec(sink);
    event_release(e);
  }
  hts_mutex_unlock(&bittorrent_mutex);
  prop_ref_dec(model);
  return 0;
}


/**
 *
 */
static torrent_file_t *
find_movie_torrent(torrent_t *to)
{
  // Find biggest file and use that as movie source

  torrent_file_t *tf, *best = NULL;
  TAILQ_FOREACH(tf, &to->to_files, tf_torrent_link)
    if(best == NULL || best->tf_size < tf->tf_size)
      best = tf;

  return best;
}

/**
 *
 */
static int
torrent_movie_open(prop_t *page, const char *url0, int sync)
{
  char errbuf[512];
  usage_page_open(sync, "Torrent movie");

  prop_t *m = prop_create_r(page, "model");

  prop_set(m, "loading", PROP_SET_INT, 1);

  hts_mutex_lock(&bittorrent_mutex);

  torrent_t *to = torrent_create_from_uri(url0, errbuf, sizeof(errbuf));

  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    prop_ref_dec(m);
    return nav_open_errorf(page, _("Unable to open torrent: %s"), errbuf);
  }

  torrent_file_t *best = find_movie_torrent(to);

  if(best == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    prop_ref_dec(m);
    return nav_open_errorf(page, _("No files in torrent"));
  }

  char url[1024];
  char hashstr[41];
  bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);
  hashstr[40] = 0;

  // Create videoparams message

  htsmsg_t *vp = htsmsg_create_map();

  htsmsg_add_str(vp, "title", to->to_title);


  snprintf(url, sizeof(url), "torrentfile://%s/%s",
           hashstr, best->tf_fullpath);
  htsmsg_add_str(vp, "canonicalUrl", url);

  htsmsg_t *src = htsmsg_create_map();
  htsmsg_add_str(src, "url", url);


  htsmsg_t *sources = htsmsg_create_list();
  htsmsg_add_msg(sources, NULL, src);
  htsmsg_add_msg(vp, "sources", sources);


  prop_set(page, "directClose", PROP_SET_INT, 1);

  rstr_t *rstr = htsmsg_json_serialize_to_rstr(vp, "videoparams:");
  prop_set(page, "source", PROP_ADOPT_RSTRING, rstr);

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
  } else if((u = mystrbegins(url, "magnet:")) != NULL) {
    return torrent_browse_open(page, url, sync);
  }

  return 0;
}


/**
 *
 */
static int
bt_canhandle(const char *url)
{
  return mystrbegins(url, "torrent:") || mystrbegins(url, "magnet:");
}


static event_t *
bt_playvideo(const char *url, media_pipe_t *mp,
             char *errbuf, size_t errlen,
             video_queue_t *vq, struct vsource_list *vsl,
             const video_args_t *va0)
{
  const char *u;

  if((u = mystrbegins(url, "torrent:video:")) != NULL) {

  } else {
    snprintf(errbuf, errlen, "Invalid torrent link");
    return NULL;
  }

  hts_mutex_lock(&bittorrent_mutex);
  torrent_t *to = torrent_create_from_uri(u, errbuf, errlen);
  if(to == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    return NULL;
  }

  torrent_file_t *best = find_movie_torrent(to);

  if(best == NULL) {
    hts_mutex_unlock(&bittorrent_mutex);
    snprintf(errbuf, errlen, "No files in torrent");
    return NULL;
  }

  char newurl[1024];
  char hashstr[41];
  bin2hex(hashstr, sizeof(hashstr), to->to_info_hash, 20);
  hashstr[40] = 0;

  snprintf(newurl, sizeof(newurl), "torrentfile://%s/%s",
           hashstr, best->tf_fullpath);

  torrent_retain(to);
  hts_mutex_unlock(&bittorrent_mutex);

  event_t *e = backend_play_video(newurl, mp, errbuf, errlen, vq, vsl, va0);

  hts_mutex_lock(&bittorrent_mutex);
  torrent_release(to);
  hts_mutex_unlock(&bittorrent_mutex);
  return e;
}


/**
 *
 */
static backend_t be_bittorrent = {
  .be_canhandle = bt_canhandle,
  .be_open      = bt_open,
  .be_play_video = bt_playvideo,
};

BE_REGISTER(bittorrent);
