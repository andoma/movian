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

#include "showtime.h"
#include "layout.h"
#include "event.h"
#include "audio/audio_ui.h"
#include "navigator.h"

glw_t *universe;

static int fullscreen;
static float fullscreen_fader;
prop_t *prop_fullscreen;

static int layout_input_event(glw_event_t *ge, void *opaque);
int layout_event_handler(glw_t *w, void *opaque, glw_signal_t sig, 
			 void *extra);

/**
 *
 */
void
layout_create(void)
{
  prop_fullscreen = prop_create(prop_get_global(), "fullscreen");

  universe = glw_model_create("theme://universe.model", NULL, 0, NULL);

  event_handler_register("universe", layout_input_event, EVENTPRI_UNIVERSE,
			 NULL);

  glw_set(universe,
	  GLW_ATTRIB_SIGNAL_HANDLER, layout_event_handler, NULL, 1000,
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
  prop_set_float(prop_fullscreen, fullscreen_fader);

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
 * Primary point for input event distribution in the UI
 */
static int
layout_input_event(glw_event_t *ge, void *opaque)
{
  return glw_signal(universe, GLW_SIGNAL_EVENT, ge);
}


/**
 *
 */
int
layout_event_handler(glw_t *w, void *opaque, glw_signal_t sig, void *extra)
{
  glw_event_t *ge = extra;

  if(sig != GLW_SIGNAL_EVENT_BUBBLE)
    return 0;

  event_post(ge);
  return 1;
}
