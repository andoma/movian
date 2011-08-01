/*
 *  Open subtitles interface
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

#include <htsmsg/htsmsg.h>
#include <htsmsg/htsmsg_xml.h>

#include "showtime.h"
#include "opensubtitles.h"
#include "settings.h"
#include "xmlrpc.h"
#include "fileaccess/fileaccess.h"
#include "media.h"

#define OPENSUB_URL "http://api.opensubtitles.org/xml-rpc"

static hts_mutex_t opensub_mutex;
static int opensub_enable;
static char *opensub_username;
static char *opensub_password;
static char *opensub_token; // Token if logged in

/**
 *
 */
static void
set_enable(void *opaque, int value)
{
  hts_mutex_lock(&opensub_mutex);
  opensub_enable = value;
  hts_mutex_unlock(&opensub_mutex);
}

/**
 *
 */
static void
set_username(void *opaque, const char *v)
{
  hts_mutex_lock(&opensub_mutex);
  free(opensub_username);
  opensub_username = v ? strdup(v) : NULL;
  hts_mutex_unlock(&opensub_mutex);
}

/**
 *
 */
static void
set_password(void *opaque, const char *v)
{
  hts_mutex_lock(&opensub_mutex);
  free(opensub_password);
  opensub_password = v ? strdup(v) : NULL;
  hts_mutex_unlock(&opensub_mutex);
}

extern struct prop *subtitle_settings_dir;

/**
 *
 */
void
opensub_init(void)
{
  prop_t *s;
  hts_mutex_init(&opensub_mutex);

  s = subtitle_settings_dir;

  settings_create_divider(s, _p("Settings for opensubtitles.org"));

  htsmsg_t *store = htsmsg_store_load("opensubtitles");
  if(store == NULL)
    store = htsmsg_create_map();

  settings_create_bool(s, "enable", _p("Use opensubtitles.org"), 0, 
		       store, set_enable, NULL,
		       SETTINGS_INITIAL_UPDATE, NULL,
		       settings_generic_save_settings,
		       (void *)"opensubtitles");

  settings_create_string(s, "username", _p("Username"), NULL, 
			 store, set_username, NULL,
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"opensubtitles");

  settings_create_string(s, "password", _p("Password"), NULL, 
			 store, set_password, NULL, 
			 SETTINGS_INITIAL_UPDATE | SETTINGS_PASSWORD, NULL,
			 settings_generic_save_settings,
			 (void *)"opensubtitles");
}







/**
 *
 */
htsmsg_t *
opensub_build_query(const char *lang, int64_t hash, int64_t movsize,
		    const char *imdb, const char *title)
{
  htsmsg_t *m = htsmsg_create_map();

  if(lang != NULL) 
    htsmsg_add_str(m, "sublanguageid", lang);

  if(movsize) {
    char str[20];
    snprintf(str, sizeof(str), "%" PRIx64, hash);
    htsmsg_add_str(m, "moviehash", str);
    htsmsg_add_s64(m, "moviebytesize", movsize);
  }

  if(imdb != NULL) 
    htsmsg_add_str(m, "imdbid", imdb);

  if(title != NULL) 
    htsmsg_add_str(m, "query", title);

  return m;
}

/**
 *
 */
static int
opensub_login(int force, char *errbuf, size_t errlen)
{
  hts_mutex_lock(&opensub_mutex);

  if(opensub_token == NULL || force) {

    htsmsg_t *in = htsmsg_create_list(), *out, *m;

    htsmsg_add_str(in, NULL, opensub_username ?: "");
    htsmsg_add_str(in, NULL, opensub_password ?: "");
    htsmsg_add_str(in, NULL, "en");
    htsmsg_add_str(in, NULL, "Showtime v2.9");
    
    TRACE(TRACE_DEBUG, "opensubtitles", "Logging in as user '%s'", 
	  opensub_username);

    out = xmlrpc_request(OPENSUB_URL, "LogIn", in, errbuf, errlen);

    if(out == NULL) {
      hts_mutex_unlock(&opensub_mutex);
      return -1;
    }

    if((m = htsmsg_get_map_in_list(out, 1)) == NULL) {
      snprintf(errbuf, errlen, "Malformed response, no parameters");
    err:
      htsmsg_destroy(out);
      hts_mutex_unlock(&opensub_mutex);
      return -1;
    }

    const char *token = htsmsg_get_str(m, "token");

    if(token == NULL) {
      snprintf(errbuf, errlen, "Malformed response, no token");
      goto err;
    }

    TRACE(TRACE_DEBUG, "opensubtitles", "Login ok, token: %s", 
	  token);

    free(opensub_token);
    opensub_token = strdup(token);
    htsmsg_destroy(out);
  }
  hts_mutex_unlock(&opensub_mutex);
  return 0;
}

