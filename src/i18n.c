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
#include <assert.h>
#include <stdio.h>

#include "main.h"
#include "settings.h"
#include "i18n.h"
#include "misc/str.h"
#include "misc/callout.h"
#include "fileaccess/fileaccess.h"
#if ENABLE_HTTPSERVER
#include "networking/http_server.h"
#endif

static HTS_MUTEX_DECL(nls_mutex);

static void nls_init(prop_t *parent);



static char lang_audio[3][4];
static char lang_subtitle[3][4];
static const charset_t *default_charset;

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
set_default_charset(void *opaque, const char *str)
{
  if(!strcmp(str ?: "", "auto")) {
    default_charset = NULL;
    TRACE(TRACE_DEBUG, "i18n", "Default charset is auto");
    return;
  }

  const charset_t *cs = charset_get(str);

  if(cs != NULL) {
    default_charset = cs;
    TRACE(TRACE_DEBUG, "i18n", "Default charset is %s", cs->title);
  }
}

#ifdef STOS
static void
set_timezone(void *opaque, const char *timezone)
{
  setenv("TZ", timezone, 1);
  tzset();
  callout_update_clock_props();
}
#endif

void
i18n_init(void)
{
  prop_t *s = settings_add_dir(NULL, _p("Languages"), "i18n", NULL,
			       _p("Preferred languages"),
			       "settings:i18n");
  int i;

  nls_init(s);

#ifdef STOS
  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Timezone")),
                 SETTING_CALLBACK(set_timezone, NULL),
                 SETTING_STORE("i18n", "timezone"),
                 NULL);

#endif

  setting_create(SETTING_MULTIOPT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Time format")),
                 SETTING_STORE("i18n", "timeformat"),
                 SETTING_WRITE_INT(&gconf.time_format),
                 SETTING_OPTION(gconf.time_format_system ? "0" : NULL,
                                _p("System default")),
                 SETTING_OPTION("1", _p("24 Hour")),
                 SETTING_OPTION("2", _p("12 Hour")),
                 NULL);


  settings_create_info(s,
		       NULL,
		       _p("Language codes should be configured as three character ISO codes, example (eng, swe, fra)"));

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Primary audio language code")),
                 SETTING_CALLBACK(set_lang, &lang_audio[0]),
                 SETTING_STORE("i18n", "audio1"),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Secondary audio language code")),
                 SETTING_CALLBACK(set_lang, &lang_audio[1]),
                 SETTING_STORE("i18n", "audio2"),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Tertiary audio language code")),
                 SETTING_CALLBACK(set_lang, &lang_audio[2]),
                 SETTING_STORE("i18n", "audio3"),
                 NULL);


  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Primary subtitle language code")),
                 SETTING_CALLBACK(&set_lang, &lang_subtitle[0]),
                 SETTING_STORE("i18n", "subtitle1"),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Secondary subtitle language code")),
                 SETTING_CALLBACK(&set_lang, &lang_subtitle[1]),
                 SETTING_STORE("i18n", "subtitle2"),
                 NULL);

  setting_create(SETTING_STRING, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Tertiary subtitle language code")),
                 SETTING_CALLBACK(&set_lang, &lang_subtitle[2]),
                 SETTING_STORE("i18n", "subtitle3"),
                 NULL);

  const char **optlist = NULL;
  const charset_t *cs;
  for(i = 0; (cs = charset_get_idx(i)) != NULL; i++) {}

  optlist = alloca((i * 2 + 3) * sizeof(const char *));

  optlist[0] = "auto";
  optlist[1] = "Auto";

  for(i = 0; (cs = charset_get_idx(i)) != NULL; i++) {
    optlist[i * 2 + 2] = cs->id;
    optlist[i * 2 + 3] = cs->title;
  }
  optlist[i * 2 + 2] = NULL;

  setting_create(SETTING_MULTIOPT, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Default character set")),
                 SETTING_STORE("i18n", "default_charset"),
                 SETTING_CALLBACK(set_default_charset, NULL),
                 SETTING_OPTION_LIST(optlist),
                 NULL);

  setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Ignore 'The' at beginning of words when sorting")),
                 SETTING_STORE("i18n", "skipthe"),
                 SETTING_WRITE_BOOL(&gconf.ignore_the_prefix),
                 NULL);
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
      return 100000 * (3 - i);
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
const charset_t *
i18n_get_default_charset(void)
{
  return default_charset;
}


