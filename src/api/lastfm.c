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
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_xml.h"

#include "main.h"
#include "lastfm.h"
#include "fileaccess/http_client.h"
#include "blobcache.h"
#include "db/db_support.h"
#include "metadata/metadata_sources.h"

#define LASTFM_APIKEY "e8fb67200bce49da092a9de1eb1c649c"

static metadata_source_t *lastfm;

/**
 *
 */
static void
lastfm_parse_albuminfo(void *db, htsmsg_t *xml, const char *artist,
		       const char *album)
{
  htsmsg_t *tags, *tracks;
  htsmsg_field_t *f, *g;

  if((tags = htsmsg_get_map_multi(xml, "lfm", "album", NULL)) == NULL)
    return;

  const char *artist_mbid = NULL;
  const char *album_mbid = htsmsg_get_str(tags, "mbid");

  if((tracks = htsmsg_get_map(tags, "tracks")) != NULL) {

    const char *artstr = htsmsg_get_str_multi(tags, "artist", NULL);

    if(artstr != NULL) {

      HTSMSG_FOREACH(f, tracks) {
        htsmsg_t *track;
        if(artist_mbid != NULL)
          break;
        if(strcmp(f->hmf_name, "track") ||
           ((track = htsmsg_get_map_by_field(f)) == NULL))
          continue;

        HTSMSG_FOREACH(g, track) {
          htsmsg_t *artist;
          if(artist_mbid != NULL)
            break;
          if(strcmp(g->hmf_name, "artist") ||
             ((artist = htsmsg_get_map_by_field(g)) == NULL))
            continue;
          const char *ta;
          ta = htsmsg_get_str(artist, "name");
          if(ta == NULL)
            continue;

          if(!strcmp(artstr, ta))
            artist_mbid = htsmsg_get_str_multi(artist, "mbid", NULL);
        }
      }
    }
  }

  int64_t artist_id =
    metadb_artist_get_by_title(db, artist, lastfm->ms_id, artist_mbid);

  if(artist_id < 0)
    return;

  int64_t album_id =
    metadb_album_get_by_title(db, album, artist_id,
			      lastfm->ms_id, album_mbid);

  if(album_id < 0)
    return;

  HTSMSG_FOREACH(f, tags) {
    if(strcmp(f->hmf_name, "image"))
      continue;

    if(f->hmf_type != HMF_STR)
      continue;

    const char *url = f->hmf_str;
    if(url == NULL)
      continue;

    htsmsg_t *image = htsmsg_get_map_by_field(f);
    if(image == NULL)
      continue;
    const char *size = htsmsg_get_str(image, "size");
    if(size == NULL)
      continue;

    int width = 0, height = 0;

    if(!strcmp(size, "medium")) {
      width = 64; height = 64;
    } else if(!strcmp(size, "large")) {
      width = 174; height = 174;
    } else if(!strcmp(size, "extralarge")) {
      width = 300; height = 300;
    } else
      continue;

    printf("Added albumart %s (%d x %d)\n", url, width, height);

    metadb_insert_albumart(db, album_id, url, width, height);
  }
}


/**
 *
 */
void
lastfm_load_albuminfo(void *db, const char *album, const char *artist)
{
  buf_t *result;
  char errbuf[100];
  int n;
  htsmsg_t *xml;

  if(lastfm == NULL)
    return;

  TRACE(TRACE_DEBUG, "lastfm", "Loading coverart for album %s", album);

  n = http_req("http://ws.audioscrobbler.com/2.0/",
               HTTP_ARG("method", "album.getinfo"),
               HTTP_ARG("artist", artist),
               HTTP_ARG("album", album),
               HTTP_ARG("api_key", LASTFM_APIKEY),
               HTTP_RESULT_PTR(&result),
               HTTP_ERRBUF(errbuf, sizeof(errbuf)),
               HTTP_FLAGS(FA_COMPRESSION),
               NULL);

  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
   return;
  }

  xml = htsmsg_xml_deserialize_buf(result, errbuf, sizeof(errbuf));

  if(xml == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse_albuminfo(db, xml, artist, album);
  htsmsg_release(xml);
  return;
}


static void
lastfm_init(void)
{
  lastfm = metadata_add_source("lastfm", "last.fm",
			       100000, METADATA_TYPE_MUSIC,
			       NULL, 0, 0);
}

INITME(INIT_GROUP_API, lastfm_init, NULL, 0);
