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
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "main.h"
#include "misc/minmax.h"
#include "htsmsg/htsmsg_json.h"
#include "htsmsg/htsmsg_store.h"
#include "fileaccess/fileaccess.h"
#include "image/pixmap.h"
#include "backend/backend.h"
#include "db/db_support.h"
#include "settings.h"
#include "metadata/metadata_sources.h"
#include "usage.h"

#define TMDB_TRACE(x, ...) do {                                         \
    if(gconf.enable_metadata_debug)                                     \
      TRACE(TRACE_DEBUG, "TMDB", x, ##__VA_ARGS__);                     \
  } while(0)

// http://help.themoviedb.org/kb/api/about-3

// Showtimes TMDB APIKEY
#define TMDB_APIKEY "a0d71cffe2d6693d462af9e4f336bc06"


static hts_mutex_t tmdb_mutex;
static metadata_source_t *tmdb;
static char *tmdb_image_base_url;
static int tmdb_configured;
static char tmdb_language[3];
static int tmdb_use_orig_title;

typedef struct tmdb_image_size {
  struct tmdb_image_size *next;
  int width;
  int height;
  char *prefix;
} tmdb_image_size_t;


static tmdb_image_size_t *poster_sizes, *backdrop_sizes, *profile_sizes;


static int64_t tmdb_no_request_before;

/**
 *
 */
static void
tmdb_handle_rate_limit(struct http_header_list *response_headers)
{
  const char *retry = http_header_get(response_headers, "retry-after");
  int waittime = 5;
  if(retry != NULL)
    waittime = atoi(retry) + 1;

  http_headers_free(response_headers);

  TMDB_TRACE("Rate limited - Throttling requests for %d seconds", waittime);
  hts_mutex_lock(&tmdb_mutex);
  tmdb_no_request_before = arch_get_ts() + waittime * 1000000;
  hts_mutex_unlock(&tmdb_mutex);
}

static void
tmdb_check_rate_limit(void)
{
  while(1) {
    hts_mutex_lock(&tmdb_mutex);
    int64_t sleeptime = tmdb_no_request_before - arch_get_ts();
    hts_mutex_unlock(&tmdb_mutex);
    if(sleeptime < 0)
      return;
    usleep(MIN(sleeptime, 10000000));
  }
}

/**
 *
 */
static const char *
getlang(void)
{
  if(!*tmdb_language)
    return NULL;
  return tmdb_language;
}


/**
 *
 */
static void
update_cfgid(void)
{
  tmdb->ms_cfgid = 
    tmdb_language[0] | (tmdb_language[1] << 8) | (tmdb_use_orig_title << 16);
}


/**
 *
 */
static void
addsize(tmdb_image_size_t **p, const char *str, float aspect)
{
  int width = 0, height = 0;
  
  if(*str == 'w') {
    width = atoi(str+1);
    if(aspect > 0)
      height = width / aspect;
  }
  else if(*str == 'h') {
    height = atoi(str+1);
    if(aspect > 0)
      width = height * aspect;
  }
  tmdb_image_size_t *x = malloc(sizeof(tmdb_image_size_t));
  x->prefix = strdup(str);
  x->width = width;
  x->height = height;
  x->next = *p;
  *p = x;
}


static void
addsizes(tmdb_image_size_t **p, htsmsg_t *img, const char *field, float aspect)
{
  htsmsg_t *l = htsmsg_get_list(img, field);
  htsmsg_field_t *f;
  if(l == NULL)
    return;

  HTSMSG_FOREACH(f, l) {
    if(f->hmf_type == HMF_STR)
      addsize(p, f->hmf_str, aspect);
  }
}


/**
 *
 */
static void
insert_videoart(void *db, int64_t itemid, metadata_image_type_t type,
		const char *path, const char *pfx)
{
  char url[256];
  snprintf(url, sizeof(url), "tmdb:image:%s:%s", pfx, path);
  metadb_insert_videoart(db, itemid, url, type, 0, 0, 0, NULL, 0);
}


/**
 *
 */
