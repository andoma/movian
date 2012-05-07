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
insert_images(void *db, int64_t itemid, metadata_image_type_t type,
	      const char *path, tmdb_image_size_t *p)
{
  char url[256];
  for(;p != NULL; p = p->next) {
    snprintf(url, sizeof(url), "%s%s%s", tmdb_image_base_url,
	     p->prefix, path);
    metadb_insert_videoart(db, itemid, url, type, p->width, p->height);
  }
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
tmdb_load_movie_info(void *db, const char *item_url, const char *lookup_id)
{
  char url[300];
  char errbuf[256];
  char *result;
  
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
    int64_t itemid = metadb_insert_videoitem(db, item_url, tmdb_datasource,
					     tmdb_id, md);

    const char *s;
  
  
    if((s = htsmsg_get_str(doc, "poster_path")) != NULL)
      insert_images(db, itemid, METADATA_IMAGE_POSTER, s, poster_sizes);
    if((s = htsmsg_get_str(doc, "backdrop_path")) != NULL)
      insert_images(db, itemid, METADATA_IMAGE_BACKDROP, s, backdrop_sizes);

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
