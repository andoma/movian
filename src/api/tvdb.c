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

#include "showtime.h"
#include "metadata/metadata.h"
#include "htsmsg/htsmsg_xml.h"
#include "fileaccess/fileaccess.h"
#include "tvdb.h"

static int tvdb_datasource;

#define TVDB_APIKEY "0ADF8BA762FED295"



static int64_t 
tvdb_query_by_episode(void *db, const char *item_url, 
		      const char *title, int season, int episode,
		      int qtype)
{
  char *result;
  char errbuf[256];


  result = fa_load_query("http://www.thetvdb.com/api/GetSeries.php",
			 NULL, errbuf, sizeof(errbuf), NULL,
			 (const char *[]){
			   "seriesname", title,
			     NULL, NULL},
			 FA_COMPRESSION);

  if(result == NULL) {
    TRACE(TRACE_INFO, "TVDB", "Unable to query for %s -- %s", title, errbuf);
    return METADATA_ERROR;
  }
  
  htsmsg_t *msg = htsmsg_xml_deserialize(result, errbuf, sizeof(errbuf));
  if(msg == NULL) {
    TRACE(TRACE_INFO, "TVDB", "Unable to parse XML -- %s", errbuf);
    return METADATA_ERROR;
  }

  htsmsg_print(msg);

  htsmsg_destroy(msg);

  return METADATA_ERROR;
}



static const metadata_source_funcs_t fns = {
  .query_by_episode = tvdb_query_by_episode,
};



/**
 *
 */
void
tvdb_init(void)
{
  tvdb_datasource =
    metadata_add_source("tvdb", "thetvdb.com", 100000,
			METADATA_TYPE_VIDEO, &fns);
}

