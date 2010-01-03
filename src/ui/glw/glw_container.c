/*
 *  GL Widgets, GLW_CONTAINER -widgets
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
#include "glw_container.h"

/**
 *
 */
static int
glw_container_x_constraints(glw_container_t *co, glw_t *skip)
{
  glw_t *c;
  int ymax = 0, xsum = 0;
  float weight = 0;
  float aspect = 0;
  int cflags = 0, f;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    if(c == skip)
      continue;

    f = glw_filter_constraints(c->glw_flags);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    ymax = GLW_MAX(ymax, c->glw_req_size_y);

    if(f & GLW_CONSTRAINT_X) {
      xsum += c->glw_req_size_x;
    } else if(f & GLW_CONSTRAINT_A) {
      aspect += c->glw_req_aspect;
    } else if(f & GLW_CONSTRAINT_W) {
      weight += c->glw_req_weight;
    } else {
      weight += 1.0f;
    }
  }

  co->weight_sum = weight;
  co->aspect_sum = aspect;
  co->x_sum = xsum;
  co->y_sum = ymax;
  co->cflags = cflags;

  glw_set_constraints(&co->w, xsum, ymax, 0, 0, cflags, 0);
  return 1;
}


/**
 *
 */
static int
glw_container_x_layout(glw_container_t *co, glw_rctx_t *rc)
{
  glw_t *c;
  float x, xs;
  glw_rctx_t rc0 = *rc;
  int f;
  float s_w, s_ax, ax;

  float size_x = rc->rc_size_x - co->co_padding_left - co->co_padding_right;

  glw_flush_render_list(&co->w);

  if(co->w.glw_alpha < 0.01)
    return 0;

  /* Add sum of requested aspect to width in pixels */
  ax = co->co_padding_left + co->co_padding_right + 
    co->x_sum + co->aspect_sum * rc->rc_size_y; 

  glw_set_constraints(&co->w, ax, co->y_sum, 0, 0, co->cflags, 0);

  x = -1.0f + 2.0 * co->co_padding_left / rc->rc_size_x;

  if(ax > size_x) {
    // Requested pixel size > available width, must scale
    s_ax = size_x / ax;
    s_w = 0;
  } else {
    s_ax = 1.0f;
    s_w = size_x - ax;

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == GLW_ALIGN_CENTER) {
	x = 0 - ax / size_x;
      } else if(co->w.glw_alignment == GLW_ALIGN_RIGHT) {
	x = 1.0 - 2 * ax / size_x;
      }
    }
  }

  s_w /= co->weight_sum;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    f = glw_filter_constraints(c->glw_flags);

    if(f & GLW_CONSTRAINT_X) {
      xs = s_ax * c->glw_req_size_x;
    } else if(f & GLW_CONSTRAINT_A) {
      xs = s_ax * c->glw_req_aspect * rc->rc_size_y;
    } else if(f & GLW_CONSTRAINT_W) {
      xs = c->glw_req_weight * s_w;
    } else {
      xs = s_w;
    }

    c->glw_parent_scale.x = xs / size_x;
    c->glw_parent_scale.y = 1.0;
    c->glw_parent_scale.z = xs / size_x;
      
    c->glw_norm_weight = c->glw_parent_scale.x;

    c->glw_parent_pos.x = x + c->glw_parent_scale.x;

    x += 2 * c->glw_parent_scale.x;
      
    rc0.rc_size_x = xs;

    glw_layout0(c, &rc0);
 
    if(xs > 1)
      glw_link_render_list(&co->w, c);
  }
  return 0;
}


/**
 *
 */
static int
glw_container_y_constraints(glw_container_t *co, glw_t *skip)
{
  glw_t *c;
  int xmax = 0;
  int fix = 0;
  float weight = 0;
  int cflags = 0, f;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    if(c == skip)
      continue;

    f = glw_filter_constraints(c->glw_flags);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    xmax = GLW_MAX(xmax, c->glw_req_size_x);

    if(f & GLW_CONSTRAINT_Y) {
      fix += c->glw_req_size_y;
    } else if(f & GLW_CONSTRAINT_W) {
      weight += c->glw_req_weight;
    } else {
      weight += 1.0;
    }
  }

  co->x_sum = xmax;
  co->y_sum = fix;
  co->weight_sum = weight;
  co->cflags = cflags;

  if(weight)
    cflags &= ~GLW_CONSTRAINT_Y;

  glw_set_constraints(&co->w, xmax, fix, 0, 0, cflags, 0);
  return 1;
}


/**
 *
 */
