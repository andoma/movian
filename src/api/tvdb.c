/*
 *  API interface to thetvdb.com
 *  Copyright (C) 2012 Andreas Öman
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

#include "showtime.h"
#include "metadata/metadata.h"
#include "htsmsg/htsmsg_xml.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/fileaccess.h"
#include "misc/dbl.h"
#include "settings.h"

static metadata_source_t *tvdb;
static char tvdb_language[3];

#define TVDB_APIKEY "0ADF8BA762FED295"


/**
 *
 */
static void
update_cfgid(void)
{
  if(!strcmp(tvdb_language, "en"))
    tvdb->ms_cfgid = 0; // For legacy reasons
  else
    tvdb->ms_cfgid = tvdb_language[0] | (tvdb_language[1] << 8);
}


static htsmsg_t *
loadxml(const char *fmt, ...)
{
  char url[256];
  char errbuf[256];

  va_list ap;
  snprintf(url, sizeof(url), "http://www.thetvdb.com/api/");

  va_start(ap, fmt);
  vsnprintf(url+strlen(url), sizeof(url)-strlen(url), fmt, ap);
  va_end(ap);

  buf_t *result = fa_load_query(url, errbuf, sizeof(errbuf), NULL,
                                NULL, FA_COMPRESSION);

  if(result == NULL) {
    TRACE(TRACE_INFO, "TVDB", "Unable to query for %s -- %s", url, errbuf);
    return NULL;
  }
  
  htsmsg_t *m = htsmsg_xml_deserialize_buf2(result, errbuf, sizeof(errbuf));
  if(m == NULL)
    TRACE(TRACE_INFO, "TVDB", "Unable to parse XML from %s -- %s", url, errbuf);
  return m;
}

LIST_HEAD(season_list, season);

/**
 * TVDB does not have a entity the represents a season
 * So we construct them on the fly
 */
typedef struct season {
  LIST_ENTRY(season) link;
  int num;
  int64_t videoitem_id;
  int artwork_deleted;
} season_t;


/**
 *
 */
static void
flush_seasons(struct season_list *list)
{
  season_t *s;
  while((s = LIST_FIRST(list)) != NULL) {
    LIST_REMOVE(s, link);
    free(s);
  }
}


/**
 *
 */
static int64_t
find_season(void *db, struct season_list *list, const char *seriesid, int num,
	    int qtype, int64_t series_vid, season_t **sp)
{
  season_t *s;
  int64_t itemid;

  *sp = NULL;
  LIST_FOREACH(s, list, link)
    if(s->num == num) {
      *sp = s;
      return s->videoitem_id;
    }

  char url[128];
  snprintf(url, sizeof(url), "tvdb:series:%s:%d", seriesid, num);

  itemid = metadb_get_videoitem(db, url);
  if(itemid <= 0) {

    metadata_t *md = metadata_create();
    md->md_type = METADATA_TYPE_SEASON;
    md->md_parent_id = series_vid;
    md->md_idx = num;
    
    char extid[64];
    snprintf(extid, sizeof(extid), "s%s-e%d", seriesid, num);
    itemid = metadb_insert_videoitem(db, url, tvdb->ms_id, extid, md,
				     METAITEM_STATUS_COMPLETE, 0,
				     qtype, tvdb->ms_cfgid);
    metadata_destroy(md);
  }

  if(itemid > 0) {
    s = calloc(1, sizeof(season_t));
    s->num = num;
    s->videoitem_id = itemid;
    LIST_INSERT_HEAD(list, s, link);
    *sp = s;
  }
  return itemid;
}


/**
 * videoitem_id - for series
 */
static int64_t
tvdb_load_actors(void *db, const char *seriesid, int64_t series_vid,
		 struct season_list *seasons, int qtype)
{
  htsmsg_field_t *f;
  htsmsg_t *doc = loadxml("%s/series/%s/actors.xml", TVDB_APIKEY, seriesid);
  htsmsg_t *b;
  int64_t rval = 0;
  char url[256];
  if(doc == NULL)
    return METADATA_TEMPORARY_ERROR;
  
  metadb_delete_videocast(db, series_vid);