static int
tmdb_parse_config(htsmsg_t *doc)
{
  htsmsg_t *img = htsmsg_get_map(doc, "images");
  const char *s;
  if(img == NULL)
    return -1;

  s = htsmsg_get_str(img, "base_url");
  if(s == NULL)
    return -1;
  tmdb_image_base_url = strdup(s);
  addsizes(&poster_sizes, img, "poster_sizes", 0.675);
  addsizes(&backdrop_sizes, img, "backdrop_sizes", 1.777777);
  addsizes(&profile_sizes, img, "profile_sizes", 0.675);
  return 0;
}


/**
 *
 */
static int
tmdb_configure(void)
{
  hts_mutex_lock(&tmdb_mutex);

  if(!tmdb_configured) {

    buf_t *result;
    char errbuf[256];
    result = fa_load("http://api.themoviedb.org/3/configuration",
                     FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                     FA_LOAD_QUERY_ARG("api_key", TMDB_APIKEY),
                     FA_LOAD_QUERY_ARG("language", getlang()),
                     FA_LOAD_FLAGS(FA_COMPRESSION | FA_IMPORTANT),
                     NULL);

    if(result == NULL) {
      TRACE(TRACE_INFO, "TMDB", "Unable to get configuration -- %s", errbuf);
      goto done;
    }

    htsmsg_t *doc = htsmsg_json_deserialize2(buf_cstr(result),
                                             errbuf, sizeof(errbuf));
    buf_release(result);

    if(doc == NULL) {
      TRACE(TRACE_ERROR, "TMDB", "Got bad JSON from config -- %s", errbuf);
      goto done;
    }
    
    tmdb_parse_config(doc);
    htsmsg_release(doc);
    tmdb_configured = 1;
  }
 done:
  hts_mutex_unlock(&tmdb_mutex);
  return !tmdb_configured;
}



/**
 *
 */
static htsmsg_t *
tmdb_load_movie_cast(const char *lookup_id)
{
  char url[300];
  char errbuf[256];
  buf_t *result;

  snprintf(url, sizeof(url), "http://api.themoviedb.org/3/movie/%s/casts",
	   lookup_id);

 retry:
  tmdb_check_rate_limit();
  int http_response_code = 0;
  struct http_header_list response_headers;
  LIST_INIT(&response_headers);

  result = fa_load(url,
                   FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                   FA_LOAD_QUERY_ARG("api_key", TMDB_APIKEY),
                   FA_LOAD_QUERY_ARG("language", getlang()),
                   FA_LOAD_RESPONSE_HEADERS(&response_headers),
                   FA_LOAD_PROTOCOL_CODE(&http_response_code),
                   FA_LOAD_FLAGS(FA_COMPRESSION),
                   NULL);
  if(result == NULL) {
    if(http_response_code == 429) {
      tmdb_handle_rate_limit(&response_headers);
      goto retry;
    }
    http_headers_free(&response_headers);
    TRACE(TRACE_INFO, "TMDB", "Load error %s", errbuf);
    return NULL;
  }
  http_headers_free(&response_headers);

  htsmsg_t *doc = htsmsg_json_deserialize2(buf_cstr(result),
                                           errbuf, sizeof(errbuf));
  if(doc == NULL) {
    TRACE(TRACE_ERROR, "TMDB", "Got bad JSON from %s -- %s", url, errbuf);
  }
  buf_release(result);
  return doc;
}


/**
 *
 */
