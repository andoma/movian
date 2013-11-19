/*
 *  GL Widgets, GLW_ARRAY widget
 *  Copyright (C) 2007 Andreas Öman
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
#include "glw_grid.h"


#define glw_parent_pos    glw_parent_val[0].i32
#define glw_parent_height glw_parent_val[1].i32

/**
 *
 */
static void
glw_grid_layout(glw_grid_t *gg, glw_rctx_t *rc)
{
  glw_t *w = &gg->w;
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  const float scale = gg->child_scale;
  int row_height = rc->rc_height * scale;

  glw_lp(&gg->filtered_ypos, w->glw_root, gg->current_ypos, 0.1);

  int ypos = 0;
  int offset = rc->rc_height / 2 - gg->filtered_ypos;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    c->glw_parent_pos    = ypos + offset;
    c->glw_parent_height = row_height;

    rc0.rc_height = row_height;
    glw_layout0(c, &rc0);

    if(c == gg->scroll_to_me) {
      gg->scroll_to_me = NULL;
      gg->current_ypos = ypos + row_height / 2;
    }
    ypos += row_height;
    c->glw_norm_weight = scale;
  }
}


/**
 *
 */
static void
glw_grid_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    rc0 = *rc;

    glw_reposition(&rc0,
		   0,
		   rc->rc_height - c->glw_parent_pos,
		   rc->rc_width,
		   rc->rc_height - c->glw_parent_pos - c->glw_parent_height);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
scroll_to_me(glw_grid_t *gg, glw_t *c)
{
  gg->scroll_to_me = c;
}


/**
 *
 */
static int
glw_grid_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_grid_t *gg = (glw_grid_t *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;


  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;
    if(c == TAILQ_FIRST(&w->glw_childs) && !TAILQ_NEXT(c, glw_parent_link))
      gg->scroll_to_me = c;
    break;

  case GLW_SIGNAL_LAYOUT:
    glw_grid_layout(gg, rc);
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(gg, extra);
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(gg->scroll_to_me == extra)
      gg->scroll_to_me = NULL;
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_grid_set(glw_t *w, va_list ap)
{
  glw_grid_t *gg = (glw_grid_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_CHILD_SCALE:
      gg->child_scale = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}


/**
 *
 */
static void
glw_grid_ctor(glw_t *w)
{
  glw_grid_t *gg = (glw_grid_t *)w;
  gg->child_scale = 1.0f;
}

/**
 *
 */
glw_class_t glw_grid = {
  .gc_name = "grid",
  .gc_instance_size = sizeof(glw_grid_t),
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_render = glw_grid_render,
  .gc_set = glw_grid_set,
  .gc_signal_handler = glw_grid_callback,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION,
  .gc_ctor = glw_grid_ctor,

};

GLW_REGISTER_CLASS(glw_grid);
