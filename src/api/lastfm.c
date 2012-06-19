/*
 *  LastFM API
 *  Copyright (C) 2009 Andreas Öman
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_xml.h"

#include "showtime.h"
#include "lastfm.h"
#include "fileaccess/fileaccess.h"
#include "blobcache.h"

#define LASTFM_APIKEY "e8fb67200bce49da092a9de1eb1c649c"


/**
 *
 */
static void
lastfm_parse_artist_images(void *db, int64_t artist_id, htsmsg_t *xml,
			   int *totalpages,
			   void (*cb)(void *opaque, const char *url,
				      int width, int height),
			   void *opaque)
{
  htsmsg_t *images, *image, *sizes, *size, *attr;
  htsmsg_field_t *f, *s;
  const char *url, *str;

  *totalpages = 1;

  if((images = htsmsg_get_map_multi(xml, "tags", "lfm", 
				    "tags", "images", NULL)) == NULL)
    return;

  if((attr = htsmsg_get_map(images, "attrib")) != NULL &&
     (str = htsmsg_get_str(attr, "totalpages")) != NULL) {
    *totalpages = atoi(str);
  }

  if((images = htsmsg_get_map(images, "tags")) == NULL)
    return;

  HTSMSG_FOREACH(f, images) {
    if(strcmp(f->hmf_name, "image") ||
       ((image = htsmsg_get_map_by_field(f)) == NULL) ||
       ((image = htsmsg_get_map(image, "tags")) == NULL))
      continue;
    
    if((sizes = htsmsg_get_map_multi(image, "sizes", "tags", NULL)) == NULL)
      continue;

    HTSMSG_FOREACH(s, sizes) {
      if(strcmp(s->hmf_name, "size") ||
	 ((size = htsmsg_get_map_by_field(s)) == NULL))
	continue;


      if((attr = htsmsg_get_map(size, "attrib")) == NULL)
	continue;
      
      if(strcmp(htsmsg_get_str(attr, "name") ?: "", "original"))
	continue;

      if((url = htsmsg_get_str(size, "cdata")) == NULL)
	continue;

      int width = htsmsg_get_s32_or_default(attr, "width", 0);
      int height = htsmsg_get_s32_or_default(attr, "height", 0);

      metadb_insert_artistpic(db, artist_id, url, width, height);
      cb(opaque, url, width, height);
#if 0
      p = prop_create_root(NULL);

      prop_set_string(prop_create(p, "url"), url);

      if(prop_set_parent(p, parent))
	prop_destroy(p);
#endif
    }
  }
}



/**
 *
 */
void
lastfm_load_artistinfo(void *db, const char *artist,
		       void (*cb)(void *opaque, const char *url,
				  int width, int height),
		       void *opaque)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n, page = 1;
  htsmsg_t *xml, *info;
  char str[20];
  int totalpages;

  TRACE(TRACE_DEBUG, "lastfm", "Loading images for artist %s", artist);

  n = http_request("http://ws.audioscrobbler.com/2.0/",
		   (const char *[]){"method", "artist.getinfo",
		       "artist", artist,
		       "api_key", LASTFM_APIKEY,
		       NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL, FA_COMPRESSION, NULL, NULL, NULL,
		   NULL, NULL);
  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
    return;
  }
  
  /* XML parser consumes 'buf' */
  if((info = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  int src = metadb_get_datasource(db, "lastfm");
  if(src < 0) {
    TRACE(TRACE_DEBUG, "lastfm", "Unable to get datasource: %d", src);
    htsmsg_destroy(info);
    return;
  }

  const char *mbid = htsmsg_get_str_multi(info,
					  "tags", "lfm",
					  "tags", "artist",
					  "tags", "mbid", 
					  "cdata", NULL);
  
  int64_t artist_id = metadb_artist_get_by_title(db, artist, src, mbid);

  if(artist_id < 0) {
    htsmsg_destroy(info);
    return;
  }

  while(1) {

    snprintf(str, sizeof(str), "%d", page);
    n = http_request("http://ws.audioscrobbler.com/2.0/",
		     (const char *[]){"method", "artist.getimages",
			 "mbid", mbid,
			 "api_key", LASTFM_APIKEY,
			 "order", "popularity",
			 "page", str,
			 NULL, NULL},
		     &result, &resultsize, errbuf, sizeof(errbuf),
		     NULL, NULL, FA_COMPRESSION, NULL, NULL, NULL,
		     NULL, NULL);

    if(n) {
      TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
      break;
    }

    /* XML parser consumes 'buf' */
    if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
      TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
      break;
    }

    lastfm_parse_artist_images(db, artist_id, xml, &totalpages, cb, opaque);

    htsmsg_destroy(xml);
    
    if(page == 5 || page == totalpages)
      break;

    page++;
  }
  htsmsg_destroy(info);
}


