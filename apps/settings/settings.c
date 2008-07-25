/*
 *  Global settings
 *  Copyright (C) 2008 Andreas Öman
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

#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "settings.h"
#include "hid/keymapper.h"
#include "layout/layout.h"
#include "libhts/hts_strtab.h"

#include "audio/audio.h"
#include "display/display.h"

/**
 *
 */
void
settings_init(void)
{
  appi_t *ai;
  glw_t *mini;
  glw_t *l;

  ai = appi_create("Settings");

  ai->ai_widget = glw_model_create("theme://settings/settings-app.model", NULL);

  mini = glw_model_create("theme://settings/settings-miniature.model", NULL);
  layout_switcher_appi_add(ai, mini);

  display_settings_init(ai, ai->ai_widget);
  audio_settings_init(ai, ai->ai_widget);
  keymapper_init(ai, ai->ai_widget);


  /* Make sure the settings top list is selected and nothing else */

  if((l = glw_find_by_id(ai->ai_widget, "settings_list", 0)) != NULL) {
    l = TAILQ_FIRST(&l->glw_childs);
    if(l != NULL)
      glw_select(l);
  }
}
