/*
 *  Layout engine
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

#include <string.h>
#include <math.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "event.h"
#include "audio/audio_ui.h"

glw_t *universe;
glw_t *layout_global_status;

static int fullscreen;
static float fullscreen_fader;
glw_prop_t *prop_fullscreen;

static int layout_input_event(glw_event_t *ge, void *opaque);

/**
 *
 */
void
layout_create(void)
{
  prop_fullscreen = glw_prop_create(prop_global, "fullscreen", GLW_GP_FLOAT);

  universe = glw_model_create("theme://universe.model", NULL, 0,
			      prop_global, NULL);

  layout_global_status = glw_find_by_id(universe,
					"global_status_container", 0);

  event_handler_register("universe", layout_input_event, EVENTPRI_UNIVERSE,
			 NULL);
}



/**
 * Master scene rendering
 */
void 
layout_draw(float aspect)
{
  glw_rctx_t rc;

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
  
  fullscreen_fader = GLW_LP(16, fullscreen_fader, fullscreen);
  glw_prop_set_float(prop_fullscreen, fullscreen_fader);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_focused = 1;
  rc.rc_fullscreen = fullscreen_fader;
  glw_layout(universe, &rc);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt(0, 0, 3.4,
	    0, 0, 1,
	    0, 1, 0);

  rc.rc_alpha = 1.0f;
  glw_render(universe, &rc);
}




/**
 *
 */
static int
layout_child_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  appi_t *ai = opaque;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    if(w == w->glw_parent->glw_selected) {
      ai->ai_active = 1;
      fullscreen = ai->ai_req_fullscreen;
    } else {
      ai->ai_active = 0;
    }
    break;

  default:
    break;
  }
  return 0;
}


/**
 * Show application instance (and optionally add it)
 */
void
layout_appi_show(appi_t *ai)
{
  glw_t *p;

  p = glw_find_by_id(universe, "application_instance_container", 0);
  if(p == NULL)
    return;

  glw_set(ai->ai_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, layout_child_callback, ai, 1,
	  GLW_ATTRIB_PARENT, p,
	  NULL);

  glw_select(ai->ai_widget);
}

/**
 * Primary point for input event distribution
 */
static int
layout_input_event(glw_event_t *ge, void *opaque)
{
  glw_signal(universe, GLW_SIGNAL_EVENT, ge);
  return 1;
}

