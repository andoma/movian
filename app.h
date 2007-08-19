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

#include "hid/input.h"
#include "media.h"

TAILQ_HEAD(app_queue, app);
TAILQ_HEAD(appi_queue, appi);

extern struct app_queue apps;
extern struct appi_queue appis;


#define appi_menu_top(ai) ((ai)->ai_menu)

#define appi_menu_display(ai) ((ai)->ai_menu_display = 1)

typedef struct appi {
  LIST_ENTRY(appi) ai_app_link;
  TAILQ_ENTRY(appi) ai_global_link;

  ic_t ai_ic;
  media_pipe_t ai_mp;
  
  AVFormatContext *ai_fctx;

  enum {
    AI_FS_NONE,
    AI_FS_WITH_BG,
    AI_FS_BLANK,
  } ai_req_fullscreen;

  int ai_got_fullscreen;
  
  glw_t *ai_widget;             /* Created by spawn() function */

  glw_t *ai_menu;               /* Top level menu */
  int ai_menu_display; 

  pthread_t ai_tid;

  struct app *ai_app;

  float ai_req_aspect;         /* requested aspect ratio, for
				  auto layouting */
  int ai_layouted;
  
  int ai_no_input_events;      /* This instance does not react
				  to input events at all */

  int ai_visible;

  /* These are stupid */

  struct play_list *ai_play_list;
  struct browser *ai_browser;

} appi_t;



typedef struct app {
  const char *app_name;
  const char *app_icon;
  
  void (*app_spawn)(appi_t *ai);

  glw_callback_t *app_win_callback;

  float app_def_aspect;

  TAILQ_ENTRY(app) app_link;
  
  LIST_HEAD(, appi) app_instances;

  glw_t *app_widget;                /* Created by layouter */
  
  int app_max_instances;

  int app_cur_instances;

  int app_auto_spawn;               /* Spawn upon startup */

} app_t;

void app_register(app_t *a);

appi_t *appi_spawn(app_t *a, int visible);

appi_t *appi_find(app_t *a, int visible, int create);

#define APP_REGISTER(name) do {			\
  extern app_t name;				\
  app_register(&name);				\
} while(0)

void appi_hide(appi_t *ai);

appi_t *appi_spawn2(app_t *a, glw_t *p);

int appi_widget_post_key(glw_t *w, void *opaque, glw_signal_t signal, ...);

#endif /* APP_H */