  htsmsg_t *list = htsmsg_get_map_multi(doc, "tags", "Actors", "tags", NULL);
  if(list != NULL) {
    HTSMSG_FOREACH(f, list) {
      if((b = htsmsg_get_map_by_field_if_name(f, "Actor")) == NULL)
	continue;
      if((b = htsmsg_get_map(b, "tags")) == NULL)
	continue;
      const char *na = htsmsg_get_cdata(b, "Name");
      const char *ro = htsmsg_get_cdata(b, "Role");
      const char *im = htsmsg_get_cdata(b, "Image");
      const char *so = htsmsg_get_cdata(b, "SortOrder");
      const char *id = htsmsg_get_cdata(b, "id");

      if(na == NULL || ro == NULL || id == NULL)
	continue;

      snprintf(url, sizeof(url), "http://www.thetvdb.com/banners/%s", im?:"");

      metadb_insert_videocast(db, series_vid,
			      na, ro, "Cast", "Actor", so?atoi(so) : 4,
			      im ? url : NULL, 0, 0, id);

    }
  }
  htsmsg_destroy(doc);
  return rval;
}

/**
 * videoitem_id - for series
 */
static int64_t
tvdb_load_banners(void *db, const char *seriesid, int64_t series_vid,
		  struct season_list *seasons, int qtype)
{
  htsmsg_field_t *f;
  htsmsg_t *doc = loadxml("%s/series/%s/banners.xml", TVDB_APIKEY, seriesid);
  htsmsg_t *b;
  int64_t rval = 0;

  char url[256];

  if(doc == NULL)
    return METADATA_TEMPORARY_ERROR;
  
  metadb_delete_videoart(db, series_vid);

  htsmsg_t *list = htsmsg_get_map_multi(doc, "tags", "Banners", "tags", NULL);
  if(list != NULL) {
    HTSMSG_FOREACH(f, list) {
      if((b = htsmsg_get_map_by_field_if_name(f, "Banner")) == NULL)
	continue;
      if((b = htsmsg_get_map(b, "tags")) == NULL)
	continue;
      const char *bp = htsmsg_get_cdata(b, "BannerPath");
      const char *t1 = htsmsg_get_cdata(b, "BannerType");
      const char *t2 = htsmsg_get_cdata(b, "BannerType2");
      const char *se = htsmsg_get_cdata(b, "Season");
      const char *ra = htsmsg_get_cdata(b, "Rating");
      const char *la = htsmsg_get_cdata(b, "Language");

      if(bp == NULL || t1 == NULL || t2 == NULL)
	continue;

      if(la != NULL && strcasecmp(la, tvdb_language))
	continue;

      snprintf(url, sizeof(url), "http://www.thetvdb.com/banners/%s", bp);
      
      //      printf("%-60s %-15s %-15s %-10s %-10s\n", url, t1, t2, se, ra);

      if(se != NULL && *se) {
	int season = atoi(se);
	
	season_t *s = NULL;
	int64_t season_vid = find_season(db, seasons, seriesid, season, qtype,
					 series_vid, &s);
	if(season_vid < 0) {
	  rval = season_vid;
	  goto out;
	}
	assert(s != NULL);

	if(!s->artwork_deleted) {
	  metadb_delete_videoart(db, season_vid);
	  s->artwork_deleted = 1;
	}

	metadata_image_type_t type;
	if(!strcmp(t2, "season"))
	  type = METADATA_IMAGE_POSTER;
	else if(!strcmp(t2, "seasonwide"))
	  type = METADATA_IMAGE_BANNER_WIDE;
	else
	  continue;

	metadb_insert_videoart(db, season_vid, url, type, 0, 0,
			       ra ? my_str2double(ra, NULL) * 1000 : 0,
			       NULL, 0);

	continue;
      }

      metadata_image_type_t type;
      if(!strcmp(t1, "poster"))
	type = METADATA_IMAGE_POSTER;
      else if(!strcmp(t1, "fanart"))
	type = METADATA_IMAGE_BACKDROP;
      else if(!strcmp(t1, "series"))
	type = METADATA_IMAGE_BANNER_WIDE;
      else
	continue;

      int titled = 0;
      if(!strcmp(t2, "graphical") || !strcmp(t2, "text"))
	titled = 1;

      metadb_insert_videoart(db, series_vid, url, type, 0, 0,
			     ra ? my_str2double(ra, NULL) * 1000 : 0,
			     NULL, titled);
    }
  }
 out:
  htsmsg_destroy(doc);
  return rval;
}



