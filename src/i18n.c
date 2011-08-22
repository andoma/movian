/*
 *  Internationalization
 *  Copyright (C) 2007-2011 Andreas Öman
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

#include "showtime.h"
#include "settings.h"
#include "i18n.h"
#include "misc/string.h"
#include "fileaccess/fileaccess.h"
#include "networking/http_server.h"

static void nls_init(prop_t *parent, htsmsg_t *store);



static char *lang_audio[3];
static char *lang_subtitle[3];
static const uint16_t *srt_charset;

static void
set_lang(void *opaque, const char *str)
{
  char **s = opaque;
  mystrset(s, str);
}

static void
set_srt_charset(void *opaque, const char *str)
{
  const charset_t *cs = charset_get(str);

  if(cs != NULL) {
    srt_charset = cs->ptr;
    TRACE(TRACE_DEBUG, "i18n", "SRT charset is %s", cs->title);
  }
}



void
i18n_init(void)
{
  prop_t *s = settings_add_dir(NULL, _p("Languages"), NULL, NULL,
			       _p("Preferred languages"));
  setting_t *x;
  int i;

  htsmsg_t *store = htsmsg_store_load("i18n");
  if(store == NULL)
    store = htsmsg_create_map();

  nls_init(s, store);


  settings_create_info(s, 
		       NULL,
		       _p("Language codes should be configured as three character ISO codes, example (eng, swe, fra)"));

  settings_create_string(s, "audio1", _p("Primary audio language code"), NULL, 
			 store, set_lang, &lang_audio[0],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "audio2", _p("Secondary audio language code"),
			 NULL, 
			 store, set_lang, &lang_audio[1],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "audio3", _p("Tertiary audio language code"), NULL, 
			 store, set_lang, &lang_audio[2],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");
  

  settings_create_string(s, "subtitle1", _p("Primary subtitle language code"),
			 NULL, store, set_lang, &lang_subtitle[0],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "subtitle2", _p("Secondary subtitle language code"),
			 NULL, store, set_lang, &lang_subtitle[1],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "subtitle3", _p("Tertiary subtitle language code"),
			 NULL, store, set_lang, &lang_subtitle[2],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  x = settings_create_multiopt(s, "srt_charset", _p("SRT character set"),
			       set_srt_charset, NULL);

  const charset_t *cs;
  for(i = 0; (cs = charset_get_idx(i)) != NULL; i++)
    settings_multiopt_add_opt_cstr(x, cs->id, cs->title, i == 0);

  settings_multiopt_initiate(x, store, settings_generic_save_settings, 
			     (void *)"i18n");
}


/**
 *
 */
static int
findscore(const char *str, char *vec[])
{
  int i;
  if(str == NULL || *str == 0)
    return 0;

  for(i = 0; i < 3; i++)
    if(vec[i] && !strcasecmp(vec[i], str))
      return 100 * (3 - i);
  return 0;
}


/**
 *
 */
int
i18n_audio_score(const char *str)
{
  return findscore(str, lang_audio);
}

/**
 *
 */
int
i18n_subtitle_score(const char *str)
{
  return findscore(str, lang_subtitle);
}


/**
 *
 */
const uint16_t *
i18n_get_srt_charset(void)
{
  return srt_charset;
}


LIST_HEAD(nls_string_queue, nls_string);


/**
 *
 */
typedef struct nls_string {
  rstr_t *ns_key;
  LIST_ENTRY(nls_string) ns_link;
  prop_t *ns_prop;
  rstr_t *ns_value;
} nls_string_t;

static struct nls_string_queue nls_strings;



/**
 *
 */
static nls_string_t *
nls_string_find(const char *key)
{
  nls_string_t *ns;

  LIST_FOREACH(ns, &nls_strings, ns_link)
    if(!strcmp(rstr_get(ns->ns_key), key))
      break;

  if(ns == NULL) {

    ns = calloc(1, sizeof(nls_string_t));
    ns->ns_key = rstr_alloc(key);
    ns->ns_prop = prop_create_root(NULL);
    prop_set_rstring(ns->ns_prop, ns->ns_key);

  } else {
    LIST_REMOVE(ns, ns_link);
  }
  LIST_INSERT_HEAD(&nls_strings, ns, ns_link);
  return ns;
}


/**
 *
 */
prop_t *
nls_get_prop(const char *string)
{
  nls_string_t *ns = nls_string_find(string);
  return ns->ns_prop;
}


/**
 *
 */
rstr_t *
nls_get_rstring(const char *string)
{
  nls_string_t *ns = nls_string_find(string);
  if(ns->ns_value == NULL)
    return rstr_dup(ns->ns_key);
  return rstr_dup(ns->ns_value);
}


/**
 *
 */
static void
nls_clear(void)
{
  nls_string_t *ns;
  LIST_FOREACH(ns, &nls_strings, ns_link) {
    rstr_release(ns->ns_value);
    ns->ns_value = NULL;
    prop_set_rstring(ns->ns_prop, ns->ns_key);
  }
}


/**
 *
 */
