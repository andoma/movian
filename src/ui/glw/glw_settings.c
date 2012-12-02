/*
 *  GL Widgets
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
#include "prop/prop_concat.h"
#include "settings.h"
#include "glw.h"
#include "glw_settings.h"

glw_settings_t glw_settings;

/**
 *
 */
void
glw_settings_adj_size(int delta)
{
  settings_add_int(glw_settings.gs_setting_size, delta); 
}


/**
 *
 */
void
glw_settings_save(void *opaque, htsmsg_t *msg)
{
  htsmsg_store_save(msg, "glw");
}


/**
 *
 */
void
glw_settings_init(void)
{
  glw_settings.gs_settings_store = htsmsg_store_load("glw");
  
  if(glw_settings.gs_settings_store == NULL)
    glw_settings.gs_settings_store = htsmsg_create_map();

  
  glw_settings.gs_settings = prop_create_root(NULL);
  prop_concat_add_source(gconf.settings_look_and_feel,
			 prop_create(glw_settings.gs_settings, "nodes"),
			 NULL);

  glw_settings.gs_setting_size =
    settings_create_int(glw_settings.gs_settings, "size",
			_p("Userinterface size"), 0,
			glw_settings.gs_settings_store, -10, 30, 1,
			settings_generic_set_int, &glw_settings.gs_size,
			SETTINGS_INITIAL_UPDATE, "px", NULL,
			glw_settings_save, NULL);

  glw_settings.gs_setting_underscan_h =
    settings_create_int(glw_settings.gs_settings, "underscan_h",
			_p("Horizontal underscan"), 0,
			glw_settings.gs_settings_store, -100, +100, 1,
			settings_generic_set_int, &glw_settings.gs_underscan_h,
			SETTINGS_INITIAL_UPDATE, "px", NULL,
			glw_settings_save, NULL);

  glw_settings.gs_setting_underscan_v =
    settings_create_int(glw_settings.gs_settings, "underscan_v",
			_p("Vertical underscan"), 0,
			glw_settings.gs_settings_store, -100, +100, 1,
			settings_generic_set_int, &glw_settings.gs_underscan_v,
			SETTINGS_INITIAL_UPDATE, "px", NULL,
			glw_settings_save, NULL);


  glw_settings.gs_setting_screensaver =
    settings_create_int(glw_settings.gs_settings, "screensaver",
			_p("Screensaver delay"),
			10, glw_settings.gs_settings_store, 1, 60, 1,
			settings_generic_set_int,
			&glw_settings.gs_screensaver_delay,
			SETTINGS_INITIAL_UPDATE, " min", NULL,
			glw_settings_save, NULL);

}

void
glw_settings_fini(void)
{
  setting_destroy(glw_settings.gs_setting_screensaver);
  setting_destroy(glw_settings.gs_setting_underscan_v);
  setting_destroy(glw_settings.gs_setting_underscan_h);
  setting_destroy(glw_settings.gs_setting_size);
  prop_destroy(glw_settings.gs_settings);
  htsmsg_destroy(glw_settings.gs_settings_store);
}