static void
tmdb_insert_movie_cast(void *db, int64_t itemid, htsmsg_t *doc)
{
  char url[300];
  char id[64];
  htsmsg_field_t *f;
  const char *s;

  htsmsg_t *cast = htsmsg_get_list(doc, "cast");
  HTSMSG_FOREACH(f, cast) {
    htsmsg_t *p = htsmsg_get_map_by_field(f);
    if(p == NULL)
      continue;

    s = htsmsg_get_str(p, "profile_path");
    if(s)
      snprintf(url, sizeof(url), "tmdb:image:profile:%s", s);
    else
      url[0] = 0;

    snprintf(id, sizeof(id), "%d", htsmsg_get_u32_or_default(p, "id", 0));

    metadb_insert_videocast(db, itemid,
			    htsmsg_get_str(p, "name"),
			    htsmsg_get_str(p, "character"),
			    "Cast",
			    "Actor",
			    htsmsg_get_u32_or_default(p, "order", 0),
			    url[0] ? url : NULL, 0, 0,
			    id);
  }



  htsmsg_t *crew = htsmsg_get_list(doc, "crew");
  int o = 0;
  HTSMSG_FOREACH(f, crew) {
    htsmsg_t *p = htsmsg_get_map_by_field(f);
    if(p == NULL)
      continue;

    s = htsmsg_get_str(p, "profile_path");
    if(s)
      snprintf(url, sizeof(url), "tmdb:image:profile:%s", s);
    else
      url[0] = 0;

    snprintf(id, sizeof(id), "%d", htsmsg_get_u32_or_default(p, "id", 0));

    metadb_insert_videocast(db, itemid,
			    htsmsg_get_str(p, "name"),
			    NULL,
			    htsmsg_get_str(p, "department"),
			    htsmsg_get_str(p, "job"),
			    o++,
			    url[0] ? url : NULL, 0, 0,
			    id);
  }
}

/**
 *
 */
static int64_t
tmdb_load_movie_info(void *db, const char *item_url, const char *lookup_id,
		     int qtype, int *cache_info)
{
  char url[300];
  char errbuf[256];
  buf_t *result;

  snprintf(url, sizeof(url), "http://api.themoviedb.org/3/movie/%s", lookup_id);

 retry:
  tmdb_check_rate_limit();
  int http_response_code = 0;
  struct http_header_list response_headers;
  LIST_INIT(&response_headers);

  result = fa_load(url,
                   FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                   FA_LOAD_QUERY_ARG("api_key", TMDB_APIKEY),
                   FA_LOAD_QUERY_ARG("language", getlang()),
                   FA_LOAD_CACHE_INFO(cache_info),
                   FA_LOAD_RESPONSE_HEADERS(&response_headers),
                   FA_LOAD_PROTOCOL_CODE(&http_response_code),
                   FA_LOAD_FLAGS(FA_COMPRESSION),
                   NULL);
  if(result == NULL) {
    if(http_response_code == 429) {
      tmdb_handle_rate_limit(&response_headers);
      goto retry;
    }
    http_headers_free(&response_headers);
    TRACE(TRACE_INFO, "TMDB", "Load error %s", errbuf);
    return METADATA_TEMPORARY_ERROR;
  }
  http_headers_free(&response_headers);

  htsmsg_t *doc = htsmsg_json_deserialize2(buf_cstr(result),
                                           errbuf, sizeof(errbuf));
  buf_release(result);
  if(doc == NULL) {
    TRACE(TRACE_ERROR, "TMDB", "Got bad JSON from %s -- %s", url, errbuf);
    return METADATA_TEMPORARY_ERROR;
  }


  metadata_t *md = metadata_create();
  md->md_type = METADATA_TYPE_VIDEO;

  md->md_description = rstr_alloc(htsmsg_get_str(doc, "overview"));
  md->md_tagline = rstr_alloc(htsmsg_get_str(doc, "tagline"));
  md->md_imdb_id = rstr_alloc(htsmsg_get_str(doc, "imdb_id"));
  md->md_title = rstr_alloc(htsmsg_get_str(doc,
					   tmdb_use_orig_title ?
					   "original_title" : "title"));

  double vote_average;

  if(!htsmsg_get_dbl(doc, "vote_average", &vote_average))
    md->md_rating = vote_average * 10;

  md->md_rating_count = htsmsg_get_s32_or_default(doc, "vote_count", -1);
  md->md_duration = htsmsg_get_s32_or_default(doc, "runtime", 0) * 60;
  md->md_year = atoi(htsmsg_get_str(doc, "release_date") ?: "");

  int64_t itemid = METADATA_TEMPORARY_ERROR;

  uint32_t id = htsmsg_get_u32_or_default(doc, "id", 0);
  if(id) {
    htsmsg_t *cast = tmdb_load_movie_cast(lookup_id);
    double pop;
    if(htsmsg_get_dbl(doc, "popularity", &pop))
      pop = 0;
    
    char tmdb_id[16];
    snprintf(tmdb_id, sizeof(tmdb_id), "%d", id);
    itemid = metadb_insert_videoitem(db, item_url, tmdb->ms_id, tmdb_id, md,
				     METAITEM_STATUS_COMPLETE, pop * 1000,
				     qtype, tmdb->ms_cfgid);

    if(itemid >= 0) {

      const char *s;

      if((s = htsmsg_get_str(doc, "poster_path")) != NULL)
	insert_videoart(db, itemid, METADATA_IMAGE_POSTER, s, "poster");
      if((s = htsmsg_get_str(doc, "backdrop_path")) != NULL)
	insert_videoart(db, itemid, METADATA_IMAGE_BACKDROP, s, "backdrop");

      htsmsg_t *genres = htsmsg_get_list(doc, "genres");
      if(genres != NULL) {
	htsmsg_field_t *f;
	HTSMSG_FOREACH(f, genres) {
	  htsmsg_t *g = htsmsg_get_map_by_field(f);
	  if(g == NULL)
	    continue;
	  
	  const char *title = htsmsg_get_str(g, "name");
	  if(title != NULL)
	    metadb_insert_videogenre(db, itemid, title);
	}
      }
      
      if(cast != NULL)
	tmdb_insert_movie_cast(db, itemid, cast);
    }
    htsmsg_release(cast);
  }

  TMDB_TRACE("Loaded movie info for %s -- %s", lookup_id,
             itemid == METADATA_TEMPORARY_ERROR ? "Not found" : "Found");

  htsmsg_release(doc);
  metadata_destroy(md);
  return itemid;
}


