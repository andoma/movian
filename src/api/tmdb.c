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

#include "tmdb.h"


// Showtimes TMDB APIKEY
#define TMDB_APIKEY "a0d71cffe2d6693d462af9e4f336bc06"

/**
 *
 */
static void
tmdb_load_movie_info(uint32_t id)
{
  char url[300];
  char errbuf[256];
  char *result;
  size_t resultsize;

  snprintf(url, sizeof(url), "http://api.themoviedb.org/v3/movie/%d", id);
  int n = http_request(url,
		       (const char *[]){
			   "api_key", TMDB_APIKEY,
			   NULL, NULL},
		       &result, &resultsize, errbuf, sizeof(errbuf),
		       NULL, NULL, HTTP_COMPRESSION,
		       NULL, NULL, NULL);
  if(n) {
    TRACE(TRACE_INFO, "TMDB", "Load error %s", errbuf);
    return;
  }

  htsmsg_t *doc = htsmsg_json_deserialize(result);
  free(result);
  if(doc == NULL)
    return;
  
  htsmsg_print(doc);
  htsmsg_destroy(doc);

}

/**
 *
 */
void
tmdb_query_by_title_and_year(const char *title, int year)
{
  char buf[300];
  char errbuf[256];
  const char *q;
  char *result;
  size_t resultsize;

  if(year > 0) {
    snprintf(buf, sizeof(buf), "%s %d", title, year);
    q = buf;
  } else {
    q = title;
  }

  int n = http_request("http://api.themoviedb.org/3/search/movie",
		       (const char *[]){"query", q,
			   "api_key", TMDB_APIKEY,
			   NULL, NULL},
		       &result, &resultsize, errbuf, sizeof(errbuf),
		       NULL, NULL, HTTP_COMPRESSION,
		       NULL, NULL, NULL);
  
  if(n)
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
    htsmsg_print(best);
    int32_t id;
    if(!htsmsg_get_s32(best, "id", &id))
      tmdb_load_movie_info(id);


  }


 done1:
  htsmsg_destroy(doc);
    
}

