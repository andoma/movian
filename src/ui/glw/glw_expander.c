/*
 *  Based on rc_zoom, display a proportial amount of second child.
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

static glw_class_t glw_expander_x;

/**
 *
 */
typedef struct glw_expander {
  glw_t w;
  float expansion;
  float last;
} glw_expander_t;


/**
 *
 */
static void
update_constraints(glw_expander_t *exp)
{
  glw_t *c = TAILQ_FIRST(&exp->w.glw_childs);
  int e, o;
  
  int f = c ? glw_filter_constraints(c->glw_flags) : 0;

  if(exp->w.glw_class == &glw_expander_x) {
    e = exp->expansion * (c != NULL ? c->glw_req_size_x : 0);
    o =                  (c != NULL ? c->glw_req_size_y : 0);
    f &= GLW_CONSTRAINT_Y;
  } else {
    e = exp->expansion * (c != NULL ? c->glw_req_size_y : 0);
    o =                  (c != NULL ? c->glw_req_size_x : 0);
    f &= GLW_CONSTRAINT_X;
  }

  if(e == 0)
    glw_focus_close_path(&exp->w);
  else if(exp->w.glw_flags & GLW_FOCUS_BLOCKED)
    glw_focus_open_path(&exp->w);

  if(exp->w.glw_class == &glw_expander_x)
    glw_set_constraints(&exp->w, e, o, 0, GLW_CONSTRAINT_X | f, 0);
  else
    glw_set_constraints(&exp->w, o, e, 0, GLW_CONSTRAINT_Y | f, 0);
}


/**
 *
 */
static int
glw_expander_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  glw_rctx_t *rc = extra;
  glw_t *c;
  glw_rctx_t rc0;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    update_constraints(exp);
    return 1;

  case GLW_SIGNAL_LAYOUT:
    if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
      break;
    rc0 = *rc;

    if(exp->w.glw_class == &glw_expander_x) {
      rc0.rc_width = c->glw_req_size_x;

      if(rc0.rc_width == 0)
	rc0.rc_width = exp->last;
      else
	exp->last = rc0.rc_width;

    } else {
      rc0.rc_height = c->glw_req_size_y;

      if(rc0.rc_height == 0)
	rc0.rc_height = exp->last;
      else
	exp->last = rc0.rc_height;
    }
    glw_layout0(c, &rc0);
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_expander_render(glw_t *w, glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  if(w->glw_class == &glw_expander_x)
    rc0.rc_width = c->glw_req_size_x;
  else
    rc0.rc_height = c->glw_req_size_y;
  glw_render0(c, &rc0);
}


/**
 *
 */
static void 
glw_expander_ctor(glw_t *w)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  update_constraints(exp);
}


/**
 *
 */
static void 
glw_expander_set(glw_t *w, va_list ap)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_EXPANSION:
      exp->expansion = va_arg(ap, double);
      update_constraints(exp);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}



static glw_class_t glw_expander_x = {
  .gc_name = "expander_x",
  .gc_instance_size = sizeof(glw_expander_t),
  .gc_render = glw_expander_render,
  .gc_set = glw_expander_set,
  .gc_ctor = glw_expander_ctor,
  .gc_signal_handler = glw_expander_callback,
};

static glw_class_t glw_expander_y = {
  .gc_name = "expander_y",
  .gc_instance_size = sizeof(glw_expander_t),
  .gc_render = glw_expander_render,
  .gc_set = glw_expander_set,
  .gc_signal_handler = glw_expander_callback,
};

GLW_REGISTER_CLASS(glw_expander_x);
GLW_REGISTER_CLASS(glw_expander_y);
