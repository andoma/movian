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

#include "bittorrent.h"

// http://www.bittorrent.org/beps/bep_0009.html

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
  torrent_create(infohash, dn, trackers);
  return 0;
}

/**
 *
 */
static int
magnet_open(prop_t *page, const char *url0, int sync)
{
  url0 += strlen("magnet:");

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
static int
magnet_canhandle(const char *url)
{
  return !strncmp(url, "magnet:", strlen("magnet:"));
}


/**
 *
 */
static backend_t be_magnet = {
  .be_canhandle = magnet_canhandle,
  .be_open      = magnet_open,
};

BE_REGISTER(magnet);




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


#endif
