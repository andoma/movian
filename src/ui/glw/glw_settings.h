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
#pragma once
#include "config.h"

typedef struct glw_settings {
  int gs_size;
  int gs_underscan_h;
  int gs_underscan_v;
  int gs_wrap;
  int gs_map_mouse_wheel_to_keys;

  int gs_screensaver_delay;
  int gs_bing_image;

  struct setting *gs_setting_size;
  struct setting *gs_setting_underscan_v;
  struct setting *gs_setting_underscan_h;
  struct setting *gs_setting_wrap;
  struct setting *gs_setting_wheel_mapping;

  struct setting *gs_setting_custom_bg;

  struct setting *gs_setting_screensaver_timer;
  struct setting *gs_setting_bing_image;
  struct setting *gs_setting_user_images;

  struct prop *gs_settings;
  struct htsmsg *gs_settings_store;

} glw_settings_t;

extern glw_settings_t glw_settings;

#if ENABLE_GLW_SETTINGS

void glw_settings_adj_size(int delta);

void glw_settings_init(void);

void glw_settings_fini(void);

#else

#define glw_settings_adj_size(delta)

#define glw_settings_init()

#define glw_settings_fini()

#endif
