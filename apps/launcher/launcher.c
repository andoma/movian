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
#if 0
#include <sys/stat.h>
#include <sys/time.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "launcher.h"
#include <layout/layout.h>

static appi_t *launcher_appi;
static glw_t *launcher_list;

void
launcher_init(void)
{
  glw_t *list;
  appi_t *ai;

  launcher_appi = ai = appi_create("Launcher");

  ai->ai_widget = glw_model_create("theme://launcher/launcher.model",
				   NULL, 0, NULL);
  ai->ai_miniature = 
    glw_model_create("theme://launcher/launcher_miniature.model",
		     NULL, 0, NULL);

  list = glw_find_by_id(ai->ai_widget, "application_container", 0);
  if(list == NULL) {
    fprintf(stderr, "Warning: 'application_container' not found. "
	    "This model will not be able to start new applications\n");
  }

  launcher_list = list;
  mainmenu_appi_add(ai, 0);
}

/**
 * Callback for launching a new app when user press ENTER
 */
static int
launcher_spawn_callback(glw_t *w, void *opaque, glw_signal_t sig, void *extra)
{
  app_t *a = opaque;
  glw_event_t *ge = extra;

  if(sig != GLW_SIGNAL_EVENT)
    return 0;

  switch(ge->ge_type) {
  case GEV_ENTER:
    app_spawn(a, NULL, 0);
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
  glw_t *w;
  if(launcher_list == NULL)
    return;

  w = glw_model_create(a->app_model, launcher_list, 0, NULL);
  if(w == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_SIGNAL_HANDLER, launcher_spawn_callback, a, 400,
	  NULL);
}
#endif