/**
 *
 */
static int64_t
tmdb_query_by_title_and_year0(void *db, const char *item_url,
                              const char *title, int year, int duration,
                              int qtype, int *cache_info)
{
  char errbuf[256];
  buf_t *result;
  char yeartxt[20];

  if(year)
    snprintf(yeartxt, sizeof(yeartxt), "%d", year);
  else
    yeartxt[0] = 0;

  const char *url = "http://api.themoviedb.org/3/search/movie";

 retry:
  tmdb_check_rate_limit();
  int http_response_code = 0;
  struct http_header_list response_headers;
  LIST_INIT(&response_headers);

  result = fa_load(url,
                   FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                   FA_LOAD_QUERY_ARG("query", title),
                   FA_LOAD_QUERY_ARG("year", *yeartxt ? yeartxt : NULL),
                   FA_LOAD_QUERY_ARG("api_key", TMDB_APIKEY),
                   FA_LOAD_QUERY_ARG("language", getlang()),
                   FA_LOAD_RESPONSE_HEADERS(&response_headers),
                   FA_LOAD_PROTOCOL_CODE(&http_response_code),
                   FA_LOAD_FLAGS(FA_COMPRESSION),
                   FA_LOAD_CACHE_INFO(cache_info),
                   NULL);

  if(result == NULL) {
    if(http_response_code == 429) {
      tmdb_handle_rate_limit(&response_headers);
      goto retry;
    }
    http_headers_free(&response_headers);
    return METADATA_TEMPORARY_ERROR;
  }
  http_headers_free(&response_headers);

  htsmsg_t *doc = htsmsg_json_deserialize2(buf_cstr(result),
                                           errbuf, sizeof(errbuf));
  buf_release(result);
  if(doc == NULL) {
    TRACE(TRACE_ERROR, "TMDB", "Got bad JSON from %s -- %s", url, errbuf);
    return METADATA_TEMPORARY_ERROR;
  }
  int results = htsmsg_get_s32_or_default(doc, "total_results", 0);
  TMDB_TRACE("Query '%s' year:%d -> %d pages %d results",
	title, year,
	htsmsg_get_s32_or_default(doc, "total_pages", -1),
	results);

  htsmsg_t *resultlist = htsmsg_get_list(doc, "results");

  int64_t rval = METADATA_PERMANENT_ERROR;

  htsmsg_field_t *f;
  if(resultlist != NULL) HTSMSG_FOREACH(f, resultlist) {
    htsmsg_t *res = htsmsg_get_map_by_field(f);

    uint32_t id = htsmsg_get_u32_or_default(res, "id", 0);
    if(id == 0)
      continue;

    metadata_t *md = metadata_create();
    md->md_type = METADATA_TYPE_VIDEO;
    md->md_title = rstr_alloc(htsmsg_get_str(res, tmdb_use_orig_title ?
					     "original_title" : "title"));

    md->md_year = atoi(htsmsg_get_str(res, "release_date") ?: "");
    double pop;
    if(htsmsg_get_dbl(res, "popularity", &pop))
      pop = 0;

    char tmdb_id[16];
    snprintf(tmdb_id, sizeof(tmdb_id), "%d", id);
    int64_t itemid =
      metadb_insert_videoitem(db, item_url, tmdb->ms_id,
			      tmdb_id, md, METAITEM_STATUS_PARTIAL,
			      pop * 1000, qtype, tmdb->ms_cfgid);
    metadata_destroy(md);
    if(itemid < 0) {
      htsmsg_release(doc);
      return itemid;
    }

    const char *s;
    if((s = htsmsg_get_str(res, "poster_path")) != NULL)
      insert_videoart(db, itemid, METADATA_IMAGE_POSTER, s, "poster");
    if((s = htsmsg_get_str(res, "backdrop_path")) != NULL)
      insert_videoart(db, itemid, METADATA_IMAGE_BACKDROP, s, "backdrop");
    rval = 0;
  }
  htsmsg_release(doc);
  return rval;
}


