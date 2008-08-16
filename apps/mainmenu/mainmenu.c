/*
 *  Showtime mainmenu
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
#include "mainmenu.h"
#include "event.h"
#include <layout/layout.h>

glw_t *mainmenumodel;

static int mainmenu_input_event(glw_event_t *ge, void *opaque);

void
mainmenu_init(void)
{
  appi_t *ai = appi_create("Mainmenu");

  event_handler_register("mainmenuswitcher", mainmenu_input_event, 
			 EVENTPRI_MAINMENU, ai);

  mainmenumodel = glw_model_create("theme://mainmenu/mainmenu.model", NULL,
				   0, prop_global, NULL);

  ai->ai_widget = mainmenumodel;

  layout_world_appi_show(ai);
}





/**
 * Callback for switching to another app instance
 */
static int
mainmenu_appi_callback(glw_t *w, void *opaque, glw_signal_t signal,
			void *extra)
{
  glw_event_t *ge = extra;

  if(signal != GLW_SIGNAL_EVENT)
    return 0;

  if(ge->ge_type == GEV_ENTER) {
    layout_world_appi_show(opaque);
    return 1;
  }

  return 0;
}

/**
 *
 */
void
mainmenu_appi_add(appi_t *ai, glw_t *miniature, int primary)
{
  glw_t *w = NULL;

  if(!primary)
    w = glw_find_by_id(mainmenumodel, "secondary_apps", 0);

  if(w == NULL)
    w = glw_find_by_id(mainmenumodel, "primary_apps", 0);

  if(w == NULL)
    return;
  
  glw_set(miniature,
	  GLW_ATTRIB_PARENT, w,
	  GLW_ATTRIB_SIGNAL_HANDLER, mainmenu_appi_callback, ai, 100,
	  NULL);
}
/**
 *
 */
static int
mainmenu_input_event(glw_event_t *ge, void *opaque)
{
  if(ge->ge_type == EVENT_KEY_MAINMENU) {
    layout_world_appi_show(opaque);
    return 1;
  }
  return 0;
}
