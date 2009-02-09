/*
 *  GL Widgets, layer, transition between childs objects
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

#include "glw.h"
#include "glw_layer.h"
#include "glw_transitions.h"

/**
 *
 */
static void
switchfocus(glw_t *prev, glw_t *n)
{
  if(prev != NULL && prev->glw_focus_mode == GLW_FOCUS_LEADER) {
    prev->glw_flags |= GLW_FOCUS_DISABLED;
  }
  if(n != NULL && n->glw_focus_mode == GLW_FOCUS_LEADER) {
    n->glw_flags &= ~GLW_FOCUS_DISABLED;
    glw_focus_set(n);
  }
}


/**
 *
 */
static int
glw_layer_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t rc0, *rc = extra;
  glw_t *c;
  float z;
  float a;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    z = 0;
    a = 1.0;
    TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
      c->glw_parent_pos.x = 0;
      c->glw_parent_pos.y = 0;
      c->glw_parent_pos.z = GLW_LP(8, c->glw_parent_pos.z, z);

      c->glw_parent_scale.x = 1.0f;
      c->glw_parent_scale.y = 1.0f;
      c->glw_parent_scale.z = 1.0f;

      c->glw_parent_misc[0] = GLW_LP(8, c->glw_parent_misc[0], a);

      glw_layout0(c, rc);

      z -= 1;
      a = a * 0.25;
    }

    break;
    
  case GLW_SIGNAL_RENDER:
    rc0 = *rc;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      rc0.rc_alpha = rc->rc_alpha * c->glw_parent_misc[0];
      glw_render_TS(c, &rc0, rc);
    }
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL) {
      if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra))
	return 1;
    }
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    glw_set_i(c, GLW_ATTRIB_FOCUSABLE, GLW_FOCUS_LEADER, NULL);

    switchfocus(w->glw_selected, c);
    w->glw_selected = c;
    c->glw_parent_pos.z = 1.0f;
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = extra;
    if(w->glw_selected != c)
      break;

    c = TAILQ_PREV(c, glw_queue, glw_parent_link);
    switchfocus(w->glw_selected, c);
    w->glw_selected = c;
    break;
  }

  return 0;
}

void 
glw_layer_ctor(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;

  if(init)
    glw_signal_handler_int(w, glw_layer_callback);

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

 }