/**
 *
 */
static int64_t 
tvdb_find_series(void *db, const char *id, int qtype,
		 struct season_list *seasons)
{
  char url[128];
  int64_t series_vid;

  snprintf(url, sizeof(url), "tvdb:series:%s", id);
  
  series_vid = metadb_get_videoitem(db, url);
  if(series_vid > 0)
    return series_vid;

  htsmsg_t *ser = loadxml("%s/series/%s/%s.xml", TVDB_APIKEY, id,
			  tvdb_language);
  if(ser == NULL)
    return METADATA_TEMPORARY_ERROR;

  htsmsg_t *tags = htsmsg_get_map_multi(ser,
					"tags", "Data",
					"tags", "Series",
					"tags", NULL);

  metadata_t *md = metadata_create();
  md->md_type = METADATA_TYPE_SERIES;
    
  md->md_title = rstr_alloc(htsmsg_get_cdata(tags, "SeriesName"));
  md->md_description = rstr_alloc(htsmsg_get_cdata(tags, "Overview"));

  const char *s;

  if((s = htsmsg_get_cdata(tags, "Rating")) != NULL)
    md->md_rating = 10 * my_str2double(s, NULL);

  if((s = htsmsg_get_cdata(tags, "RatingCount")) != NULL)
    md->md_rating_count = atoi(s);
  

  md->md_imdb_id = rstr_alloc(htsmsg_get_cdata(tags, "IMDB_ID"));

  char extid[64];
  snprintf(extid, sizeof(extid), "s%s", id);

  series_vid = metadb_insert_videoitem(db, url, tvdb->ms_id, extid, md,
				       METAITEM_STATUS_COMPLETE, 0,
				       qtype, tvdb->ms_cfgid);
  metadata_destroy(md);
  htsmsg_destroy(ser);

  tvdb_load_banners(db, id, series_vid, seasons, qtype);
  tvdb_load_actors(db, id, series_vid, seasons, qtype);

  return series_vid;
}


/**
 *
 */
