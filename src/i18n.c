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

#include <assert.h>
#include <stdio.h>

#include "showtime.h"
#include "settings.h"
#include "i18n.h"
#include "misc/str.h"
#include "fileaccess/fileaccess.h"
#include "networking/http_server.h"

static void nls_init(prop_t *parent, htsmsg_t *store);



static char lang_audio[3][4];
static char lang_subtitle[3][4];
static const uint16_t *srt_charset;

static void
set_lang(void *opaque, const char *str)
{
  char *s = (char *)opaque;
  if(str == NULL) {
    *s = 0;
    return;
  }

  if(strlen(str) > 3) {
    memcpy(s, str, 3);
    s[3] = 0;
  } else {
    strcpy(s, str);
  }
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
			       _p("Preferred languages"),
			       "settings:i18n");
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

  x = settings_create_multiopt(s, "srt_charset", _p("SRT character set"), 0);
			       

  const charset_t *cs;
  for(i = 0; (cs = charset_get_idx(i)) != NULL; i++)
    settings_multiopt_add_opt_cstr(x, cs->id, cs->title, i == 0);

  settings_multiopt_initiate(x,
			     set_srt_charset, NULL, NULL,
			     store, settings_generic_save_settings, 
			     (void *)"i18n");
}


/**
 *
 */
static int
findscore(const char *str, char vec[][4])
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
const char *
i18n_subtitle_lang(unsigned int num)
{
  return num < 3 && lang_subtitle[num][0] ? lang_subtitle[num] : NULL;
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

  int ns_values;

  union {
    rstr_t *rstr;
    rstr_t **vec;
  } ns_u;

} nls_string_t;

static struct nls_string_queue nls_strings;


/**
 *
 */
static void
ns_val_set(nls_string_t *ns, int idx, const char *value)
{
  if(ns->ns_values == 0 && idx == 0) {
    ns->ns_values = 1;
    ns->ns_u.rstr = rstr_alloc(value);
    return;
  }
  if(ns->ns_values == 1 && idx == 0) {
    rstr_release(ns->ns_u.rstr);
    ns->ns_u.rstr = rstr_alloc(value);
    return;
  }

  if(idx < ns->ns_values) {
    assert(ns->ns_values > 1);
    rstr_release(ns->ns_u.vec[idx]);
    ns->ns_u.vec[idx] = rstr_alloc(value);
    return;
  }


  rstr_t *p = NULL;
  if(ns->ns_values == 1) {
    p = ns->ns_u.rstr;
    ns->ns_values = 0;
    assert(idx > 0);
  }
  ns->ns_u.vec = realloc(ns->ns_values ? ns->ns_u.vec : NULL,
			 (1+idx) * sizeof(rstr_t *));
  if(p)
    ns->ns_u.vec[0] = p;

  ns->ns_u.vec[idx] = rstr_alloc(value);
  ns->ns_values = idx + 1;
}


/**
 *
 */
static rstr_t *
ns_val_get(nls_string_t *ns, int idx)
{
  if(ns->ns_values == 0)
    return NULL;
  if(ns->ns_values == 1 && idx == 0)
    return ns->ns_u.rstr;
  if(idx < ns->ns_values)
    return ns->ns_u.vec[idx];
  else
    return NULL;
}


/**
 *
 */
static void
ns_val_clr(nls_string_t *ns)
{
  if(ns->ns_values == 1) {
    rstr_release(ns->ns_u.rstr);
    ns->ns_u.rstr = NULL;
  } else if(ns->ns_values > 1) {
    int i;
    for(i = 0; i < ns->ns_values; i++)
      rstr_release(ns->ns_u.vec[i]);
    free(ns->ns_u.vec);
    ns->ns_u.vec = NULL;
  }
  ns->ns_values = 0;
}


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
  return nls_string_find(string)->ns_prop;
}


/**
 *
 */
