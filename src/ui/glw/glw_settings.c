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
#include "main.h"
#include "prop/prop_concat.h"
#include "settings.h"
#include "glw.h"
#include "glw_settings.h"
#include "htsmsg/htsmsg_store.h"
#include "db/kvstore.h"

/**
 *
 */
void
glw_settings_adj_size(int delta)
{
  if(delta == 0)
    setting_set(glw_settings.gs_setting_size, SETTING_INT, 0);
  else
    settings_add_int(glw_settings.gs_setting_size, delta);
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

  prop_t *s = glw_settings.gs_settings;
  htsmsg_t *store = glw_settings.gs_settings_store;

  glw_settings.gs_setting_size =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Font and icon size")),
                   SETTING_RANGE(-10, 30),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_size),
                   SETTING_HTSMSG("size", store, "glw"),
                   NULL);

  glw_settings.gs_setting_underscan_h =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface horizontal shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_h),
                   SETTING_HTSMSG("underscan_h", store, "glw"),
                   NULL);

  glw_settings.gs_setting_underscan_v =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Interface vertical shrink")),
                   SETTING_RANGE(-100, 100),
                   SETTING_UNIT_CSTR("px"),
                   SETTING_WRITE_INT(&glw_settings.gs_underscan_v),
                   SETTING_HTSMSG("underscan_v", store, "glw"),
                   NULL);

  glw_settings.gs_setting_wrap =
    setting_create(SETTING_BOOL, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Wrap when reaching beginning/end of lists")),
                   SETTING_VALUE(1),
                   SETTING_WRITE_BOOL(&glw_settings.gs_wrap),
                   SETTING_HTSMSG("wrap", store, "glw"),
                   NULL);


  settings_create_separator(s, _p("Screensaver"));

  glw_settings.gs_setting_screensaver =
    setting_create(SETTING_INT, s, SETTINGS_INITIAL_UPDATE,
                   SETTING_TITLE(_p("Screensaver delay")),
                   SETTING_VALUE(10),
                   SETTING_RANGE(0, 60),
		   SETTING_ZERO_TEXT(_p("Off")),
                   SETTING_UNIT_CSTR("min"),
                   SETTING_WRITE_INT(&glw_settings.gs_screensaver_delay),
                   SETTING_HTSMSG("screensaver", store, "glw"),
                   NULL);

  prop_t *p = prop_create(prop_get_global(), "glw");
  p = prop_create(p, "osk");
  kv_prop_bind_create(p, "showtime:glw:osk");
}


/**
 *
 */
void
glw_settings_fini(void)
{
  setting_destroy(glw_settings.gs_setting_screensaver);
  setting_destroy(glw_settings.gs_setting_underscan_v);
  setting_destroy(glw_settings.gs_setting_underscan_h);
  setting_destroy(glw_settings.gs_setting_size);
  setting_destroy(glw_settings.gs_setting_wrap);
  prop_destroy(glw_settings.gs_settings);
  htsmsg_release(glw_settings.gs_settings_store);
}
