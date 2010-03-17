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

#include "htsmsg/htsmsg_json.h"
#include "fileaccess/fileaccess.h"

#include "tmdb.h"


// Showtimes TMDB APIKEY
#define TMDB_APIKEY "a0d71cffe2d6693d462af9e4f336bc06"

/**
 *
 */
struct htsmsg *
tmdb_query_by_hash(uint64_t hash, char *errbuf, size_t errlen)
{
  char url[256];
  char *result;
  size_t resultsize;
  htsmsg_t *r;

  snprintf(url, sizeof(url),
	   "http://api.themoviedb.org/2.1/Hash.getInfo/en/json/"
	   TMDB_APIKEY"/%016llx",
	   hash);

  int n = http_request(url, NULL, &result, &resultsize, errbuf, errlen,
		       NULL, NULL);
  
  if(n)
    return NULL;

  r = htsmsg_json_deserialize(result);
  free(result);
  if(r == NULL)
    snprintf(errbuf, errlen, "Unable to parse JSON");
  return r;
}