static void
deescape_cstyle(char *src)
{
  char *dst = src;
  while(*src) {
    if(*src == '\\') {
      src++;
      if(*src == 0)
	break;
      if(*src == 'n')
	*dst++ = '\n';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = 0;
}


/**
 *
 */
static void
nls_load_from_data(char *s)
{
  const char *s2;

  // Skip UTF-8 BOM
  if(!memcmp(s, (const uint8_t []){0xef, 0xbb, 0xbf}, 3))
    s+=3;

  int l;
  nls_string_t *ns = NULL;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
    if(s[0] == '#')
      continue;

    if((s2 = mystrbegins(s, "id:")) != NULL) {
      while(*s2 <33 && *s2)
	s2++;
      
      deescape_cstyle((char *)s2);
      ns = nls_string_find(s2);
      continue;
    }

    if(ns == NULL)
      continue;

    if((s2 = mystrbegins(s, "msg:")) != NULL) {
      
      while(*s2 <33 && *s2)
	s2++;
      
      if(*s2) {
	rstr_release(ns->ns_value);
	deescape_cstyle((char *)s2);
	ns->ns_value = rstr_alloc(s2);
	prop_set_rstring(ns->ns_prop, ns->ns_value);
      }
    }
  }
}

/**
 *
 */
static void
nls_load_lang(const char *path)
{
  char errbuf[200];
  fa_stat_t fs;
  char *data = fa_quickload(path, &fs, NULL, errbuf, sizeof(errbuf));

  if(data == NULL) {
    TRACE(TRACE_ERROR, "NLS", "Unable to load %s -- %s", path, errbuf);
    return;
  }

  nls_load_from_data(data);
  free(data);
}


/**
 *
 */
static void
set_language(void *opaque, const char *str)
{
  char buf[200];

  nls_clear();

  if(!strcmp(str, "none")) {
    TRACE(TRACE_INFO, "i18n", "Unloading language definition");
    return;
  }

  snprintf(buf, sizeof(buf), "%s/%s.lang", SHOWTIME_LANGUAGES_URL, str);
  TRACE(TRACE_INFO, "i18n", "Loading language %s", str);
  nls_load_lang(buf);
}


static int
nls_lang_metadata(const char *path, char *errbuf, size_t errlen,
		  char *language, size_t languagesize,
		  char *native, size_t nativesize)
{
  fa_stat_t fs;

  char *data = fa_quickload(path, &fs, NULL, errbuf, errlen);
  char *s;
  const char *s2;
  if(data == NULL)
    return -1;


  *language = 0;
  *native = 0;

  s = data;
  // Skip UTF-8 BOM
  if(!memcmp(s, (const uint8_t []){0xef, 0xbb, 0xbf}, 3))
    s+=3;

  int l;
  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    s[l] = 0;
    if(s[0] == '#')
      continue;
    
    
    if((s2 = mystrbegins(s, "language:")) != NULL) {
      while(*s2 <33 && *s2)
	s2++;
      snprintf(language, languagesize, "%s", s2);
    }

    if((s2 = mystrbegins(s, "native:")) != NULL) {
      while(*s2 <33 && *s2)
	s2++;
      snprintf(native, nativesize, "%s", s2);
    }
    if(*language && *native)
      break;
  }

  free(data);

  if(*language && *native)
    return 0;
  
  snprintf(errbuf, errlen, "Not a valid language file");
  return -1;
}

/**
 *
 */
static int
upload_translation(http_connection_t *hc, const char *remain, void *opaque,
		   http_cmd_t method)
{
  size_t len;
  void *data = http_get_post_data(hc, &len, 0);

  if(method != HTTP_CMD_POST || data == NULL)
    return 405;

  nls_clear();
  nls_load_from_data(data);
  TRACE(TRACE_INFO, "i18n", "Loading language from HTTP POST");
  return HTTP_STATUS_OK;
}


typedef struct lang {
  LIST_ENTRY(lang) link;
  const char *id;
  const char *str;
} lang_t;

/**
 *
 */
static int 
langcmp(lang_t *a, lang_t *b)
{
  return dictcmp(a->str, b->str);
}



/**
 *
 */
static void 
nls_init(prop_t *parent, htsmsg_t *store)
{
  setting_t  *x;
  char buf[200];
  char buf2[200];
  fa_dir_t *fd = fa_scandir(SHOWTIME_LANGUAGES_URL, buf, sizeof(buf));
  fa_dir_entry_t *fde;
  char language[64];
  char native[64];
  char *e;
  LIST_HEAD(, lang) list;
  lang_t *l;

  http_path_add("/showtime/translation", NULL, upload_translation);

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "i18n", "Unable to scan languages in %s -- %s",
	  SHOWTIME_LANGUAGES_URL, buf);
    return;
  }

  x = settings_create_multiopt(parent, "language", _p("Language"),
			       set_language, NULL);

  settings_multiopt_add_opt_cstr(x, "none", "English (default)", 1);
  LIST_INIT(&list);

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(fde->fde_filename[strlen(fde->fde_filename) - 1] == '~')
      continue;

    snprintf(buf, sizeof(buf), "%s", fde->fde_filename);
    if((e = strstr(buf, ".lang")) == NULL)
      continue;
    *e = 0;

    if(nls_lang_metadata(fde->fde_url, 
			 buf2, sizeof(buf2),
			 language, sizeof(language), 
			 native, sizeof(native))) {
      TRACE(TRACE_ERROR, "i18n", "Unable to load language from %s -- %s",
	    fde->fde_url, buf2);
      continue;
    }
    l = alloca(sizeof(lang_t));
    l->id = mystrdupa(buf);
    snprintf(buf2, sizeof(buf2), "%s (%s)", native, language);
    l->str = mystrdupa(buf2);
    LIST_INSERT_SORTED(&list, l, link, langcmp);
  }

  LIST_FOREACH(l, &list, link)
    settings_multiopt_add_opt_cstr(x, l->id, l->str, 0);

  settings_multiopt_initiate(x, store, settings_generic_save_settings, 
			     (void *)"i18n");
  
  fa_dir_free(fd);
}
