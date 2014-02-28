/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include "glw_grid.h"


#define glw_parent_pos    glw_parent_val[0].i32
#define glw_parent_width  glw_parent_val[1].i32

/**
 *
 */
static void
glw_gridrow_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_gridrow_t *ggr = (glw_gridrow_t *)w;
  glw_grid_t *gg = (glw_grid_t *)ggr->w.glw_parent;
  if(gg->w.glw_class != &glw_grid)
    return;

  glw_t *c;
  glw_rctx_t rc0 = *rc;
  const float scale = ggr->child_scale;
  int col_width = rc->rc_width * scale;

  if(gg->w.glw_focused == w)
    glw_lp(&gg->filtered_xpos, w->glw_root, gg->current_xpos, 0.1);

  int xpos = 0;
  int offset = rc->rc_width / 2 - gg->filtered_xpos;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    c->glw_parent_pos    = xpos + offset;
    c->glw_parent_width  = col_width;

    rc0.rc_width = col_width;
    glw_layout0(c, &rc0);

    if(c == ggr->scroll_to_me) {
      ggr->scroll_to_me = NULL;
      gg->current_xpos = xpos + col_width / 2;
    }
    xpos += col_width;
    c->glw_norm_weight = scale;
  }
}


/**
 *
 */
static void
glw_gridrow_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    rc0 = *rc;

    glw_reposition(&rc0,
		   c->glw_parent_pos,
		   rc->rc_height,
		   c->glw_parent_pos + c->glw_parent_width,
                   0);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
scroll_to_me(glw_gridrow_t *ggr, glw_t *c)
{
  ggr->scroll_to_me = c;
}


/**
 *
 */
static int
glw_gridrow_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_gridrow_t *ggr = (glw_gridrow_t *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;


  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;
    if(c == TAILQ_FIRST(&w->glw_childs) && !TAILQ_NEXT(c, glw_parent_link))
      scroll_to_me(ggr, c);
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(ggr, extra);
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(ggr->scroll_to_me == extra)
      ggr->scroll_to_me = NULL;
    break;
  }
  return 0;
}


/**
 *
 */
static int
glw_gridrow_set_float(glw_t *w, glw_attribute_t attrib, float value)
{
  glw_gridrow_t *ggr = (glw_gridrow_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_CHILD_SCALE:
    if(ggr->child_scale == value)
      return 0;

    ggr->child_scale = value;
    break;

  default:
    return -1;
  }
  return 1;
}



/**
 *
 */
static void
glw_gridrow_ctor(glw_t *w)
{
  glw_gridrow_t *ggr = (glw_gridrow_t *)w;
  ggr->child_scale = 1.0f;
}


/**
 *
 */
static glw_class_t glw_gridrow = {
  .gc_name = "gridrow",
  .gc_instance_size = sizeof(glw_gridrow_t),
  .gc_nav_descend_mode = GLW_NAV_DESCEND_ALL,
  .gc_layout = glw_gridrow_layout,
  .gc_render = glw_gridrow_render,
  .gc_set_float = glw_gridrow_set_float,
  .gc_signal_handler = glw_gridrow_callback,
  .gc_child_orientation = GLW_ORIENTATION_HORIZONTAL,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION,
  .gc_ctor = glw_gridrow_ctor,
};

GLW_REGISTER_CLASS(glw_gridrow);
