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
#include "glw_transitions.h"

#define glw_parent_alpha glw_parent_val[0].f
#define glw_parent_z     glw_parent_val[1].f
#define glw_parent_layer glw_parent_val[2].i32

static void
glw_layer_select_child(glw_t *w)
{
  glw_t *c;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link)
    if(!(c->glw_flags & (GLW_HIDDEN | GLW_RETIRED)))
      break;

  w->glw_selected = c;

  if(c != NULL)
    glw_focus_open_path_close_all_other(c);
}


/**
 *
 */
static int
glw_layer_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra, rc0;
  glw_t *c = extra, *p;
  float z, a;
  int layer = 0;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:

    if(w->glw_alpha < 0.01)
      return 0;
    rc0 = *rc;

    for(c = TAILQ_LAST(&w->glw_childs, glw_queue); c != NULL; c = p) {
      p = TAILQ_PREV(c, glw_queue, glw_parent_link);

      z = 1.0;
      a = 1.0;

      c->glw_parent_layer = layer;

      if(c->glw_flags & GLW_RETIRED) {
	a = 0;

	if(c->glw_parent_z > 0.99) {
	  glw_destroy(c);
	  continue;
	}
	
      } else if(!(c->glw_flags & GLW_HIDDEN)) {
	layer++;

	z = 0.0;
	a = 1;
      } else {
	a = 0;
      }

      c->glw_parent_z     = GLW_LP(5, c->glw_parent_z,     z);
      c->glw_parent_alpha = GLW_LP(5, c->glw_parent_alpha, a);

      rc0.rc_layer = c->glw_parent_layer + rc->rc_layer;

      if(c->glw_parent_alpha > 0.01)
	glw_layout0(c, &rc0);
    }

    break;
    

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL) {
      if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra))
	return 1;
    }
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    c->glw_parent_z = 1.0f;

    glw_layer_select_child(w);
    break;

  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    glw_layer_select_child(w);
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_layer_retire_child(glw_t *w, glw_t *c)
{
  c->glw_flags |= GLW_RETIRED;
  glw_layer_select_child(w);
}


/**
 *
 */
static void
glw_layer_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    rc0 = *rc;
    rc0.rc_alpha *= c->glw_parent_alpha * w->glw_alpha;
    if(rc0.rc_alpha < 0.01)
      continue;
    rc0.rc_layer = c->glw_parent_layer + rc->rc_layer;
    glw_Translatef(&rc0, 0, 0, 0.1*c->glw_parent_z);
    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static glw_class_t glw_layer = {
  .gc_name = "layer",
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_instance_size = sizeof(glw_t),
  .gc_render = glw_layer_render,
  .gc_retire_child = glw_layer_retire_child,
  .gc_signal_handler = glw_layer_callback,
};

GLW_REGISTER_CLASS(glw_layer);
