/*
 *  Scrapping core
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

#include "scrappers.h"
#include "showtime.h"
#include "fileaccess/fileaccess.h"

static prop_courier_t *scrapper_courier;
static hts_mutex_t scrapper_mutex;


typedef struct scrap_prop_artist {
  prop_t *spa_prop;
  
  char *spa_artistname;
} scrap_prop_artist_t;



/**
 * The LastFM stuff should reside in a file of its own...
 * later.. someday
 */
#define LASTFM_APIKEY "e8fb67200bce49da092a9de1eb1c649c"


static void
lastfm_parse(htsmsg_t *xml, prop_t *parent)
{
  htsmsg_t *images, *image, *sizes, *size, *attr;
  htsmsg_field_t *f, *s;
  const char *url;
  prop_t *p;

  if((images = htsmsg_get_map_multi(xml, "tags", "lfm", 
				    "tags", "images", 
				    "tags", NULL)) == NULL) {
    printf("SCRAPPER: no images\n");
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


static void
scrap_test(const char *artistname, prop_t *p)
{
  char *result;
  size_t resultsize;
  char errbuf[100];
  int n;
  htsmsg_t *xml;

  n = http_request("http://ws.audioscrobbler.com/2.0/",
		   (const char *[]){"method", "artist.getimages",
				    "artist", artistname,
				    "api_key", LASTFM_APIKEY,
				    NULL, NULL},
		   &result, &resultsize, errbuf, sizeof(errbuf),
		   NULL, NULL);

  if(n) {
    TRACE(TRACE_DEBUG, "scrapper", "HTTP query to lastfm failed: %s",  errbuf);
    return;
  }

  /* XML parser consumes 'buf' */
  if((xml = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_DEBUG, "scrapper", "lastfm xml parse failed: %s",  errbuf);
    return;
  }

  lastfm_parse(xml, p);

  htsmsg_destroy(xml);
}


/**
 *
 */
static void
scrap_prop_artist_cb(void *opaque, prop_event_t event, ...)
{
  scrap_prop_artist_t *spa = opaque;

  switch(event) {
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    TRACE(TRACE_DEBUG, "scrapper", "Scrapping images for artist %s",
	  spa->spa_artistname);
    scrap_test(spa->spa_artistname, spa->spa_prop);
    break;

  case PROP_DESTROYED:
    prop_ref_dec(spa->spa_prop);
    free(spa->spa_artistname);
    free(spa);
    break;

  default:
    break;
  }
}




/**
 *
 */
void
scrapper_artist_init(prop_t *prop, const char *artist)
{
  scrap_prop_artist_t *spa;

  spa = calloc(1, sizeof(scrap_prop_artist_t));
  spa->spa_artistname = strdup(artist);
  spa->spa_artistname[strcspn(spa->spa_artistname, ";:,-[]")] = 0;

  spa->spa_prop = prop;
  prop_ref_inc(prop);

  prop_subscribe(PROP_SUB_TRACK_DESTROY | PROP_SUB_SUBSCRIPTION_MONITOR,
		 PROP_TAG_CALLBACK, scrap_prop_artist_cb, spa,
		 PROP_TAG_COURIER, scrapper_courier,
		 PROP_TAG_ROOT, prop,
		 NULL);
}


/**
 *
 */
void
scrappers_init(void)
{
  hts_mutex_init(&scrapper_mutex);
  scrapper_courier = prop_courier_create_thread(&scrapper_mutex, "scrapper");
}
