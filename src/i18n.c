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

struct {
  const char *id, *title;
  const uint16_t *ptr;
} charsets[] = {
  {"ISO-8859-1", "ISO-8859-1 (Latin-1)", NULL},
  {"ISO-8859-2", "ISO-8859-2 (Latin-2)", ISO_8859_2},
  {"ISO-8859-3", "ISO-8859-3 (Latin-3)", ISO_8859_3},
  {"ISO-8859-4", "ISO-8859-4 (Latin-4)", ISO_8859_4},
  {"ISO-8859-5", "ISO-8859-5 (Latin/Cyrillic)", ISO_8859_5},
  {"ISO-8859-6", "ISO-8859-6 (Latin/Arabic)", ISO_8859_6},
  {"ISO-8859-7", "ISO-8859-7 (Latin/Greek)", ISO_8859_7},
  {"ISO-8859-8", "ISO-8859-8 (Latin/Hebrew)", ISO_8859_8},
  {"ISO-8859-9", "ISO-8859-9 (Turkish)", ISO_8859_9},
  {"ISO-8859-10", "ISO-8859-10 (Latin-5)", ISO_8859_10},
  {"ISO-8859-11", "ISO-8859-11 (Latin/Thai)", ISO_8859_11},
  {"ISO-8859-13", "ISO-8859-13 (Baltic Rim)", ISO_8859_13},
  {"ISO-8859-14", "ISO-8859-14 (Celtic)", ISO_8859_14},
  {"ISO-8859-15", "ISO-8859-15 (Latin-9)", ISO_8859_15},
  {"ISO-8859-16", "ISO-8859-16 (Latin-10)", ISO_8859_16},
};



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
  int i;
  if(str == NULL)
    str = "ISO-8859-1";

  for(i = 0; i < sizeof(charsets) / sizeof(charsets[0]); i++) {
    if(!strcmp(str, charsets[i].id)) {
      srt_charset = charsets[i].ptr;
      TRACE(TRACE_DEBUG, "i18n", "SRT charset is %s", charsets[i].title);
    }
  }
}



void
i18n_init(void)
{
  prop_t *s = settings_add_dir(NULL, "Languages", NULL, NULL);
  setting_t *x;
  int i;

  htsmsg_t *store = htsmsg_store_load("i18n");
  if(store == NULL)
    store = htsmsg_create_map();

  settings_create_info(s, 
		       NULL,
		       "Language codes should be configured as "
		       "three character ISO codes, example (eng, swe, fra)");

  settings_create_string(s, "audio1", "Primary audio language code", NULL, 
			 store, set_lang, &lang_audio[0],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "audio2", "Secondary audio language code", NULL, 
			 store, set_lang, &lang_audio[1],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "audio3", "Tertiary audio language code", NULL, 
			 store, set_lang, &lang_audio[2],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");
  

  settings_create_string(s, "subtitle1", "Primary subtitle language code",
			 NULL, store, set_lang, &lang_subtitle[0],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "subtitle2", "Secondary subtitle language code",
			 NULL, store, set_lang, &lang_subtitle[1],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  settings_create_string(s, "subtitle3", "Tertiary subtitle language code",
			 NULL, store, set_lang, &lang_subtitle[2],
			 SETTINGS_INITIAL_UPDATE,  NULL,
			 settings_generic_save_settings, 
			 (void *)"i18n");

  x = settings_create_multiopt(s, "srt_charset", "SRT character set",
			       set_srt_charset, NULL);

  for(i = 0; i < sizeof(charsets) / sizeof(charsets[0]); i++) {
    settings_multiopt_add_opt(x, charsets[i].id,
			      charsets[i].title, i == 0);
  }

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


/**
 *
 */
const char *
i18n_get_charset_name(const void *p)
{
  int i;
  for(i = 0; i < sizeof(charsets) / sizeof(charsets[0]); i++)
    if(p == charsets[i].ptr)
      return charsets[i].title;
  return "???";
}
