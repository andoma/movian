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
#include "glw_expander.h"


/**
 *
 */
static void
update_constraints(glw_expander_t *exp)
{
  glw_t *c = TAILQ_FIRST(&exp->w.glw_childs);
  float e;

  e = exp->expansion * (c != NULL ? c->glw_req_size_y : 0);

  if(e == 0)
    glw_focus_close_path(&exp->w);
  else if(exp->w.glw_flags & GLW_FOCUS_BLOCKED)
    glw_focus_open_path(&exp->w);

  glw_set_constraints(&exp->w, 0, e, 0, 0, GLW_CONSTRAINT_Y, 0);
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

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    update_constraints(exp);
    return 1;

  case GLW_SIGNAL_LAYOUT:
    c = TAILQ_FIRST(&w->glw_childs);

    if(c != NULL)
      glw_layout0(c, rc);
    break;
    
  case GLW_SIGNAL_RENDER:
    c = TAILQ_FIRST(&w->glw_childs);

    if(c != NULL)
      glw_render0(c, rc);
    break;
  }
  return 0;
}


/**
 *
 */
void 
glw_expander_ctor(glw_t *w, int init, va_list ap)
{
  glw_expander_t *exp = (glw_expander_t *)w;
  glw_attribute_t attrib;

  if(init) {
    glw_signal_handler_int(w, glw_expander_callback);
    update_constraints(exp);
  }

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
