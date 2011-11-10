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

#include <htsmsg/htsmsg.h>
#include <htsmsg/htsmsg_xml.h>

#include "showtime.h"
#include "lastfm.h"
#include "fileaccess/fileaccess.h"
#include "blobcache.h"

static prop_courier_t *lastfm_courier;
static hts_mutex_t lastfm_mutex;
#define LASTFM_APIKEY "e8fb67200bce49da092a9de1eb1c649c"

LIST_HEAD(album_art_cache_list, album_art_cache);
TAILQ_HEAD(album_art_cache_queue, album_art_cache);

#define AAC_HASHWIDTH 67

typedef struct album_art_cache {
  TAILQ_ENTRY(album_art_cache) aac_link;
  LIST_ENTRY(album_art_cache) aac_hash_link;
  rstr_t *aac_artist;
  rstr_t *aac_album;
  rstr_t *aac_url;
} album_art_cache_t;

static struct album_art_cache_queue aacqueue;
static struct album_art_cache_list aachash[AAC_HASHWIDTH];
static int naaclist;


typedef struct lastfm_prop {
  prop_t *lp_prop;
  prop_sub_t *lp_sub;

  rstr_t *lp_album;
  rstr_t *lp_artist;
} lastfm_prop_t;


TAILQ_HEAD(artist_image_queue, artist_image);

/**
 *
 */
typedef struct artist_image {
  TAILQ_ENTRY(artist_image) link;
  char *url;
} artist_image_t;



/**
 *
 */
static void
aac_insert(rstr_t *artist, rstr_t *album, rstr_t *url)
{
  album_art_cache_t *aac = malloc(sizeof(album_art_cache_t));
  unsigned int hash = mystrhash(rstr_get(album)) % AAC_HASHWIDTH;

  aac->aac_artist = rstr_dup(artist);
  aac->aac_album = rstr_dup(album);
  aac->aac_url = rstr_dup(url);
  LIST_INSERT_HEAD(&aachash[hash], aac, aac_hash_link);
  TAILQ_INSERT_TAIL(&aacqueue, aac, aac_link);
  if(naaclist < 512) {
    naaclist++;
    return;
  }
  aac = TAILQ_FIRST(&aacqueue);
  LIST_REMOVE(aac, aac_hash_link);
  TAILQ_REMOVE(&aacqueue, aac, aac_link);
  rstr_release(aac->aac_url);
  rstr_release(aac->aac_artist);
  rstr_release(aac->aac_album);
  free(aac);
}


/**
 *
 */
static void
lastfm_parse_artist_images(htsmsg_t *xml, prop_t *parent, int *totalpages,
			   struct artist_image_queue *q)
{
  htsmsg_t *images, *image, *sizes, *size, *attr;
  htsmsg_field_t *f, *s;
  const char *url, *str;
  prop_t *p;
  artist_image_t *ai;

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

      p = prop_create_root(NULL);

      prop_set_string(prop_create(p, "url"), url);

      if(prop_set_parent(p, parent))
	prop_destroy(p);

      ai = malloc(sizeof(artist_image_t));
      ai->url = strdup(url);
      TAILQ_INSERT_TAIL(q, ai, link);
    }
  }
}


/**
 *
 */
static void
write_to_blobcache(const char *artist, struct artist_image_queue *q)
{
  artist_image_t *ai;
  int blobsize = 0;
  char *blob, *ptr;

  TAILQ_FOREACH(ai, q, link)
    blobsize += strlen(ai->url) + 1;

  ptr = blob = malloc(blobsize);


  while((ai = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, ai, link);
    int l = strlen(ai->url);
    memcpy(ptr, ai->url, l);
    ptr += l;

    *ptr++ = TAILQ_NEXT(ai, link) ? '\n' : 0;
    free(ai->url);
    free(ai);
  }
  
  blobcache_put(artist, "lastfm.artist.images", blob, blobsize, 86400,
		NULL, 0);
  free(blob);
}


/**
 *
 */