LIST_HEAD(nls_string_list, nls_string);


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

#define NLS_STRING_HASH_WIDTH 61

static struct nls_string_list nls_strings[NLS_STRING_HASH_WIDTH];


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

  unsigned int hash = mystrhash(key) % NLS_STRING_HASH_WIDTH;

  hts_mutex_lock(&nls_mutex);

  LIST_FOREACH(ns, &nls_strings[hash], ns_link)
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
  LIST_INSERT_HEAD(&nls_strings[hash], ns, ns_link);

  hts_mutex_unlock(&nls_mutex);
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
  hts_mutex_lock(&nls_mutex);

  for(int i = 0; i < NLS_STRING_HASH_WIDTH; i++) {
    LIST_FOREACH(ns, &nls_strings[i], ns_link) {
      ns_val_clr(ns);
      prop_set_rstring(ns->ns_prop, ns->ns_key);
    }
  }
  hts_mutex_unlock(&nls_mutex);
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
  buf_t *b = fa_load(path,
                      FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                      NULL);

  if(b == NULL) {
    TRACE(TRACE_ERROR, "NLS", "Unable to load %s -- %s", path, errbuf);
    return;
  }

  buf_make_writable(b);
  nls_load_from_data(buf_str(b));
  buf_release(b);
}


/**
 *
 */
static void
set_language(void *opaque, const char *str)
{
  char buf[200];
  char iso639_1[3];
  nls_clear();

  if(!strcmp(str, "none")) {
    TRACE(TRACE_INFO, "i18n", "Unloading language definition");
    snprintf(gconf.lang, sizeof(gconf.lang), "en");
  } else {
    snprintf(gconf.lang, sizeof(gconf.lang), "%s", str);

    snprintf(buf, sizeof(buf), "%s/lang/%s.lang", app_dataroot(), str);
    TRACE(TRACE_INFO, "i18n", "Loading language %s", str);
    nls_load_lang(buf);
  }
  memcpy(iso639_1, gconf.lang, 3);
  iso639_1[2] = 0;
  prop_setv(prop_get_global(), "i18n", "iso639_1", NULL, PROP_SET_STRING,
            iso639_1);
}


static int
nls_lang_metadata(const char *path, char *errbuf, size_t errlen,
		  char *language, size_t languagesize,
		  char *native, size_t nativesize)
{
  buf_t *b = fa_load(path,
                      FA_LOAD_ERRBUF(errbuf, errlen),
                      NULL);
  char *s;
  const char *s2;
  if(b == NULL)
    return -1;

  char *data = buf_str(b);

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
      while((uint8_t)*s2 <33 && *s2)
	s2++;
      snprintf(language, languagesize, "%s", s2);
    }

    if((s2 = mystrbegins(s, "native:")) != NULL) {
      while((uint8_t)*s2 <33 && *s2)
	s2++;
      snprintf(native, nativesize, "%s", s2);
    }
    if(*language && *native)
      break;
  }

  buf_release(b);

  if(*language && *native)
    return 0;
  
  snprintf(errbuf, errlen, "Not a valid language file");
  return -1;
}

#if ENABLE_HTTPSERVER
/**
 *
 */
static htsmsg_t *
decode_multipart(buf_t *b, const char *boundary0)
{
  char *s0 = buf_str(b);
  char *s = s0;
  int len = buf_len(b);

  int blen = strlen(boundary0);
  char *boundary = alloca(blen + 3);
  boundary[0] = '-';
  boundary[1] = '-';
  memcpy(boundary + 2, boundary0, blen + 1);

  char *boundary2 = alloca(blen + 5);
  boundary2[0] = 0xd;
  boundary2[1] = 0xa;
  boundary2[2] = '-';
  boundary2[3] = '-';
  memcpy(boundary2 + 4, boundary0, blen + 1);

  char *line = find_str(s, len, boundary);
  if(line)
    line += strlen(boundary);

  if(line == NULL) {
    free(s);
    return NULL;
  }

  htsmsg_t *parts = htsmsg_create_map();
  parts->hm_backing_store = buf_retain(b);

  while(line != NULL) {
    while(*line && *line < 32)
      line++;

    if(*line == 0)
      break;

    const char *name = NULL;
    char *next = NULL;

    while(1) {
      char *eol = strchr(line, 0xd);
      if(eol == NULL) {
        name = NULL;
        break;
      }
      if(eol[1] != 0xa) {
        name = NULL;
        break;
      }
      *eol = 0;
      next = eol + 2;

      if(*line == 0)
        break;

      const char *v;
      if((v = mystrbegins(line, "Content-Disposition:")) != NULL) {
        name = mystrstr(v, "name=\"");
        if(name != NULL) {
          name += strlen("name=\"");
          char *eon = strchr(name, '"');
          if(eon != NULL) {
            *eon = 0;
          } else {
            name = NULL;
          }
        }
      }
      line = next;
    }

    char *data = next;
    if(next == NULL)
      break;
    next = find_str(data, len - (data - s0), boundary2);
    if(next == NULL)
      break;

    int datasize = next - data;
    *next = 0;
    next += strlen(boundary2);

    if(name != NULL)
      htsmsg_add_binptr(parts, name, data, datasize);

    line = next;
  }
  return parts;
}


