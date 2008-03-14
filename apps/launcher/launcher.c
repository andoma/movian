/*
 *  Application launcher
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
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "launcher.h"
#include <layout/layout.h>

static appi_t *launcher_appi;
static glw_t *launcher_list;
static int launcher_input_event(inputevent_t *ie);

void
launcher_init(void)
{
  glw_t *list;
  appi_t *ai;
  glw_t *mini;

  launcher_appi = ai = appi_create("Launcher");

  ai->ai_widget = glw_create(GLW_MODEL,
			     GLW_ATTRIB_FILENAME, "launcher",
			     NULL);
  mini = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "launcher_miniature",
	       NULL);

  list = glw_find_by_id(ai->ai_widget, "application_container", 0);
  if(list == NULL) {
    fprintf(stderr, "Warning: 'application_container' not found. "
	    "This model will not be able to start new applications\n");
    return;
  }

  launcher_list = list;
  inputhandler_register(199, launcher_input_event);
  glw_focus_set(&ai->ai_gfs, list);

  layout_world_appi_show(ai);
  layout_switcher_appi_add(ai, mini);
}


/**
 * Toggle display of application launcher when we see INPUT_KEY_APP_LAUNCHER
 */
static int
launcher_input_event(inputevent_t *ie)
{
  appi_t *ai = launcher_appi;

  if(ie->type != INPUT_KEY)
    return 0;

  if(!appi_focused(ai) && ie->u.key == INPUT_KEY_APP_LAUNCHER) {
    layout_world_appi_show(launcher_appi);
    return 1;
  }
  return 0;
}

/**
 * Callback for launching a new app when user press ENTER
 */
static int
launcher_spawn_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  app_t *a = opaque;
  switch(signal) {
  case GLW_SIGNAL_ENTER:
    a->app_spawn(NULL);
    return 1;
  default:
    break;
  }
  return 0;
}

/**
 * Add a new app
 */
void
launcher_app_add(app_t *a)
{
  if(launcher_list == NULL)
    return;

  glw_create(GLW_MODEL,
	     GLW_ATTRIB_PARENT, launcher_list,
	     GLW_ATTRIB_SIGNAL_HANDLER, launcher_spawn_callback, a, 400,
	     GLW_ATTRIB_FILENAME, a->app_model,
	     NULL);
}
