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

#include <stdio.h>
#include <string.h>

#include <htsmsg/htsmsg.h>
#include <htsmsg/htsmsg_xml.h>

#include "showtime.h"
#include "lastfm.h"
#include "fileaccess/fileaccess.h"

static prop_courier_t *lastfm_courier;
static hts_mutex_t lastfm_mutex;
#define LASTFM_APIKEY "e8fb67200bce49da092a9de1eb1c649c"


typedef struct lastfm_prop {
  prop_t *lp_prop;
  prop_sub_t *lp_sub;

  rstr_t *lp_album;
  rstr_t *lp_artist;
} lastfm_prop_t;



/**
 *
 */
static void
lastfm_parse_artist_images(htsmsg_t *xml, prop_t *parent)
{
  htsmsg_t *images, *image, *sizes, *size, *attr;
  htsmsg_field_t *f, *s;
  const char *url;
  prop_t *p;

  if((images = htsmsg_get_map_multi(xml, "tags", "lfm", 
				    "tags", "images", 
				    "tags", NULL)) == NULL) {
    return;
  }

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

      p = prop_create(NULL, NULL);

      prop_set_string(prop_create(p, "url"), url);

      if(prop_set_parent(p, parent))
	prop_destroy(p);
    }
  }
}


/**
 *
 */
static void
lastfm_artistpics_query(lastfm_prop_t *lp)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n;
  htsmsg_t *xml;

  char *artist = mystrdupa(rstr_get(lp->lp_artist));

  artist[strcspn(artist, ";:,-[]")] = 0;

  TRACE(TRACE_DEBUG, "lastfm", "Loading images for artist %s", artist);

  n = http_request("http://ws.audioscrobbler.com/2.0/",
		   (const char *[]){"method", "artist.getimages",
				    "artist", artist,
				    "api_key", LASTFM_APIKEY,
				    NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL, HTTP_REQUEST_ESCAPE_PATH);

  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
    return;
  }

  /* XML parser consumes 'buf' */
  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse_artist_images(xml, lp->lp_prop);

  htsmsg_destroy(xml);
}


/**
 *
 */
static void
lastfm_parse_coverart(htsmsg_t *xml, prop_t *p)
{
  htsmsg_t *tags, *image;
  htsmsg_field_t *f;
  int curscore = -1, s;
  const char *size, *url, *best = NULL;

  if((tags = htsmsg_get_map_multi(xml, "tags", "lfm", 
				    "tags", "album", 
				    "tags", NULL)) == NULL) {
    return;
  }

  HTSMSG_FOREACH(f, tags) {
    if(strcmp(f->hmf_name, "image") ||
       ((image = htsmsg_get_map_by_field(f)) == NULL))
      continue;

    if((url = htsmsg_get_str(image, "cdata")) == NULL)
      continue;

    s = 0;
    if((size = htsmsg_get_str_multi(image, "attrib", "size", NULL)) != NULL) {

      if(!strcmp(size, "small"))
	s = 1;
      else if(!strcmp(size, "medium"))
	s = 2;
      else if(!strcmp(size, "large"))
	s = 3;
      else if(!strcmp(size, "extralarge"))
	s = 4;
    }

    if(s <= curscore)
      continue;

    curscore = s;
    best = url;
  }
  prop_set_string(p, best);
}


/**
 *
 */
static void
lastfm_albumart_query(lastfm_prop_t *lp)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n;
  htsmsg_t *xml;
  char *artist = mystrdupa(rstr_get(lp->lp_artist));
  char *album  = mystrdupa(rstr_get(lp->lp_album));

  artist[strcspn(artist, ";:,-[]")] = 0;
  album[strcspn(album, "[]()")] = 0;

  TRACE(TRACE_DEBUG, "lastfm", "Loading coverart for album %s", album);

  n = http_request("http://ws.audioscrobbler.com/2.0/",
		   (const char *[]){"method", "album.getinfo",
		       "artist", artist,
		       "album", album,
		       "api_key", LASTFM_APIKEY,
		       NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL, HTTP_REQUEST_ESCAPE_PATH);

  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
    return;
  }

  /* XML parser consumes 'buf' */
  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse_coverart(xml, lp->lp_prop);
  htsmsg_destroy(xml);
}

/**
 *
 */
static void
lp_destroy(lastfm_prop_t *lp)
{
  prop_unsubscribe(lp->lp_sub);
  prop_ref_dec(lp->lp_prop);
  rstr_release(lp->lp_artist);
  rstr_release(lp->lp_album);
  free(lp);
}


/**
 *
 */
static void
lastfm_prop_artist_cb(void *opaque, prop_event_t event, ...)
{
  lastfm_prop_t *lp = opaque;

  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    lastfm_artistpics_query(lp);
    // FALLTHRU
  case PROP_DESTROYED:
    lp_destroy(lp);
    break;

  default:
    break;
  }
}


/**
 *
 */
static void
lastfm_prop_album_cb(void *opaque, prop_event_t event, ...)
{
  lastfm_prop_t *lp = opaque;

  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    lastfm_albumart_query(lp);
    // FALLTHRU
  case PROP_DESTROYED:
    lp_destroy(lp);
    break;

  default:
    break;
  }
}




/**
 *
 */
void
lastfm_artistpics_init(prop_t *prop, rstr_t *artist)
{
  lastfm_prop_t *lp;

  lp = calloc(1, sizeof(lastfm_prop_t));
  lp->lp_artist = rstr_dup(artist);

  lp->lp_prop = prop;
  prop_ref_inc(prop);

  lp->lp_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK, lastfm_prop_artist_cb, lp,
		   PROP_TAG_COURIER, lastfm_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
}


/**
 *
 */
void
lastfm_albumart_init(prop_t *prop, rstr_t *artist, rstr_t *album)
{
  lastfm_prop_t *lp;

  lp = calloc(1, sizeof(lastfm_prop_t));
  lp->lp_artist = rstr_dup(artist);
  lp->lp_album  = rstr_dup(album);

  lp->lp_prop = prop;
  prop_ref_inc(prop);

  lp->lp_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK, lastfm_prop_album_cb, lp,
		   PROP_TAG_COURIER, lastfm_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
}


/**
 *
 */
void
lastfm_init(void)
{
  hts_mutex_init(&lastfm_mutex);
  lastfm_courier = prop_courier_create_thread(&lastfm_mutex, "lastfm");
}