static int64_t 
tvdb_query_by_episode(void *db, const char *item_url, 
		      const char *title, int season, int episode,
		      int qtype)
{
  buf_t *result;
  char errbuf[256];
  
  result = fa_load_query("http://www.thetvdb.com/api/GetSeries.php",
			 errbuf, sizeof(errbuf), NULL,
			 (const char *[]){
			   "seriesname", title,
			     NULL, NULL},
			 FA_COMPRESSION);

  if(result == NULL) {
    TRACE(TRACE_INFO, "TVDB", "Unable to search for %s -- %s", title, errbuf);
    return METADATA_TEMPORARY_ERROR;
  }
  
  htsmsg_t *gs = htsmsg_xml_deserialize_buf2(result, errbuf, sizeof(errbuf));
  if(gs == NULL) {
    TRACE(TRACE_INFO, "TVDB", "Unable to parse XML -- %s", errbuf);
    return METADATA_TEMPORARY_ERROR;
  }

  const char *series_id = 
    htsmsg_get_str_multi(gs,
			 "tags", "Data",
			 "tags", "Series",
			 "tags", "seriesid",
			 "cdata", NULL);


  if(series_id == NULL) {
    TRACE(TRACE_INFO, "TVDB", "No series id in response");
    htsmsg_destroy(gs);
    return METADATA_TEMPORARY_ERROR;
  }

  series_id = mystrdupa(series_id); // Make a copy of the ID
  htsmsg_destroy(gs);               // .. cause we destroyed the XML doc

  // --------------------------------------------------------------------
  // Get episode

  
  htsmsg_t *epi = loadxml("%s/series/%s/default/%d/%d/%s.xml",
			  TVDB_APIKEY, series_id, season, episode,
			  tvdb_language);
  if(epi == NULL)
    return METADATA_TEMPORARY_ERROR;

  metadata_t *md = metadata_create();
  md->md_type = METADATA_TYPE_VIDEO;

  htsmsg_t *tags = htsmsg_get_map_multi(epi,
					"tags", "Data",
					"tags", "Episode",
					"tags", NULL);


  struct season_list seasons;
  LIST_INIT(&seasons);

  int64_t series_vid = tvdb_find_series(db, series_id, qtype, &seasons);

  int64_t itemid = METADATA_TEMPORARY_ERROR;

  if(tags != NULL) {

    const char *se = htsmsg_get_cdata(tags, "SeasonNumber");

    if(se == NULL)
      goto out;

    int season = atoi(se);
	
    season_t *ses = NULL;
    int64_t season_vid = find_season(db, &seasons, series_id, season, qtype,
				     series_vid, &ses);
    if(season_vid < 0) {
      itemid = season_vid;
      goto out;
    }
    assert(ses != NULL);


    metadata_t *md = metadata_create();
    md->md_type = METADATA_TYPE_VIDEO;
    md->md_parent_id = ses->videoitem_id;

    md->md_title = rstr_alloc(htsmsg_get_cdata(tags, "EpisodeName"));
    md->md_description = rstr_alloc(htsmsg_get_cdata(tags, "Overview"));
    md->md_idx = episode;

    const char *s;
    if((s = htsmsg_get_cdata(tags, "Rating")) != NULL)
      md->md_rating = 10 * my_str2double(s, NULL);

    if((s = htsmsg_get_cdata(tags, "RatingCount")) != NULL)
      md->md_rating_count = atoi(s);

    md->md_imdb_id = rstr_alloc(htsmsg_get_cdata(tags, "IMDB_ID"));

    const char *extid = htsmsg_get_cdata(tags, "id");

    itemid = metadb_insert_videoitem(db, item_url, tvdb->ms_id, extid, md,
				     METAITEM_STATUS_COMPLETE, 0,
				     qtype, tvdb->ms_cfgid);

    if(itemid != -1) {
      metadb_delete_videoart(db, itemid);

      const char *thumb = htsmsg_get_cdata(tags, "filename");
      if(thumb) {
	char url[256];
	snprintf(url, sizeof(url), "http://www.thetvdb.com/banners/%s", thumb);

	metadb_insert_videoart(db, itemid, url, METADATA_IMAGE_POSTER, 0, 0,
			       1, NULL, 0);
      }
    }
    metadata_destroy(md);
  }

 out:
  // cleanup
  htsmsg_destroy(epi);
  flush_seasons(&seasons);
  return itemid;
}



static const metadata_source_funcs_t fns = {
  .query_by_episode = tvdb_query_by_episode,
};


/**
 *
 */
static void
set_lang(void *opaque, const char *str)
{
  if(str != NULL && strlen(str) != 2)
    str = NULL;
  snprintf(tvdb_language, sizeof(tvdb_language), "%s", str ?: "en");
  update_cfgid();
}


/**
 *
 */
static void
tvdb_init(void)
{

  snprintf(tvdb_language, sizeof(tvdb_language), "%s", "en");

  tvdb = metadata_add_source("tvdb", "thetvdb.com", 100000,
			     METADATA_TYPE_VIDEO, &fns,
			     // Properties we resolve for a partial lookup
			     0,
			     // Properties we resolve for a complete lookup
			     1 << METADATA_PROP_TITLE |
			     1 << METADATA_PROP_POSTER |
			     1 << METADATA_PROP_YEAR |
			     1 << METADATA_PROP_DESCRIPTION |
			     1 << METADATA_PROP_RATING |
			     1 << METADATA_PROP_RATING_COUNT |
			     1 << METADATA_PROP_GENRE |
			     1 << METADATA_PROP_CAST |
			     1 << METADATA_PROP_CREW |
			     1 << METADATA_PROP_BACKDROP
			     );

  if(tvdb == NULL)
    return;

  htsmsg_t *store = htsmsg_store_load("tvdb") ?: htsmsg_create_map();

  settings_create_string(tvdb->ms_settings, "language",
			 _p("Language (ISO 639-1 code)"),
			 NULL, store, set_lang, NULL,
			 SETTINGS_INITIAL_UPDATE, NULL,
			 settings_generic_save_settings, 
			 (void *)"tvdb");
}

INITME(INIT_GROUP_API, tvdb_init);


