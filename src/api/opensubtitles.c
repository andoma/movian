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

#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_xml.h"

#include "showtime.h"
#include "opensubtitles.h"
#include "settings.h"
#include "xmlrpc.h"
#include "fileaccess/fileaccess.h"
#include "media.h"
#include "i18n.h"
#include "misc/redblack.h"

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
static void
opensub_init(void)
{
  prop_t *s;
  hts_mutex_init(&opensub_mutex);

  s = subtitle_settings_dir;

  settings_create_separator(s, _p("Settings for opensubtitles.org"));

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


INITME(INIT_GROUP_API, opensub_init);


#if 0
/**
 *
 */
static htsmsg_t *
opensub_build_query(int64_t hash, int64_t movsize,
		    const char *imdb, const char *title)
{
  htsmsg_t *m = htsmsg_create_map();

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
#endif

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
    char v[256];
    snprintf(v, sizeof(v), "Showtime %s", htsversion_full);
    htsmsg_add_str(in, NULL, v);
    
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




static htsmsg_t *
makeq(htsmsg_t *queries, const char *lang, int season, int episode)
{
  htsmsg_t *q = htsmsg_create_map();
  if(*lang)
    htsmsg_add_str(q, "sublanguageid", lang);
  if(season > 0)
    htsmsg_add_u32(q, "season", season);
  if(episode > 0)
    htsmsg_add_u32(q, "episode", episode);
 
  htsmsg_add_msg(queries, NULL, q);
  return q;
}

RB_HEAD(dedup_tree, dedup);

typedef struct dedup {
  RB_ENTRY(dedup) link;
  char *str;
} dedup_t;


static int
dedup_cmp(const dedup_t *a, const dedup_t *b)
{
  return strcmp(a->str, b->str);
}


/**
 *
 */
void
opensub_query(struct prop *p, hts_mutex_t *mtx, uint64_t hash,
	      uint64_t size, const char *title, const char *imdb,
	      int season, int episode)
{
  char errbuf[256];
  htsmsg_t *queries, *in, *out, *m, *data, *entry, *q;
  htsmsg_field_t *f;
  uint32_t v;

  int r; // Retry counter

  char lang[16];

  const char *lang0 = i18n_subtitle_lang(0);
  const char *lang1 = i18n_subtitle_lang(1);
  const char *lang2 = i18n_subtitle_lang(2);
  struct dedup_tree dt;
  dedup_t *d;

  snprintf(lang, sizeof(lang), "%s%s%s%s%s",
	   lang0 ?: "",
	   lang0 && lang1 ? "," : "",
	   lang1 ?: "",
	   (lang0 || lang1) && lang2 ? "," : "",
	   lang2 ?: "");

  for(r = 0; r < 2; r++) {
    
    TRACE(TRACE_DEBUG, "opensubtitles", "Doing query #%d", r);
    TRACE(TRACE_DEBUG, "opensubtitles",
	  "  Hash = 0x%016llx, title=%s, imdbid=%s", hash,
	  title ?: "<unset>", imdb ?: "<unset>");
    if(opensub_login(r, errbuf, sizeof(errbuf))) {
      TRACE(TRACE_ERROR, "opensubtitles", "Unable to login: %s", errbuf);
      break;
    }

    queries = htsmsg_create_list();

    if(size) {
      q = makeq(queries, lang, season, episode);
      char str[20];
      snprintf(str, sizeof(str), "%" PRIx64, hash);
      htsmsg_add_str(q, "moviehash", str);
      htsmsg_add_s64(q, "moviebytesize", size);
    }

    if(imdb != NULL && imdb[0] == 't' && imdb[1] == 't') {
      q = makeq(queries, lang, season, episode);
      htsmsg_add_str(q, "imdbid", imdb+2);
    } else if(title != NULL) {
      q = makeq(queries, lang, season, episode);
      htsmsg_add_str(q, "query", title);
    }

    in = htsmsg_create_list();
    htsmsg_add_str(in, NULL, opensub_token);
    htsmsg_add_msg(in, NULL, queries);

    out = xmlrpc_request(OPENSUB_URL,
			 "SearchSubtitles", in, errbuf, sizeof(errbuf));

    if(out == NULL) {
      TRACE(TRACE_ERROR, "opensubtitles", "Unable to query: %s", errbuf);
      continue;
    }

    if((m = htsmsg_get_map_in_list(out, 1)) == NULL) {
      TRACE(TRACE_ERROR, "opensubtitles", "No parameter in response");
      htsmsg_destroy(out);
      continue;
    }
    
    const char *status = htsmsg_get_str(m, "status");

    if(status == NULL) {
      TRACE(TRACE_ERROR, "opensubtitles", "No 'status' field in response");
      htsmsg_destroy(out);
      continue;
    }

    TRACE(TRACE_DEBUG, "opensubtitles", "Response: %s", status);

    int code = atoi(status);

    if(code != 200) {
      TRACE(TRACE_DEBUG, "opensubtitles", "Request error, retrying...");
      htsmsg_destroy(out);
      continue;
    }

    if(!htsmsg_get_u32(m, "data", &v)) {
      TRACE(TRACE_DEBUG, "opensubtitles", "No subtitles available");
      htsmsg_destroy(out);
      break;
    }

    if((data = htsmsg_get_list(m, "data")) == NULL) {
      TRACE(TRACE_ERROR, "opensubtitles", "No 'data' field in response");
      htsmsg_destroy(out);
      continue;
    }
    int added = 0;

    RB_INIT(&dt);

    HTSMSG_FOREACH(f, data) {
      char src[64];
      if((entry = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      
      const char *url = htsmsg_get_str(entry, "SubDownloadLink");
      if(url == NULL)
	continue;


      const char *mb = htsmsg_get_str(entry, "MatchedBy");
      if(mb == NULL)
	continue;

      d = malloc(sizeof(dedup_t));
      d->str = strdup(url);
      if(RB_INSERT_SORTED(&dt, d, link, dedup_cmp)) {
	free(d->str);
	free(d);
	continue;
      }

      snprintf(src, sizeof(src), "Opensubtitles (%s)",  mb);

      int xscore = 0;
      if(!strcmp(mb, "moviehash"))
	xscore++;

      added++;
      hts_mutex_lock(mtx);
      mp_add_track(p,
		   htsmsg_get_str(entry, "SubFileName"),
		   url,
		   htsmsg_get_str(entry, "SubFormat"),
		   NULL,
		   htsmsg_get_str(entry, "SubLanguageID"),
		   src, NULL, xscore);
      hts_mutex_unlock(mtx);
    }
    TRACE(TRACE_DEBUG, "opensubtitles", "Got %d subtitles", added);
    
    htsmsg_destroy(out);

    while((d = dt.root) != NULL) {
      RB_REMOVE(&dt, d, link);
      free(d->str);
      free(d);
    }

    return;
  }
}




/**
 * Compute "opensubtitle" hash for the given file
 *
 * http://trac.opensubtitles.org/projects/opensubtitles/wiki/HashSourceCodes
 */
int
opensub_compute_hash(fa_handle_t *fh, uint64_t *hashp)
{
  int i;
  uint64_t hash;
  int64_t *mem;

  int64_t size = fa_fsize(fh);
  
  if(size < 65536)
    return -1;

  hash = size;

  if(fa_seek(fh, 0, SEEK_SET) != 0)
    return -1;

  mem = malloc(sizeof(int64_t) * 8192);

  if(fa_read(fh, mem, 65536) != 65536) {
    free(mem);
    return -1;
  }

  for(i = 0; i < 8192; i++) {
#if defined(__BIG_ENDIAN__)
    hash += __builtin_bswap64(mem[i]);
#else
    hash += mem[i];
#endif
  }

  if(fa_seek(fh, size - 65536, SEEK_SET) == -1 ||
     fa_read(fh, mem, 65536) != 65536) {
    free(mem);
    return -1;
  }

  for(i = 0; i < 8192; i++) {
#if defined(__BIG_ENDIAN__)
    hash += __builtin_bswap64(mem[i]);
#else
    hash += mem[i];
#endif
  }
  free(mem);
  *hashp = hash;
  return 0;
}