/**
 *
 */
typedef struct opensub_async_query {
  prop_t *node;
  htsmsg_t *query;
} opensub_async_query_t;



/**
 *
 */
static void
async_query_do(prop_t *node, htsmsg_t *query)
{
  char errbuf[256];
  htsmsg_t *queries, *in, *out, *m, *data, *entry;
  htsmsg_field_t *f;
  uint32_t v;

  if(opensub_login(0, errbuf, sizeof(errbuf))) {
    TRACE(TRACE_ERROR, "opensubtitles", "Unable to login: %s", errbuf);
    htsmsg_destroy(query);
    return;
  }

  queries = htsmsg_create_list();
  htsmsg_add_msg(queries, NULL, query);

  in = htsmsg_create_list();
  htsmsg_add_str(in, NULL, opensub_token);
  htsmsg_add_msg(in, NULL, queries);

  out = xmlrpc_request(OPENSUB_URL,
		       "SearchSubtitles", in, errbuf, sizeof(errbuf));
  if(out == NULL) {
    TRACE(TRACE_ERROR, "opensubtitles", "Unable to query: %s", errbuf);
    return;
  }

  if((m = htsmsg_get_map_in_list(out, 1)) == NULL) {
    TRACE(TRACE_ERROR, "opensubtitles", "No parameter in response");
    htsmsg_destroy(out);
    return;
  }
  
  if(!htsmsg_get_u32(m, "data", &v)) {
    TRACE(TRACE_DEBUG, "opensubtitles", "No subtitles available");
    htsmsg_destroy(out);
    return;
  }

  if((data = htsmsg_get_list(m, "data")) == NULL) {
    TRACE(TRACE_ERROR, "opensubtitles", "No 'data' field in response");
    htsmsg_destroy(out);
    return;
  }

  TRACE(TRACE_DEBUG, "opensubtitles", "Got response");

  HTSMSG_FOREACH(f, data) {
    if((entry = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    const char *url = htsmsg_get_str(entry, "SubDownloadLink");

    if(url == NULL)
      continue;

    mp_add_track(node, NULL, url,
		 htsmsg_get_str(entry, "SubFormat"),
		 NULL,
		 htsmsg_get_str(entry, "SubLanguageID"),
		 "opensubtitles.org", 0);
  }

  htsmsg_destroy(out);
}

/**
 *
 */
static void *
async_query_thread(void *aux)
{
  opensub_async_query_t *oaq = aux;

  // query is consumed by this func
  async_query_do(oaq->node, oaq->query);

  prop_ref_dec(oaq->node);
  free(oaq);
  return NULL;
}

/**
 *
 */
void
opensub_add_subtitles(prop_t *node, htsmsg_t *query)
{
  opensub_async_query_t *oaq;

  if(opensub_enable == 0) {
    htsmsg_destroy(query);
    return;
  }

  oaq = malloc(sizeof(opensub_async_query_t));

  oaq->node = prop_ref_inc(node);
  oaq->query = query;

  hts_thread_create_detached("opensub query", async_query_thread, oaq,
			     THREAD_PRIO_LOW);
}


/**
 * Compute "opensubtitle" hash for the given file
 *
 * http://trac.opensubtitles.org/projects/opensubtitles/wiki/HashSourceCodes
 */
int
opensub_compute_hash(AVIOContext *avio, uint64_t *hashp)
{
  int i;
  uint64_t hash;

  int64_t size = avio_size(avio);
  
  if(size < 65536)
    return -1;

  hash = size;

  if(avio_seek(avio, 0, SEEK_SET) == -1)
    return -1;

  for(i = 0; i < 8192; i++)
    hash += avio_rl64(avio);

  if(avio_seek(avio, size-65536, SEEK_SET) == -1)
    return -1;

  for(i = 0; i < 8192; i++)
    hash += avio_rl64(avio);

  *hashp = hash;
  return 0;
}