static int
load_from_blobcache(const char *artist, prop_t *parent)
{
  char *data, *s0, *s;
  size_t size;
  prop_t *p;

  s0 = data = blobcache_get(artist, "lastfm.artist.images", &size, 1, 0,
			    NULL, NULL);
  if(data == NULL)
    return 0;

  while((s = strsep(&s0, "\n")) != NULL) {
      p = prop_create_root(NULL);

      prop_set_string(prop_create(p, "url"), s);

      if(prop_set_parent(p, parent))
	prop_destroy(p);
  }
  free(data);
  return 1;
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
  int n, page = 1;
  htsmsg_t *xml;
  char str[20];
  char *artist = mystrdupa(rstr_get(lp->lp_artist));
  int totalpages;
  struct artist_image_queue q;

  artist[strcspn(artist, ";:,-[]")] = 0;

  if(load_from_blobcache(artist, lp->lp_prop))
    return;

  TAILQ_INIT(&q);
  TRACE(TRACE_DEBUG, "lastfm", "Loading images for artist %s", artist);

  while(1) {

    snprintf(str, sizeof(str), "%d", page);
    n = http_request("http://ws.audioscrobbler.com/2.0/",
		     (const char *[]){"method", "artist.getimages",
			 "artist", artist,
			 "api_key", LASTFM_APIKEY,
			 "order", "popularity",
			 "page", str,
			 NULL, NULL},
		     &result, &resultsize, errbuf, sizeof(errbuf),
		     NULL, NULL, 0, NULL, NULL, NULL);

    if(n) {
      TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
      return;
    }


    /* XML parser consumes 'buf' */
    if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
      TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
      return;
    }

    lastfm_parse_artist_images(xml, lp->lp_prop, &totalpages, &q);

    htsmsg_destroy(xml);
    
    if(page == 5 || page == totalpages)
      break;

    page++;
  }

  write_to_blobcache(artist, &q);
}


/**
 *
 */
static void
lastfm_parse_coverart(htsmsg_t *xml, lastfm_prop_t *lp)
{
  htsmsg_t *tags, *image;
  htsmsg_field_t *f;
  int curscore = -1, s;
  const char *size, *url, *best = NULL;
  rstr_t *img;

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
  if(best == NULL)
    return;

  img = rstr_alloc(best);

  aac_insert(lp->lp_artist, lp->lp_album, img);
  prop_set_rstring(lp->lp_prop, img);
  rstr_release(img);
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
		   NULL, NULL, 0, NULL, NULL, NULL);

  if(n) {
    TRACE(TRACE_DEBUG, "lastfm", "HTTP query to lastfm failed: %s",  errbuf);
    return;
  }

  /* XML parser consumes 'buf' */
  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "lastfm", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse_coverart(xml, lp);
  htsmsg_destroy(xml);
}

/**
 *
 */
static void
lp_destroy(lastfm_prop_t *lp)
{
  if(lp->lp_sub != NULL)
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
static int
lastfm_albumart_from_cache(lastfm_prop_t *lp)
{
  album_art_cache_t *aac;
  unsigned int hash = mystrhash(rstr_get(lp->lp_album)) % AAC_HASHWIDTH;

  LIST_FOREACH(aac, &aachash[hash], aac_hash_link) {
    if(!strcmp(rstr_get(aac->aac_artist), rstr_get(lp->lp_artist)) &&
       !strcmp(rstr_get(aac->aac_album),  rstr_get(lp->lp_album)))
      break;
  }
  if(aac == NULL)
    return -1;

  TAILQ_REMOVE(&aacqueue, aac, aac_link);
  TAILQ_INSERT_TAIL(&aacqueue, aac, aac_link);
  prop_set_rstring(lp->lp_prop, aac->aac_url);
  return 0;
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
    if(lastfm_albumart_from_cache(lp))
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

  lp->lp_prop = prop_ref_inc(prop);

  hts_mutex_lock(&lastfm_mutex);

  lp->lp_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK, lastfm_prop_artist_cb, lp,
		   PROP_TAG_COURIER, lastfm_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
  if(lp->lp_sub == NULL)
    lp_destroy(lp);
  hts_mutex_unlock(&lastfm_mutex);
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

  lp->lp_prop = prop_ref_inc(prop);

  hts_mutex_lock(&lastfm_mutex);

  lp->lp_sub = 
    prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SUBSCRIPTION_MONITOR,
		   PROP_TAG_CALLBACK, lastfm_prop_album_cb, lp,
		   PROP_TAG_COURIER, lastfm_courier,
		   PROP_TAG_ROOT, prop,
		   NULL);
  if(lp->lp_sub == NULL)
    lp_destroy(lp);
  hts_mutex_unlock(&lastfm_mutex);
}


/**
 *
 */
void
lastfm_init(void)
{
  hts_mutex_init(&lastfm_mutex);
  TAILQ_INIT(&aacqueue);
  lastfm_courier = prop_courier_create_thread(&lastfm_mutex, "lastfm");
}
