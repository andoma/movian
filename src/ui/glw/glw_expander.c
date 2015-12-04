/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include "glw.h"

static glw_class_t glw_expander_x;

/**
 *
 */
typedef struct glw_expander {
  glw_t w;
  float expansion;
  int last;
} glw_expander_t;


/**
 *
 */
static void
update_constraints(glw_expander_t *exp)
{
  glw_t *c = TAILQ_FIRST(&exp->w.glw_childs);
  int e, o;
  
  int f = c ? glw_filter_constraints(c) : 0;

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
    glw_set_constraints(&exp->w, e, o, 0, GLW_CONSTRAINT_X | f);
  else
    glw_set_constraints(&exp->w, o, e, 0, GLW_CONSTRAINT_Y | f);
}


/**
 *
 */
static void
glw_expander_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  if(exp->expansion < GLW_ALPHA_EPSILON)
    return;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;
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
}



/**
 *
 */
static int
glw_expander_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_expander_t *exp = (glw_expander_t *)w;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    update_constraints(exp);
    return 1;

  }
  return 0;
}


/**
 *
 */
static void
glw_expander_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  glw_rctx_t rc0;
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return;

  if(exp->expansion < GLW_ALPHA_EPSILON)
    return;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  /**
   * Trick childs into rendering themselfs as if the widget is
   * fully expanded
   */

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
static int
glw_expander_set_float(glw_t *w, glw_attribute_t attrib, float value,
                       glw_style_t *gs)
{
  glw_expander_t *exp = (glw_expander_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_EXPANSION:
    if(exp->expansion == value)
      return 0;

    exp->expansion = value;
    update_constraints(exp);
    break;

  default:
    return -1;
  }
  return 1;
}



static glw_class_t glw_expander_x = {
  .gc_name = "expander_x",
  .gc_instance_size = sizeof(glw_expander_t),
  .gc_layout = glw_expander_layout,
  .gc_render = glw_expander_render,
  .gc_set_float = glw_expander_set_float,
  .gc_ctor = glw_expander_ctor,
  .gc_signal_handler = glw_expander_callback,
};

static glw_class_t glw_expander_y = {
  .gc_name = "expander_y",
  .gc_instance_size = sizeof(glw_expander_t),
  .gc_layout = glw_expander_layout,
  .gc_render = glw_expander_render,
  .gc_set_float = glw_expander_set_float,
  .gc_signal_handler = glw_expander_callback,
  .gc_ctor = glw_expander_ctor,
};

GLW_REGISTER_CLASS(glw_expander_x);
GLW_REGISTER_CLASS(glw_expander_y);