/**
 *
 */
 __attribute__((unused)) static const char *
result_to_str(int64_t rval, int cache_info)
{
  switch(rval) {
  case METADATA_TEMPORARY_ERROR:
    return "Temporary error";
  case METADATA_PERMANENT_ERROR:
    return "Permanent error";
  default:

    switch(cache_info) {
    case FA_CACHE_INFO_FROM_CACHE:
      return "OK (Cached)";
    case FA_CACHE_INFO_FROM_CACHE_NOT_MODIFIED:
      return "OK (Cached-not-modified)";
    case FA_CACHE_INFO_EXPIRED_FROM_CACHE:
      return "OK (Cached-expired)";
    default:
      return "OK";
    }

  }
}

/**
 *
 */
static int64_t
tmdb_query_by_title_and_year(void *db, const char *item_url,
			     const char *title, int year, int duration,
			     int qtype, const char *initiator)
{
  int cache_info = 0;

  if(tmdb == NULL)
    return METADATA_TEMPORARY_ERROR;

  int64_t rval =
    tmdb_query_by_title_and_year0(db, item_url, title, year, duration, qtype,
                                  &cache_info);

  usage_event("TMDB query by title", 1,
              USAGE_SEG("qtype", metadata_qtypestr(qtype),
                        "initiator", initiator,
                        "result", result_to_str(rval, cache_info)));

  return rval;
}


/**
 *
 */
static int64_t
tmdb_query_by_imdb_id(void *db, const char *item_url, const char *imdb_id,
		      int qtype, const char *initiator)
{
  int cache_info = 0;

  if(tmdb == NULL)
    return METADATA_TEMPORARY_ERROR;

  int64_t rval = tmdb_load_movie_info(db, item_url, imdb_id, qtype,
                                      &cache_info);

  usage_event("TMDB query by IMDB-id", 1,
              USAGE_SEG("qtype", metadata_qtypestr(qtype),
                        "initiator", initiator,
                        "result", result_to_str(rval, cache_info)));

  return rval;
}

/**
 *
 */
static int64_t
tmdb_query_by_id(void *db, const char *item_url, const char *imdb_id,
                 const char *initiator)
{
  int cache_info = 0;

  if(tmdb == NULL)
    return METADATA_TEMPORARY_ERROR;


  int64_t rval = tmdb_load_movie_info(db, item_url, imdb_id, 0, &cache_info);

  usage_event("TMDB query by id", 1,
              USAGE_SEG("initiator", initiator,
                        "result", result_to_str(rval, cache_info)));

  return rval;
}


static const metadata_source_funcs_t search_fns = {
  .query_by_title_and_year = tmdb_query_by_title_and_year,
  .query_by_id = tmdb_query_by_id,
  .query_by_imdb_id = tmdb_query_by_imdb_id,
};


/**
 *
 */