rstr_t *
nls_get_rstringp(const char *string, const char *singularis, int val)
{
  rstr_t *r;
  nls_string_t *ns = nls_string_find(string);
  if(val == 1)
    r =  rstr_dup(ns_val_get(ns, 1) ?: rstr_alloc(singularis));
  else
    r = rstr_dup(ns_val_get(ns, 0) ?: ns->ns_key);
  return r;
}


/**
 *
 */
rstr_t *
nls_get_rstring(const char *string)
{
  nls_string_t *ns = nls_string_find(string);
  return rstr_dup(ns_val_get(ns, 0) ?: ns->ns_key);
}


/**
 *
 */
static void
nls_clear(void)
{
  nls_string_t *ns;
  LIST_FOREACH(ns, &nls_strings, ns_link) {
    ns_val_clr(ns);
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
      ns_val_clr(ns);
      continue;
    }

    if(ns == NULL)
      continue;

    if((s2 = mystrbegins(s, "msg:")) != NULL) {
      
      while(*s2 <33 && *s2)
	s2++;
      
      if(*s2) {
	deescape_cstyle((char *)s2);
	ns_val_set(ns, 0, s2);
	prop_set_rstring(ns->ns_prop, ns_val_get(ns, 0));
      }
      continue;
    }

    if((s2 = mystrbegins(s, "msg[")) != NULL) {
      while(*s2 <33 && *s2)
	s2++;
      
      int i = atoi(s2);
      while(*s2 != ']' && *s2)
	s2++;
      if(*s2)
	s2++;
      while(*s2 <33 && *s2)
	s2++;
      if(*s2 != ':')
	continue;
      s2++;
      while(*s2 <33 && *s2)
	s2++;
      
      if(*s2) {
	deescape_cstyle((char *)s2);
	ns_val_set(ns, i, s2);
	if(i == 0)
	  prop_set_rstring(ns->ns_prop, ns_val_get(ns, 0));
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
  char *data = fa_load(path, NULL, NULL, errbuf, sizeof(errbuf), NULL, 0,
		       NULL, NULL);

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

  snprintf(buf, sizeof(buf), "%s/lang/%s.lang", showtime_dataroot(), str);
  TRACE(TRACE_INFO, "i18n", "Loading language %s", str);
  nls_load_lang(buf);
}


static int
nls_lang_metadata(const char *path, char *errbuf, size_t errlen,
		  char *language, size_t languagesize,
		  char *native, size_t nativesize)
{
  char *data = fa_load(path, NULL, NULL, errbuf, errlen, NULL, 0,
		       NULL, NULL);
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
  snprintf(buf2, sizeof(buf2), "%s/lang", showtime_dataroot());
  fa_dir_t *fd = fa_scandir(buf2, buf, sizeof(buf));
  fa_dir_entry_t *fde;
  char language[64];
  char native[64];
  char *e;
  LIST_HEAD(, lang) list;
  lang_t *l;

  http_path_add("/showtime/translation", NULL, upload_translation, 1);

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "i18n", "Unable to scan languages in %s/lang -- %s",
	  showtime_dataroot(), buf);
    return;
  }

  x = settings_create_multiopt(parent, "language", _p("Language"), 0);
			       

  settings_multiopt_add_opt_cstr(x, "none", "English (default)", 1);
  LIST_INIT(&list);

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
    const char *filename = rstr_get(fde->fde_filename);
    if(filename[strlen(filename) - 1] == '~')
      continue;

    snprintf(buf, sizeof(buf), "%s", filename);
    if((e = strstr(buf, ".lang")) == NULL)
      continue;
    *e = 0;

    if(nls_lang_metadata(rstr_get(fde->fde_url), 
			 buf2, sizeof(buf2),
			 language, sizeof(language), 
			 native, sizeof(native))) {
      TRACE(TRACE_ERROR, "i18n", "Unable to load language from %s -- %s",
	    rstr_get(fde->fde_url), buf2);
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

  settings_multiopt_initiate(x,
			     set_language, NULL, NULL,
			     store, settings_generic_save_settings, 
			     (void *)"i18n");
  
  fa_dir_free(fd);
}
