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
#include "glw_array.h"

/**
 *
 */
static void
glw_list_update_metrics(glw_array_t *a, float max, float val)
{
  a->w.glw_flags &= ~GLW_UPDATE_METRICS;
  a->metrics.knob_size = 2.0 / (max + 2.0);
  if(max > 0)
    a->metrics.position = val / max;
  else
    a->metrics.position = 0;

  glw_signal0(&a->w, GLW_SIGNAL_SLIDER_METRICS, &a->metrics);
}


/**
 *
 */
static void
glw_array_layout(glw_array_t *a, glw_rctx_t *rc)
{
  glw_t *c, *w = &a->w;
  float y = 0;
  float size_y, t, vy;
  glw_rctx_t rc0 = *rc;
  int column = 0;
  int xentries;
  float size_x;

  if(a->child_tiles_x && a->child_tiles_y) {

    xentries = a->child_tiles_x;
    size_y = 1.0 / a->child_tiles_y;

  } else {

    xentries = GLW_MAX(1, rc->rc_size_x / a->child_width);
    size_y = a->child_height / rc->rc_size_y;
  }

  a->xentries = xentries;
  size_x = 1.0 / xentries;

  t = GLW_MIN(GLW_MAX(0, a->center_y_target), a->center_y_max);
  a->center_y = GLW_LP(6, a->center_y, t);


  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    vy = y + size_y;

    c->glw_parent_pos.y = 1.0 - vy + a->center_y;
    c->glw_parent_pos.x = -1.0 + (2 * size_x * column) + size_x;

    if(c->glw_parent_pos.y - size_y <= 1.5f &&
       c->glw_parent_pos.y + size_y >= -1.5f) {

      c->glw_parent_scale.x = size_x;
      c->glw_parent_scale.y = size_y;
      c->glw_parent_scale.z = size_y;

      rc0.rc_size_x = rc->rc_size_x * c->glw_parent_scale.x;
      rc0.rc_size_y = rc->rc_size_y * c->glw_parent_scale.y;
      glw_layout0(c, &rc0);
    }

    if(c == a->scroll_to_me) {
      a->scroll_to_me = NULL;
     
      if(vy + size_y - a->center_y_target > 2) {
	t = vy + size_y - 2;
	w->glw_flags |= GLW_UPDATE_METRICS;
      } else if(vy - size_y - a->center_y_target < 0) {
	t = vy - size_y;
	w->glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    column++;
    if(column == xentries) {
      y += size_y * 2;
      column = 0;
    }
  }

  if(column != 0)
    y += size_y * 2;

  y = GLW_MAX(y - 2, 0);
  
  if(a->center_y_max != y)
    w->glw_flags |= GLW_UPDATE_METRICS;

  a->center_y_max = y;
  a->center_y_target = t;

  if(w->glw_flags & GLW_UPDATE_METRICS)
    glw_list_update_metrics(a, y, t);
}


/**
 *
 */
static void
glw_array_render(glw_array_t *a, glw_rctx_t *rc)
{
  glw_t *w = &a->w;
  glw_t *c;
  glw_rctx_t rc0;
  int clip1, clip2;
  float size_y;
  if(rc->rc_alpha < 0.01)
    return;

  size_y = a->child_height / rc->rc_size_y;

  glw_store_matrix(w, rc);
  
  clip1 = glw_clip_enable(rc, GLW_CLIP_TOP);
  clip2 = glw_clip_enable(rc, GLW_CLIP_BOTTOM);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_parent_pos.y - size_y <= 1.0f &&
       c->glw_parent_pos.y + size_y >= -1.0f) {
      rc0 = *rc;
      glw_render_TS(c, &rc0, rc);
      c->glw_flags &= ~GLW_FOCUS_BLOCKED;
    } else {
      c->glw_flags |= GLW_FOCUS_BLOCKED;
    }
  }

  glw_clip_disable(rc, clip1);
  glw_clip_disable(rc, clip2);
}


/**
 *
 */
static void
glw_array_scroll(glw_array_t *a, glw_scroll_t *gs)
{
  a->center_y_target = gs->value * a->center_y_max;
}


/**
 *
 */
static int
glw_array_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_array_t *a = (glw_array_t *)w;
  glw_pointer_event_t *gpe;
  glw_t *c;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_array_layout(a, rc);
    return 0;

  case GLW_SIGNAL_RENDER:
    glw_array_render(a, rc);
    return 0;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    a->scroll_to_me = extra;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    c->glw_parent_pos.z = 0.0;

    c->glw_parent_scale.x = 1.0;
    c->glw_parent_scale.y = 1.0;
    c->glw_parent_scale.z = 1.0;
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(a->scroll_to_me == extra)
      a->scroll_to_me = NULL;
    break;

  case GLW_SIGNAL_POINTER_EVENT:
    gpe = extra;

    if(gpe->type == GLW_POINTER_SCROLL) {
      a->center_y_target += gpe->delta_y;
      a->w.glw_flags |= GLW_UPDATE_METRICS;
    }
    break;

  case GLW_SIGNAL_SCROLL:
    glw_array_scroll(a, extra);
    break;
  }
  return 0;
}



/**
 *
 */
void 
glw_array_ctor(glw_t *w, int init, va_list ap)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_attribute_t attrib;

  if(init) {
    glw_signal_handler_int(w, glw_array_callback);

    // Just something
    a->child_width  = 100;
    a->child_height = 100;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_CHILD_HEIGHT:
      a->child_height = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_WIDTH:
      a->child_width  = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_TILES_X:
      a->child_tiles_x = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_TILES_Y:
      a->child_tiles_y = va_arg(ap, int);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
