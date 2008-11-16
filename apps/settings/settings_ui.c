/*
 *  User interface settings
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>


#include <libglw/glw.h>

#include "showtime.h"
#include "settings.h"

uint32_t userinterface_scale = 100;
extern glw_prop_t *prop_ui_scale;

/**
 *
 */
static void
ui_settings_save(void)
{
  htsmsg_t *m = htsmsg_create();

  htsmsg_add_u32(m, "scale", userinterface_scale);
  hts_settings_save(m, "userinterface");
  htsmsg_destroy(m);
  
}

/**
 *
 */
static int
ui_scale_change_cb(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  if(signal != GLW_SIGNAL_CHANGED)
    return 0;
  glw_prop_set_float(prop_ui_scale, (float)userinterface_scale / 100.0);
  ui_settings_save();
  return 0;
}


/**
 *
 */
void
settings_userinterface_init(glw_t *m)
{
  glw_t *icon, *tab, *w;
  htsmsg_t *settings;

  icon = glw_model_create("theme://settings/ui/ui-icon.model", NULL,
			  0, prop_global, NULL);
  tab = glw_model_create("theme://settings/ui/ui.model", NULL,
			 0, prop_global, NULL);


  if((settings = hts_settings_load("userinterface")) != NULL) {
    htsmsg_get_u32(settings, "scale", &userinterface_scale);
    htsmsg_destroy(settings);
  }




  glw_prop_set_float(prop_ui_scale, (float)userinterface_scale / 100.0);

  if((w = glw_find_by_id(tab, "uiscale", 0)) != NULL)
    glw_set(w, 
	    GLW_ATTRIB_INTPTR, &userinterface_scale,
	    GLW_ATTRIB_SIGNAL_HANDLER, ui_scale_change_cb, NULL, 10,
	    NULL);

  glw_add_tab(m, "settings_list", icon, "settings_deck", tab);
}
