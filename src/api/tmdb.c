/*
 *  API interface to themoviedb.org
 *  Copyright (C) 2010 Andreas Öman
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

#include "showtime.h"
#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"
#include "misc/pixmap.h"
#include "backend/backend.h"
#include "db/db_support.h"
#include "tmdb.h"

// http://help.themoviedb.org/kb/api/about-3

// Showtimes TMDB APIKEY
#define TMDB_APIKEY "a0d71cffe2d6693d462af9e4f336bc06"


static hts_mutex_t tmdb_mutex;
static int tmdb_datasource;
static char *tmdb_image_base_url;

typedef struct tmdb_image_size {
  struct tmdb_image_size *next;
  int width;
  int height;
  char *prefix;
} tmdb_image_size_t;


static tmdb_image_size_t *poster_sizes, *backdrop_sizes, *profile_sizes;


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
  metadb_insert_videoart(db, itemid, url, type, 0, 0);
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

  if(!tmdb_datasource) {

    char *result;
    char errbuf[256];
    result = fa_load_query("http://api.themoviedb.org/3/configuration",
			   NULL, errbuf, sizeof(errbuf), NULL,
			   (const char *[]){
			     "api_key", TMDB_APIKEY,
			       NULL, NULL},
			   FA_COMPRESSION);

    if(result == NULL) {
      TRACE(TRACE_INFO, "TMDB", "Unable to get configuration -- %s", errbuf);
      goto bad;
    }

    htsmsg_t *doc = htsmsg_json_deserialize(result);
    free(result);
    if(doc == NULL) {
      TRACE(TRACE_INFO, "TMDB", "Unable to parse configuration (JSON err)");
      goto bad;
    }
    
    tmdb_parse_config(doc);
    htsmsg_destroy(doc);


    // Get our datasource ID

    void *db = metadb_get();
    if(db != NULL) {
      if(!db_begin(db)) {
	tmdb_datasource = metadb_get_datasource(db, "tmdb");
	db_commit(db);
      }
      metadb_close(db);
    }
  }
 bad:
  hts_mutex_unlock(&tmdb_mutex);
  return !tmdb_datasource;
}



/**
 *
 */
