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

glw_t *layout_world;

static int fullscreen;

float fullscreen_fader;

extern float layout_switcher_alpha;



static int layout_world_input_event(glw_event_t *ge);

glw_t *layout_global_status;

/**
 *
 */
static int
layout_world_status_fader(glw_t *w, void *opaque, glw_signal_t signal,
			  void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    w->glw_weight = 1 - fullscreen_fader;
    break;
  }
  return 0;
}


/**
 * Create world
 */
void
layout_world_create(void)
{
  glw_t *w;

  layout_world = glw_model_create("theme://universe.model", NULL, 0,
				  prop_global, NULL);

  layout_global_status = glw_find_by_id(layout_world,
					"global_status_container", 0);

  if((w = glw_find_by_id(layout_world, "global_status_place", 0)) != NULL) {
    glw_set(w,
	    GLW_ATTRIB_SIGNAL_HANDLER, layout_world_status_fader, NULL, 30,
	    NULL);
  }
  
  event_handler_register(0, layout_world_input_event);
}


/**
 * Render the world model
 */
void
layout_world_render(float aspect)
{
  glw_rctx_t rc;
  
  fullscreen_fader = GLW_LP(16, fullscreen_fader, fullscreen);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_focused = 1;
  rc.rc_fullscreen = fullscreen_fader;
  glw_layout(layout_world, &rc);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  gluLookAt(0, 0, 3.4,
	    0, 0, 1,
	    0, 1, 0);

  rc.rc_alpha = 1.0f - (0.9 * layout_switcher_alpha);
  glw_render(layout_world, &rc);
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
layout_world_appi_show(appi_t *ai)
{
  glw_t *p;

  p = glw_find_by_id(layout_world, "application_instance_container", 0);
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
layout_world_input_event(glw_event_t *ge)
{
  glw_signal(layout_world, GLW_SIGNAL_EVENT, ge);
  return 1;
}