/**
 *
 */
static int
upload_translation(http_connection_t *hc, const char *remain, void *opaque,
		   http_cmd_t method)
{
  size_t len;
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  htsbuf_qprintf(&out,"<html><body>");

  if(method == HTTP_CMD_POST) {
    void *data = http_get_post_data(hc, &len, 1);
    if(data == NULL)
      return 405;

    const char *ct = http_arg_get_hdr(hc, "Content-Type");
    if(ct == NULL) {
      free(data);
      return 400;
    }

    if(!strncasecmp(ct, "multipart/form-data", strlen("multipart/form-data"))) {
      const char *b = mystrstr(ct, "boundary=");
      if(b == NULL) {
        free(data);
        return 400;
      }
      b += strlen("boundary=");

      buf_t *buf = buf_create_from_malloced(len, data);
      htsmsg_t *m = decode_multipart(buf, b);
      buf_release(buf);

      if(m == NULL)
        return 400;

      const void *bin;
      size_t binlen;
      if(!htsmsg_get_bin(m, "file", &bin, &binlen)) {
        nls_load_from_data((void *)bin);
        htsbuf_qprintf(&out, "<p>Language updated</p>");
      }

      htsmsg_release(m);

    } else {
      nls_clear();
      nls_load_from_data(data);
      free(data);
      TRACE(TRACE_INFO, "i18n", "Loading language from raw HTTP POST");
      return 200;
    }
  }

  htsbuf_qprintf(&out,
                 "<h3>Load new translation file into "APPNAMEUSER"</h3>"
                 "<form method=\"post\" enctype=\"multipart/form-data\">"
                 "<label for=\"file\">.lang file:</label>"
                 "<input type=\"file\" name=\"file\" id=\"file\"><br>"
                 "<input type=\"submit\" name=\"submit\" value=\"Submit\">"
                 "</form></body></html>");

  return http_send_reply(hc, 0, "text/html", NULL, NULL, 0, &out);
}
#endif // ENABLE_HTTPSERVER


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
nls_init(prop_t *parent)
{
  char buf[200];
  char buf2[200];
  snprintf(buf2, sizeof(buf2), "%s/lang", app_dataroot());
  fa_dir_t *fd = fa_scandir(buf2, buf, sizeof(buf));
  fa_dir_entry_t *fde;
  char language[64];
  char native[64];
  char *e;
  LIST_HEAD(, lang) list;
  lang_t *l;

#if ENABLE_HTTPSERVER
  http_path_add("/api/translation", NULL, upload_translation, 1);
#endif

  if(fd == NULL) {
    TRACE(TRACE_ERROR, "i18n", "Unable to scan languages in %s/lang -- %s",
	  app_dataroot(), buf);
    return;
  }

  const char **optlist = NULL;
  int num = 0;

  LIST_INIT(&list);

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
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
    LIST_INSERT_SORTED(&list, l, link, langcmp, lang_t);
    num++;
  }

  num++;
  optlist = alloca((num * 2 + 1) * sizeof(const char *));

  int idx = 0;
  optlist[idx++] = "none";
  optlist[idx++] = "English (default)";

  LIST_FOREACH(l, &list, link) {
    optlist[idx++] = l->id;
    optlist[idx++] = l->str;
  }

  optlist[idx] = NULL;


  setting_create(SETTING_MULTIOPT, parent, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Language")),
                 SETTING_STORE("i18n", "language"),
                 SETTING_CALLBACK(set_language, NULL),
                 SETTING_OPTION_LIST(optlist),
                 NULL);

  fa_dir_free(fd);
}
