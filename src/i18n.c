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

static char *lang_audio[3];
static char *lang_subtitle[3];

static void
set_lang(void *opaque, const char *str)
{
  char **s = opaque;
  mystrset(s, str);
}

void
i18n_init(void)
{
  prop_t *s = settings_add_dir(NULL, "Languages", NULL, NULL);

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