static void
tmdb_load_movie_cast(void *db, int64_t itemid, const char *lookup_id)
{
  char url[300];
  char id[64];
  char errbuf[256];
  char *result;
  htsmsg_field_t *f;
  const char *s;

  snprintf(url, sizeof(url), "http://api.themoviedb.org/3/movie/%s/casts",
	   lookup_id);

  result = fa_load_query(url, NULL, errbuf, sizeof(errbuf),
			 NULL,
			 (const char *[]){
			   "api_key", TMDB_APIKEY,
			     NULL, NULL},
			 FA_COMPRESSION);
  if(result == NULL) {
    TRACE(TRACE_INFO, "TMDB", "Load error %s", errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(result);
  free(result);
  if(doc == NULL)
    return;

  htsmsg_t *cast = htsmsg_get_list(doc, "cast");
  HTSMSG_FOREACH(f, cast) {
    htsmsg_t *p = htsmsg_get_map_by_field(f);
    if(p == NULL)
      continue;

    s = htsmsg_get_str(p, "profile_path");
    snprintf(url, sizeof(url), s ? "tmdb:image:profile:%s" : "", s);

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
  HTSMSG_FOREACH(f, crew) {
    htsmsg_t *p = htsmsg_get_map_by_field(f);
    if(p == NULL)
      continue;

    s = htsmsg_get_str(p, "profile_path");
    snprintf(url, sizeof(url), s ? "tmdb:image:profile:%s" : "", s);
    snprintf(id, sizeof(id), "%d", htsmsg_get_u32_or_default(p, "id", 0));

    metadb_insert_videocast(db, itemid,
			    htsmsg_get_str(p, "name"),
			    NULL,
			    htsmsg_get_str(p, "department"),
			    htsmsg_get_str(p, "job"),
			    0,
			    url[0] ? url : NULL, 0, 0,
			    id);
  }

  htsmsg_destroy(doc);

}

/**
 *
 */
static void
tmdb_load_movie_info(void *db, const char *item_url, const char *lookup_id)
{
  char url[300];
  char errbuf[256];
  char *result;
  int64_t itemid;
  
  snprintf(url, sizeof(url), "http://api.themoviedb.org/3/movie/%s", lookup_id);

  result = fa_load_query(url, NULL, errbuf, sizeof(errbuf),
			 NULL,
			 (const char *[]){
			   "api_key", TMDB_APIKEY,
			     NULL, NULL},
			 FA_COMPRESSION);
  if(result == NULL) {
    TRACE(TRACE_INFO, "TMDB", "Load error %s", errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(result);
  free(result);
  if(doc == NULL)
    return;

  metadata_t *md = metadata_create();
  
  md->md_description = rstr_alloc(htsmsg_get_str(doc, "overview"));
  md->md_tagline = rstr_alloc(htsmsg_get_str(doc, "tagline"));
  md->md_imdb_id = rstr_alloc(htsmsg_get_str(doc, "imdb_id"));
  md->md_title = rstr_alloc(htsmsg_get_str(doc, "original_title"));

  double vote_average;

  if(!htsmsg_get_dbl(doc, "vote_average", &vote_average))
    md->md_rating = vote_average * 10;

  md->md_rate_count = htsmsg_get_s32_or_default(doc, "vote_count", -1);
  md->md_duration = htsmsg_get_s32_or_default(doc, "runtime", 0) * 60;
  md->md_year = atoi(htsmsg_get_str(doc, "release_date") ?: "");

  uint32_t id = htsmsg_get_u32_or_default(doc, "id", 0);
  if(id) {
    char tmdb_id[16];
    snprintf(tmdb_id, sizeof(tmdb_id), "%d", id);
    itemid = metadb_insert_videoitem(db, item_url, tmdb_datasource, 
				      tmdb_id, md);

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

    tmdb_load_movie_cast(db, itemid, lookup_id);
  }
  htsmsg_destroy(doc);
  metadata_destroy(md);
}

/**
 *
 */
void
tmdb_query_by_title_and_year(void *db, const char *item_url,
			     const char *title, int year)
{
  char buf[300];
  char errbuf[256];
  const char *q;
  char *result;

  if(tmdb_configure())
    return;

  if(year > 0) {
    snprintf(buf, sizeof(buf), "%s %d", title, year);
    q = buf;
  } else {
    q = title;
  }

  result = fa_load_query("http://api.themoviedb.org/3/search/movie",
			 NULL, errbuf, sizeof(errbuf), NULL,
		       (const char *[]){"query", q,
			   "api_key", TMDB_APIKEY,
			   NULL, NULL},
			 FA_COMPRESSION);

  if(result == NULL)
    return;

  htsmsg_t *doc = htsmsg_json_deserialize(result);
  free(result);
  if(doc == NULL)
    return;

  TRACE(TRACE_DEBUG, "TMDB", "Query '%s' -> %d pages %d results",
	q,
	htsmsg_get_s32_or_default(doc, "total_pages", -1),
	htsmsg_get_s32_or_default(doc, "total_results", -1));
  
  htsmsg_t *resultlist = htsmsg_get_list(doc, "results");
  if(resultlist == NULL)
    goto done1;
  htsmsg_field_t *f;
  htsmsg_t *best = NULL;
  float best_pop = 0;

  HTSMSG_FOREACH(f, resultlist) {
    htsmsg_t *res = htsmsg_get_map_by_field(f);
    double pop;
    if(res == NULL)
      continue;
    if(htsmsg_get_dbl(res, "popularity", &pop))
      pop = 0;
    if(best == NULL || pop > best_pop) {
      best = res;
      best_pop = pop;
    }
  }

  if(best != NULL)  {
    int32_t id;
    if(!htsmsg_get_s32(best, "id", &id)) {
      char idtxt[20];
      snprintf(idtxt, sizeof(idtxt), "%d", id);
      tmdb_load_movie_info(db, item_url, idtxt);
    }
  }

 done1:
  htsmsg_destroy(doc);
}


/**
 *
 */
void
tmdb_query_by_imdb_id(void *db, const char *item_url,
		      const char *imdb_id)
{
  if(tmdb_configure())
    return;

  tmdb_load_movie_info(db, item_url, imdb_id);
}




/**
 *
 */
void
tmdb_init(void)
{
  hts_mutex_init(&tmdb_mutex);
}



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
static pixmap_t *
be_tmdb_imageloader(const char *url, const image_meta_t *im,
		    const char **vpaths, char *errbuf, size_t errlen,
		    int *cache_control, be_load_cb_t *cb, void *opaque)
{
  tmdb_image_size_t *s;
  const char *p;
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
  htsmsg_destroy(m);
  pixmap_t *pm = backend_imageloader(rstr, im, vpaths, errbuf, errlen,
				     cache_control, cb, opaque);
  rstr_release(rstr);
  return pm;
  

}

/**
 *
 */
static backend_t be_tmdb = {
  .be_canhandle = be_tmdb_canhandle,
  .be_imageloader = be_tmdb_imageloader,
};

BE_REGISTER(tmdb);
