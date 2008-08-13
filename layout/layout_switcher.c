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

static glw_t *layout_switcher;
static glw_t *layout_switcher_list;

static int layout_switcher_hold_time;
float layout_switcher_alpha;

static int layout_switcher_input_event(glw_event_t *ge);

void
layout_switcher_create(void)
{
  event_handler_register(201, layout_switcher_input_event);

  layout_switcher = glw_model_create("theme://switcher/switcher.model", NULL,
				     0, NULL);

  layout_switcher_list = 
    glw_find_by_id(layout_switcher, "switcher_container", 0);

  if(layout_switcher_list == NULL) {
    fprintf(stderr, "Warning: 'switcher_container' not found. "
	    "This model will not be able to switch between applications\n");
    return;
  }
}


/**
 * Callback for switching to another app instance
 */
static int
switcher_spawn_callback(glw_t *w, void *opaque, glw_signal_t signal,
			void *extra)
{
  return 0;
}


/**
 * New application instance, link it in
 */
void
layout_switcher_appi_add(appi_t *ai, glw_t *w)
{
  if(layout_switcher_list == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_PARENT, layout_switcher_list,
	  GLW_ATTRIB_SIGNAL_HANDLER, switcher_spawn_callback, ai, 100,
	  NULL);
}

/**
 * Let the current selected application be the first one in the app list
 */
static void
layout_switcher_set_current_to_front(void)
{
  glw_move_selected_to_front(layout_switcher_list);
}


/**
 *
 */
void
layout_switcher_render(float aspect)
{
  glw_rctx_t rc0;

  if(layout_switcher_hold_time > 0) {
    layout_switcher_hold_time--;
    if(layout_switcher_hold_time == 0)
      layout_switcher_set_current_to_front();
  }

  layout_switcher_alpha = GLW_LP(8, layout_switcher_alpha,
				 layout_switcher_hold_time > 0 ? 1.0 : 0.0);

  memset(&rc0, 0, sizeof(rc0));

  rc0.rc_zoom  = 1.0f;
  rc0.rc_alpha = layout_switcher_alpha;
  rc0.rc_aspect = aspect;
  rc0.rc_focused = 1;

  glw_layout(layout_switcher, &rc0);

  if(layout_switcher_alpha < 0.01)
    return;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0.0f, 1.0f, 4.3f,
	    0.0f, 1.0f, 1.0f,
	    0.0f, 1.0f, 0.0f);

  glPushMatrix();
  glTranslatef(0, 1, 3.0 - 1 * layout_switcher_alpha);
  glw_render(layout_switcher, &rc0);
  glPopMatrix();
}

/**
 * Display currently selected app
 */
static void
layout_switcher_switch(void)
{
  appi_t *ai;
  glw_t *w = layout_switcher_list->glw_selected;

  if(w == NULL)
    return;

  ai = glw_get_opaque(w, switcher_spawn_callback);
  layout_world_appi_show(ai);
}



/**
 * Primary point for input event distribution
 */
static int
layout_switcher_input_event(glw_event_t *ge)
{
  if(layout_switcher_list == NULL)
    return 0;

  if(ge->ge_type == EVENT_KEY_TASK_DOSWITCH) {
    ge->ge_type = GEV_INCR;
    glw_signal(layout_switcher_list, GLW_SIGNAL_EVENT, ge);
    layout_switcher_hold_time = 1000000 / frame_duration;
    layout_switcher_switch();
    return 1;
  }

  if(layout_switcher_hold_time == 0) {
    if(ge->ge_type == EVENT_KEY_TASK_SWITCHER) {
      layout_switcher_hold_time = 10 * 1000000 / frame_duration;
      return 1;
    }
    return 0;
  }

  /* task switcher is visible */

  switch(ge->ge_type) {
  case GEV_ENTER:
    layout_switcher_hold_time = 0;
    layout_switcher_switch();
    layout_switcher_set_current_to_front();
    return 1;

  case EVENT_KEY_TASK_SWITCHER:
    layout_switcher_hold_time = 0;
    return 1;
    
  default:
    layout_switcher_hold_time = 10 * 1000000 / frame_duration;
    glw_signal(layout_switcher_list, GLW_SIGNAL_EVENT, ge);
    layout_switcher_switch();
  }
  return 1;
}