static void
use_orig_title(void *opaque, int value)
{
  tmdb_use_orig_title = value;
  update_cfgid();
}


/**
 *
 */
static void
set_lang(void *opaque, const char *str)
{
  if(str)
    snprintf(tmdb_language, sizeof(tmdb_language), "%s", str);
  else
    memset(tmdb_language, 0, sizeof(tmdb_language));
  update_cfgid();
}

/**
 *
 */
static void
tmdb_init(void)
{
  hts_mutex_init(&tmdb_mutex);

  tmdb = metadata_add_source("tmdb", "themoviedb.org", 100001,
			     METADATA_TYPE_VIDEO, &search_fns,

			     // Properties we resolve for a partial lookup
			     1 << METADATA_PROP_TITLE |
			     1 << METADATA_PROP_POSTER |
			     1 << METADATA_PROP_YEAR,
			     // Properties we resolve for a complete lookup
			     1 << METADATA_PROP_TAGLINE |
			     1 << METADATA_PROP_DESCRIPTION |
			     1 << METADATA_PROP_RATING |
			     1 << METADATA_PROP_RATING_COUNT |
			     1 << METADATA_PROP_GENRE |
			     1 << METADATA_PROP_CAST |
			     1 << METADATA_PROP_CREW |
			     1 << METADATA_PROP_BACKDROP
			     );


  if(tmdb == NULL)
    return;

  setting_create(SETTING_STRING, tmdb->ms_settings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Language (ISO 639-1 code)")),
                 SETTING_CALLBACK(set_lang, NULL),
                 SETTING_STORE("tmdb", "language"),
                 NULL);


  setting_create(SETTING_BOOL, tmdb->ms_settings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Use original title")),
                 SETTING_CALLBACK(use_orig_title, NULL),
                 SETTING_STORE("tmdb", "enabled"),
                 NULL);
}


INITME(INIT_GROUP_API, tmdb_init, NULL, 0);

/**
 *
 */
static int
be_tmdb_canhandle(const char *url)
{
  if(!strncmp(url, "tmdb:", strlen("tmdb:")))
    return 1;
  return 0;
}


/**
 *
 */
static image_t *
be_tmdb_imageloader(const char *url, const image_meta_t *im,
		    const char **vpaths, char *errbuf, size_t errlen,
		    int *cache_control, cancellable_t *c)
{
  tmdb_image_size_t *s;
  const char *p;

  if(tmdb_configure()) {
    snprintf(errbuf, errlen, "Failed to load TMDB configuration");
    return NULL;
  }

  if((p = mystrbegins(url, "tmdb:image:poster:")) != NULL)
    s = poster_sizes;
  else if((p = mystrbegins(url, "tmdb:image:backdrop:")) != NULL)
    s = backdrop_sizes;
  else if((p = mystrbegins(url, "tmdb:image:profile:")) != NULL)
    s = profile_sizes;
  else {
    snprintf(errbuf, errlen, "Invalid TMDB url");
    return NULL;
  }
  
  htsmsg_t *m = htsmsg_create_list();
  
  for(;s != NULL; s = s->next) {
    htsmsg_t *img = htsmsg_create_map();
    char u[256];

    snprintf(u, sizeof(u), "%s%s%s", tmdb_image_base_url, s->prefix, p);
    htsmsg_add_str(img, "url", u);
    if(s->width)
      htsmsg_add_u32(img, "width", s->width);
    if(s->height)
    htsmsg_add_u32(img, "height", s->height);
    htsmsg_add_msg(m, NULL, img);
  }

  rstr_t *rstr = htsmsg_json_serialize_to_rstr(m, "imageset:");
  htsmsg_release(m);
  image_t *img = backend_imageloader(rstr, im, vpaths, errbuf, errlen,
				     cache_control, c);
  rstr_release(rstr);
  if(img != NULL && img != NOT_MODIFIED)
    img->im_flags |= IMAGE_ADAPTED;

  return img;
}

/**
 *
 */
static backend_t be_tmdb = {
  .be_canhandle = be_tmdb_canhandle,
  .be_imageloader = be_tmdb_imageloader,
};

BE_REGISTER(tmdb);
