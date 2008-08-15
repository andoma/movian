/*
 *  Application handing
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef APP_H
#define APP_H

#include <libhts/htsmsg.h>
#include "media.h"

/**
 * Application 
 */
typedef struct app {
  LIST_ENTRY(app) app_link;

  const char *app_name;
  const char *app_model;

  glw_t *app_icon_widget;

  void (*app_spawn)(struct appi *ai);

} app_t;

#define appi_menu_top(ai) ((ai)->ai_menu)


/**
 * Application instance
 */
typedef struct appi {
  glw_t *ai_widget;

  LIST_ENTRY(appi) ai_link;

  int ai_active;

  app_t *ai_app;

  glw_event_queue_t ai_geq;
  media_pipe_t *ai_mp;
  
  AVFormatContext *ai_fctx;

  int ai_instance_index;
  htsmsg_t *ai_settings;

  int ai_req_fullscreen;

  glw_t *ai_menu;               /* Top level menu */

  const char *ai_name;

  char ai_speedbutton[32];

  glw_prop_t *ai_prop_root;
  glw_prop_t *ai_prop_title;

} appi_t;


#define appi_focused(ai) ((ai)->ai_gfs.gfs_active)

void apps_load(void);

void app_spawn(app_t *app, htsmsg_t *settings, int index);

void appi_destroy(appi_t *ai);

htsmsg_t *appi_settings_create(appi_t *ai);

void appi_settings_save(appi_t *ai, htsmsg_t *m);

appi_t *appi_create(const char *name);

void app_settings(appi_t *ai, glw_t *parent, 
		  const char *name, const char *miniature);

void app_load_generic_config(appi_t *ai, const char *name);

void app_save_generic_config(appi_t *ai, const char *name);

void appi_speedbutton_mapper(glw_t *w, const char *name, appi_t *ai);

#endif /* APP_H */
