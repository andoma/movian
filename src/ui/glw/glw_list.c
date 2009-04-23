/*
 *  GL Widgets, GLW_LIST widget
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
#include "glw_list.h"

#define glw_parent_size_x glw_parent_misc[0]
#define glw_parent_size_y glw_parent_misc[1]

/**
 *
 */
static void
glw_list_update_metrics(glw_list_t *l, float max, float val)
{
  l->w.glw_flags &= ~GLW_UPDATE_METRICS;
  l->metrics.knob_size = 2.0 / (max + 2.0);
  if(max > 0)
    l->metrics.position = val / max;
  else
    l->metrics.position = 0;

  glw_signal0(&l->w, GLW_SIGNAL_SLIDER_METRICS, &l->metrics);
}


/**
 *
 */
static void
glw_list_layout_y(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  float y = 0;
  float sa, size_y, t, vy;
  glw_rctx_t rc0 = *rc;

  sa = rc->rc_size_x / rc->rc_size_y;

  t = GLW_MIN(GLW_MAX(0, l->center_y_target), l->center_y_max);
  l->center_y = GLW_LP(6, l->center_y, t);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_req_size_y) {
      size_y = c->glw_req_size_y / rc->rc_size_y;
    } else if(c->glw_req_aspect) {
      size_y = sa / c->glw_req_aspect;
    } else {
      size_y = sa / 10;
    }
    

    c->glw_parent_size_y = size_y;

    vy = y + size_y;

    c->glw_parent_pos.y = 1.0 - vy + l->center_y;

    if(c->glw_parent_pos.y - c->glw_parent_size_y <= 1.5f &&
       c->glw_parent_pos.y + c->glw_parent_size_y >= -1.5f) {

      c->glw_parent_pos.z = 0;
      c->glw_parent_pos.x = 0;

      c->glw_parent_scale.y = size_y;
      rc0.rc_size_y = rc->rc_size_y * size_y;
      glw_layout0(c, &rc0);
    }

    if(c == l->scroll_to_me) {
      l->scroll_to_me = NULL;
     
      if(vy + size_y - l->center_y_target > 2) {
	t = vy + size_y - 2;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(vy - size_y - l->center_y_target < 0) {
	t = vy - size_y;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }
    y += size_y * 2;
  }

  y = GLW_MAX(y - 2, 0);
  
  if(l->center_y_max != y)
    l->w.glw_flags |= GLW_UPDATE_METRICS;

  l->center_y_max = y;
  l->center_y_target = t;

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_list_update_metrics(l, y, t);
}



/**
 *
 */
static void
glw_list_layout_x(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  float x = 0;
  float isa, size_x, t, vx;
  glw_rctx_t rc0 = *rc;

  isa = rc->rc_size_y / rc->rc_size_x;

  t = GLW_MIN(GLW_MAX(0, l->center_x_target), l->center_x_max);
  l->center_x = GLW_LP(6, l->center_x, t);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_req_aspect) {
      size_x = c->glw_req_aspect * isa;
    } else {
      size_x = isa;
    }

    c->glw_parent_size_x = size_x;

    vx = x + size_x;

    c->glw_parent_pos.x = -1.0 + vx - l->center_x;
    c->glw_parent_pos.y = 0;
    c->glw_parent_pos.z = 0;
    
    c->glw_parent_scale.x = size_x;

    rc0.rc_size_x = rc->rc_size_x * size_x;

    glw_layout0(c, &rc0);

    if(c == l->scroll_to_me) {
      l->scroll_to_me = NULL;
     
      if(vx + size_x - l->center_x_target > 2) {
	t = vx + size_x - 2;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(vx - size_x - l->center_x_target < 0) {
	t = vx - size_x;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }
    x += size_x * 2;
  }

  x = GLW_MAX(x - 2, 0);

  if(l->center_x_max != x)
    l->w.glw_flags |= GLW_UPDATE_METRICS;


  l->center_x_max = x;
  l->center_x_target = t;

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_list_update_metrics(l, x, t);
}


/**
 *
 */
static void
glw_list_render(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  glw_rctx_t rc0;
  int clip1, clip2;
  if(rc->rc_alpha < 0.01)
    return;

  glw_store_matrix(w, rc);
  
  if(w->glw_class == GLW_LIST_X) {
    clip1 = glw_clip_enable(rc, GLW_CLIP_LEFT);
    clip2 = glw_clip_enable(rc, GLW_CLIP_RIGHT);
  } else {
    clip1 = glw_clip_enable(rc, GLW_CLIP_TOP);
    clip2 = glw_clip_enable(rc, GLW_CLIP_BOTTOM);
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_parent_pos.y - c->glw_parent_size_y <= 1.0f &&
       c->glw_parent_pos.y + c->glw_parent_size_y >= -1.0f &&
       c->glw_parent_pos.x - c->glw_parent_size_x <= 1.0f &&
       c->glw_parent_pos.x + c->glw_parent_size_x >= -1.0f) {
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
glw_list_scroll(glw_list_t *l, glw_scroll_t *gs)
{
  if(l->w.glw_class == GLW_LIST_Y)
    l->center_y_target = gs->value * l->center_y_max;
  else
    l->center_x_target = gs->value * l->center_x_max;
}


/**
 *
 */
static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_list_t *l = (void *)w;
  glw_pointer_event_t *gpe;
  glw_t *c;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    if(w->glw_class == GLW_LIST_Y)
      glw_list_layout_y(l, rc);
    else
      glw_list_layout_x(l, rc);
    return 0;

  case GLW_SIGNAL_RENDER:
    glw_list_render(l, rc);
    return 0;

  case GLW_SIGNAL_FOCUS_INTERACTIVE:
    l->scroll_to_me = extra;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    c->glw_parent_size_x = 0.5;
    c->glw_parent_size_y = 0.5;

    c->glw_parent_scale.x = 1.0;
    c->glw_parent_scale.y = 1.0;
    c->glw_parent_scale.z = 1.0;
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(l->scroll_to_me == extra)
      l->scroll_to_me = NULL;
    break;

  case GLW_SIGNAL_POINTER_EVENT:
    gpe = extra;

    if(gpe->type == GLW_POINTER_SCROLL) {
      l->center_y_target += gpe->delta_y;
      l->w.glw_flags |= GLW_UPDATE_METRICS;
    }
    break;

  case GLW_SIGNAL_SCROLL:
    glw_list_scroll(l, extra);
    break;
  }
  return 0;
}


/**
 *
 */
void 
glw_list_ctor(glw_t *w, int init, va_list ap)
{
  glw_list_t *l = (void *)w;
  glw_attribute_t attrib;

  if(init) {
    glw_signal_handler_int(w, glw_list_callback);
    l->child_aspect = w->glw_class == GLW_LIST_Y ? 20 : 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_CHILD_ASPECT:
      l->child_aspect = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