static int
glw_container_y_layout(glw_container_t *co, glw_rctx_t *rc)
{
  glw_t *c;
  float y, ys;
  glw_rctx_t rc0 = *rc;
  int f;
  float s_w, s_fy;

  float size_y = rc->rc_size_y - co->co_padding_top - co->co_padding_bottom;

  glw_flush_render_list(&co->w);

  if(co->w.glw_alpha < 0.01)
    return 0;

  y = 1.0f - 2.0 * co->co_padding_top / rc->rc_size_y;

  if(co->y_sum > size_y) {
    s_w = 0;
    s_fy = size_y / co->y_sum;

  } else {
    s_w = size_y - co->y_sum;
    s_fy = 1.0f;

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == GLW_ALIGN_CENTER) {
	y = co->y_sum / size_y;
      } else if(co->w.glw_alignment == GLW_ALIGN_BOTTOM) {
	y = 1.0 - 2 * co->y_sum / size_y;
      }
    }
  }

  s_w /= co->weight_sum;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    
    f = glw_filter_constraints(c->glw_flags);
    
    if(f & GLW_CONSTRAINT_Y) {
      ys = s_fy * c->glw_req_size_y;
    } else if(f & GLW_CONSTRAINT_W) {
      ys = c->glw_req_weight * s_w;
    } else {
      ys = s_w;
    }

    c->glw_parent_scale.x = 1.0;
    c->glw_parent_scale.y = ys / size_y;
    c->glw_parent_scale.z = ys / size_y;

    c->glw_norm_weight = c->glw_parent_scale.y;

    c->glw_parent_pos.y = y - c->glw_parent_scale.y;

    y -= 2 * c->glw_parent_scale.y;
      
    rc0.rc_size_y = ys;

    glw_layout0(c, &rc0);
 
    if(ys > 1)
      glw_link_render_list(&co->w, c);
  }
  return 0;
}


/**
 *
 */
static int
glw_container_z_constraints(glw_t *w, glw_t *skip)
{
  glw_t *c;

  c = TAILQ_FIRST(&w->glw_childs);
  if(c == skip)
    c = TAILQ_NEXT(c, glw_parent_link);
  
  if(c != NULL)
    glw_copy_constraints(w, c);
  else
    glw_clear_constraints(w);

  return 1;
}


/**
 *
 */
static int
glw_container_z_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  if(w->glw_alpha < 0.01)
    return 0;

  rc0 = *rc;

  glw_flush_render_list(w);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    
    c->glw_parent_pos.x = 0;
    c->glw_parent_pos.y = 0;
    c->glw_parent_pos.z = 0;

    c->glw_parent_scale.x = 1.0f;
    c->glw_parent_scale.y = 1.0f;
    c->glw_parent_scale.z = 1.0f;

    glw_layout0(c, &rc0);

    glw_link_render_list(w, c);
  }
  return 0;
}




static void
glw_container_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  glw_rctx_t rc0 = *rc;

  if(alpha < 0.01)
    return;

  rc0.rc_alpha = alpha;
  
  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link)
    glw_render_TS(c, &rc0, rc);
}


static int
glw_container_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_RENDER:
    glw_container_render(w, extra);
    break;

  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;

  default:
    break;
  }
  return 0;
}



static int
glw_container_x_callback(glw_t *w, void *opaque, glw_signal_t signal,
			  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_container_x_layout((glw_container_t *)w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    return glw_container_x_constraints((glw_container_t *)w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_x_constraints((glw_container_t *)w, extra);
  default:
    return glw_container_callback(w, opaque, signal, extra);
  }
}

static int
glw_container_y_callback(glw_t *w, void *opaque, glw_signal_t signal,
			  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_container_y_layout((glw_container_t *)w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    return glw_container_y_constraints((glw_container_t *)w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_y_constraints((glw_container_t *)w, extra);
  default:
    return glw_container_callback(w, opaque, signal, extra);
  }
}

static int
glw_container_z_callback(glw_t *w, void *opaque, glw_signal_t signal,
			 void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_container_z_layout(w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    return glw_container_z_constraints(w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_z_constraints(w, extra);
  default:
    return glw_container_callback(w, opaque, signal, extra);
  }
}



void 
glw_container_ctor(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;
  glw_container_t *co = (glw_container_t *)w;

  if(init) {
    switch(w->glw_class) {
    default:
      abort();

    case GLW_CONTAINER_X:
      glw_signal_handler_int(w, glw_container_x_callback);
      break;

    case GLW_CONTAINER_Y:
      glw_signal_handler_int(w, glw_container_y_callback);
      break;
      
    case GLW_CONTAINER_Z:
      glw_signal_handler_int(w, glw_container_z_callback);
      break;
    }
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_PADDING:
      co->co_padding_left   = va_arg(ap, double);
      co->co_padding_top    = va_arg(ap, double);
      co->co_padding_right  = va_arg(ap, double);
      co->co_padding_bottom = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

}