/**
 *
 */
static void
lastfm_parse_albuminfo(void *db, htsmsg_t *xml, const char *artist,
		       const char *album)
{
  htsmsg_t *tags, *tracks;
  htsmsg_field_t *f, *g;
  const char *size, *url;

  if((tags = htsmsg_get_map_multi(xml, "tags", "lfm", 
				  "tags", "album", 
				  "tags", NULL)) == NULL) {
    return;
  }

  const char *artist_mbid = NULL;

  const char *album_mbid = htsmsg_get_str_multi(tags, "mbid", "cdata", NULL);

  if((tracks = htsmsg_get_map_multi(tags, "tracks", "tags", NULL)) != NULL) {

    const char *artstr = htsmsg_get_str_multi(tags, "artist", "cdata", NULL);

    HTSMSG_FOREACH(f, tracks) {
      htsmsg_t *track;
      if(artist_mbid != NULL)
	break;
      if(strcmp(f->hmf_name, "track") ||
	 ((track = htsmsg_get_map_by_field(f)) == NULL))
	continue;
      htsmsg_t *ttags = htsmsg_get_map(track, "tags");
      
      HTSMSG_FOREACH(g, ttags) {
	htsmsg_t *artist;
	if(artist_mbid != NULL)
	  break;
	if(strcmp(g->hmf_name, "artist") ||
	   ((artist = htsmsg_get_map_by_field(g)) == NULL))
	  continue;
	const char *ta;
	ta = htsmsg_get_str_multi(artist, "tags", "name", "cdata", NULL);
	if(ta == NULL)
	  continue;

	if(!strcmp(artstr, ta)) {
	  artist_mbid = htsmsg_get_str_multi(artist, "tags", "mbid",
					     "cdata", NULL);
	}
      }
    }
  }

  int src = metadb_get_datasource(db, "lastfm");
  if(src < 0) {
    TRACE(TRACE_DEBUG, "lastfm", "Unable to get datasource: %d", src);
    return;
  }

  int64_t artist_id = metadb_artist_get_by_title(db, artist,
						 src, artist_mbid);

  if(artist_id < 0)
    return;

  int64_t album_id = metadb_album_get_by_title(db, album, artist_id,
					       src, album_mbid);

  if(album_id < 0)
    return;

  HTSMSG_FOREACH(f, tags) {
    htsmsg_t *image;
    if(strcmp(f->hmf_name, "image") ||
       ((image = htsmsg_get_map_by_field(f)) == NULL))
      continue;

    if((url = htsmsg_get_str(image, "cdata")) == NULL)
      continue;

    if((size = htsmsg_get_str_multi(image, "attrib", "size", NULL)) == NULL)
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

    metadb_insert_albumart(db, album_id, url, width, height);
  }
}


/**
 *
 */
void
lastfm_load_albuminfo(void *db, const char *album, const char *artist)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n;
  htsmsg_t *xml;

  TRACE(TRACE_DEBUG, "lastfm", "Loading coverart for album %s", album);

  n = http_request("http://ws.audioscrobbler.com/2.0/",
		   (const char *[]){"method", "album.getinfo",
		       "artist", artist,
		       "album", album,
		       "api_key", LASTFM_APIKEY,
		       NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL, FA_COMPRESSION, NULL, NULL, NULL,
		   NULL, NULL);

  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
   return;
  }
  /* XML parser consumes 'buf' */
  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse_albuminfo(db, xml, artist, album);
  htsmsg_destroy(xml);
  return;
}


