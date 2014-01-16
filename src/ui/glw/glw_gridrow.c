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
#define glw_parent_tile_x glw_parent_val[2].i32


/**
 * Super lame, we should pass it in glw_rctx_t instead i think
 */
static glw_grid_t *
get_grid(glw_t *w)
{
  glw_grid_t *gg;
  gg = (glw_grid_t *)w->glw_parent;

  if(gg->w.glw_class == &glw_grid)
    return gg;

  gg = (glw_grid_t *)w->glw_parent->glw_parent;
  if(gg->w.glw_class == &glw_grid)
    return gg;
  return NULL;
}



/**
 *
 */
static void
glw_gridrow_layout(glw_gridrow_t *ggr, glw_rctx_t *rc)
{
  glw_grid_t *gg = get_grid(&ggr->w);
  if(gg == NULL)
    return;

  glw_t *w = &ggr->w;
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  const float scale = ggr->child_scale;
  const float col_width = rc->rc_width * scale;

  float xpos = 0;
  float offset = round(rc->rc_width / 2 - gg->filtered_xtile * scale * rc->rc_width - col_width / 2 - (gg->filtered_xtile * ggr->spacing));
  int xtile = 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    c->glw_parent_pos    = xpos + offset;
    c->glw_parent_width  = col_width;

    rc0.rc_width = col_width;

    glw_layout0(c, &rc0);

    if(c == ggr->scroll_to_me) {
      ggr->scroll_to_me = NULL;
      if(gg->current_xtile != xtile) {
        gg->current_xtile = xtile;
        glw_signal0(w, GLW_SIGNAL_TILE_CHANGED, NULL);
      }
    }

    if(c->glw_parent_tile_x != xtile) {
      c->glw_parent_tile_x = xtile;
      glw_grid_flood_signal(c);
    }

    xtile++;
    xpos += col_width + ggr->spacing;
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


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    if(c->glw_parent_pos + c->glw_parent_width > 0 &&
       c->glw_parent_pos < rc->rc_width) {
      glw_rctx_t rc0 = *rc;

      glw_reposition(&rc0,
                     c->glw_parent_pos,
                     rc->rc_height,
                     c->glw_parent_pos + c->glw_parent_width,
                     0);

      glw_render0(c, &rc0);
    }
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
  glw_rctx_t *rc = extra;
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

  case GLW_SIGNAL_LAYOUT:
    glw_gridrow_layout(ggr, rc);
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(ggr, extra);
  case GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC:
    glw_signal0(w, GLW_SIGNAL_TILE_CHANGED, NULL);
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
static void
glw_gridrow_set(glw_t *w, va_list ap)
{
  glw_gridrow_t *ggr = (glw_gridrow_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_CHILD_SCALE:
      ggr->child_scale = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SPACING:
      ggr->spacing = va_arg(ap, int);
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
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_ALL,
  .gc_render = glw_gridrow_render,
  .gc_set = glw_gridrow_set,
  .gc_signal_handler = glw_gridrow_callback,
  .gc_child_orientation = GLW_ORIENTATION_HORIZONTAL,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION,
  .gc_ctor = glw_gridrow_ctor,
  .gc_escape_score = 100,
};

GLW_REGISTER_CLASS(glw_gridrow);


/**
 *
 */
int
glw_grid_get_tile_x(glw_t *w)
{
  if(w->glw_class == &glw_gridrow) {
    glw_grid_t *gg = get_grid(w);
    return gg ? gg->current_xtile : 0;
  }

  if(w->glw_class == &glw_gridrow)
    return w->glw_focused ? w->glw_focused->glw_parent_tile_x : 0;

  while(w->glw_parent) {
    if(w->glw_parent->glw_class == &glw_gridrow)
      break;
    w = w->glw_parent;
  }

  glw_t *p = w->glw_parent;
  if(p == NULL)
    return 0;

  return w->glw_parent_tile_x;
}

