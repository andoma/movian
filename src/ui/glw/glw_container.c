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

typedef struct glw_container {
  glw_t w;
  
  int cflags;
  float weight_sum;

  int16_t width;
  int16_t height;
  int16_t co_padding_left;
  int16_t co_padding_right;
  int16_t co_padding_top;
  int16_t co_padding_bottom;
  int16_t co_spacing;
  int16_t co_biggest;
  char co_using_aspect;

} glw_container_t;

#define glw_parent_size   glw_parent_val[0].i32
#define glw_parent_pos    glw_parent_val[1].f
#define glw_parent_scale  glw_parent_val[2].f

/**
 *
 */
static int
glw_container_x_constraints(glw_container_t *co, glw_t *skip)
{
  glw_t *c;
  int height = 0;
  int width = co->co_padding_left + co->co_padding_right;
  float weight = 0;
  int cflags = 0, f;
  int elements = 0;
  int numfix = 0;

  co->co_biggest = 0;
  co->co_using_aspect = 0;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN || c == skip)
      continue;

    f = glw_filter_constraints(c->glw_flags);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    if(f & GLW_CONSTRAINT_Y)
      height = GLW_MAX(height, c->glw_req_size_y);

    if(f & GLW_CONSTRAINT_X) {

      if(co->w.glw_flags & GLW_HOMOGENOUS) {
	co->co_biggest = GLW_MAX(c->glw_req_size_x, co->co_biggest);
	numfix++;
      } else {
	width += c->glw_req_size_x;
      }
    } else if(f & GLW_CONSTRAINT_W) {
      if(c->glw_req_weight == 0)
	continue;
      if(c->glw_req_weight > 0)
	weight += c->glw_req_weight;
      else
	co->co_using_aspect = 1;

    } else {
      weight += 1.0f;
    }
    elements++;
  }

  if(co->w.glw_flags & GLW_HOMOGENOUS)
    width += numfix * co->co_biggest;

  if(elements > 0)
    width += (elements - 1) * co->co_spacing;

  co->weight_sum = weight;
  co->width = width;
  co->cflags = cflags;

  height += co->co_padding_bottom + co->co_padding_top;

  glw_set_constraints(&co->w, width, height, 0, cflags, 0);
  return 1;
}


/**
 *
 */
static int
glw_container_x_layout(glw_container_t *co, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  int width = co->width;
  float IW; 
  int weightavail;  // Pixels available for weighted childs
  float pos;        // Current position
  float fixscale;   // Scaling to apply to fixed width requests
                    // Used if the available width < sum of requested width

  if(co->w.glw_alpha < 0.01f)
    return 0;

  rc0.rc_height = rc->rc_height - co->co_padding_top - co->co_padding_bottom;

  if(co->co_using_aspect) {
    // If any of our childs wants a fixed aspect we need to compute
    // the total width those will consume
    TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
      int f = glw_filter_constraints(c->glw_flags);
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w < 0)
	width += rc0.rc_height * -w;
    }
  }

  if(width > rc->rc_width) {
    // Requested pixel size > available width, must scale
    weightavail = 0;
    fixscale = (float)rc->rc_width / width;
    pos = co->co_padding_left * fixscale;
  } else {
    fixscale = 1;

    weightavail = rc->rc_width - width;  // Pixels available for weighted childs

    pos = co->co_padding_left;

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == LAYOUT_ALIGN_CENTER) {
	pos = rc->rc_width / 2 - width / 2;
      } else if(co->w.glw_alignment == LAYOUT_ALIGN_RIGHT) {
	pos = rc->rc_width - width;
      }
    }
  }

  int right, left = rintf(pos);

  IW = 1.0f / rc->rc_width;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    float cw;

    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c->glw_flags);

    if(f & GLW_CONSTRAINT_X) {
      if(co->w.glw_flags & GLW_HOMOGENOUS)
	cw = co->co_biggest * fixscale;
      else
	cw = c->glw_req_size_x * fixscale;
    } else {
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w == 0)
	continue;

      if(w > 0) {
	cw = weightavail * w / co->weight_sum;
      } else {
	cw = rc0.rc_height * -w;
      }
    }

    pos += cw;
    right = rintf(pos);
    
    rc0.rc_width = right - left;

    c->glw_parent_pos = -1.0f + (right + left) * IW;
    c->glw_parent_scale = rc0.rc_width * IW;
      
    c->glw_norm_weight = c->glw_parent_scale;

    c->glw_parent_size = right - left;
    glw_layout0(c, &rc0);
    left = right + co->co_spacing;
    pos += co->co_spacing;

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
  int width = 0;
  int height = co->co_padding_bottom + co->co_padding_top;
  float weight = 0;
  int cflags = 0, f;
  int elements = 0;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN || c == skip)
      continue;

    f = glw_filter_constraints(c->glw_flags);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    if(f & GLW_CONSTRAINT_X)
      width = GLW_MAX(width, c->glw_req_size_x);

    if(f & GLW_CONSTRAINT_Y) {
      height += c->glw_req_size_y;
    } else if(f & GLW_CONSTRAINT_W) {
      if(c->glw_req_weight > 0)
	weight += c->glw_req_weight;
    } else {
      weight += 1.0f;
    }
    elements++;
  }

  if(elements > 0)
    height += (elements - 1) * co->co_spacing;

  co->height = height;
  co->weight_sum = weight;
  co->cflags = cflags;

  if(weight)
    cflags &= ~GLW_CONSTRAINT_Y;

  width += co->co_padding_left + co->co_padding_right;
  glw_set_constraints(&co->w, width, height, 0, cflags, 0);
  return 1;
}


static int
glw_container_y_layout(glw_container_t *co, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  const int height = co->height;
  float IH;
  int weightavail;  // Pixels available for weighted childs
  float pos;        // Current position
  float fixscale;   // Scaling to apply to fixed height requests
                    // Used if the available height < sum of requested height
  
  if(co->w.glw_alpha < 0.01f)
    return 0;

  rc0.rc_width = rc->rc_width - co->co_padding_left - co->co_padding_right;

  if(height > rc->rc_height) {
    // Requested pixel size > available height, must scale
    weightavail = 0;
    fixscale = (float)rc->rc_height / height;
    pos = co->co_padding_top * fixscale;
  } else {
    fixscale = 1;

    // Pixels available for weighted childs
    weightavail = rc->rc_height - height;

    pos = co->co_padding_top;

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == LAYOUT_ALIGN_CENTER) {
	pos = rc->rc_height / 2 - height / 2;
      } else if(co->w.glw_alignment == LAYOUT_ALIGN_BOTTOM) {
	pos = rc->rc_height - height;
      }
    }
  }

  int bottom, top = rintf(pos);
  IH = 1.0f / rc->rc_height;

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    float cw = 0;

    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c->glw_flags);

    if(f & GLW_CONSTRAINT_Y) {
      cw = fixscale * c->glw_req_size_y;
    } else {
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w > 0)
	cw = weightavail * w / co->weight_sum;
    }

    pos += cw;
    bottom = rintf(pos);

    rc0.rc_height = bottom - top;

    c->glw_parent_pos = 1.0f - (bottom + top) * IH;
    c->glw_parent_scale = rc0.rc_height * IH;
    c->glw_norm_weight = c->glw_parent_scale;
    c->glw_parent_size = rc0.rc_height;

    glw_layout0(c, &rc0);
    top = bottom + co->co_spacing;
    pos += co->co_spacing;

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
  while(c != NULL) {
    if(c == skip)
      c = TAILQ_NEXT(c, glw_parent_link);
    
    if(c == NULL || !(c->glw_class->gc_flags & GLW_UNCONSTRAINED))
      break;
    c = TAILQ_NEXT(c, glw_parent_link);
  }

       

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

  if(w->glw_alpha < 0.01f)
    return 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
  return 0;
}


/**
 *
 */
static void
glw_container_y_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  float blur  = rc->rc_blur  * w->glw_blur;
  glw_container_t *co = (glw_container_t *)w;
  glw_rctx_t rc0;

  if(alpha < 0.01f)
    return;
  
  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if(co->co_padding_left || co->co_padding_right) {
    glw_rctx_t rc1 = *rc;
    glw_reposition(&rc1,
		   co->co_padding_left,
		   rc->rc_height,
		   rc->rc_width - co->co_padding_right,
		   0);
    rc = &rc1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    rc0 = *rc;
    rc0.rc_alpha = alpha;
    rc0.rc_blur  = blur;

    rc0.rc_height = c->glw_parent_size;
    
    glw_Translatef(&rc0, 0, c->glw_parent_pos, 0);
    glw_Scalef(&rc0, 1.0, c->glw_parent_scale, c->glw_parent_scale);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
glw_container_x_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  float blur  = rc->rc_blur  * w->glw_blur;
  glw_container_t *co = (glw_container_t *)w;
  glw_rctx_t rc0;

  if(alpha < 0.01f)
    return;
  
  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if(co->co_padding_top || co->co_padding_bottom) {
    glw_rctx_t rc1 = *rc;
    glw_reposition(&rc1,
		   0,
		   rc->rc_height - co->co_padding_top,
		   rc->rc_width,
		   co->co_padding_bottom);
    rc = &rc1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    rc0 = *rc;
    rc0.rc_alpha = alpha;
    rc0.rc_blur  = blur;

    rc0.rc_width = c->glw_parent_size;
    
    glw_Translatef(&rc0, c->glw_parent_pos, 0, 0);
    glw_Scalef(&rc0, c->glw_parent_scale, 1.0, c->glw_parent_scale);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
glw_container_z_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;

  glw_rctx_t rc0;

  if(alpha < 0.01f)
    return;
  
  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  rc0 = *rc;
  rc0.rc_alpha = alpha;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static int
glw_container_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_t *c;

  switch(signal) {
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
  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
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
  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
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


/**
 *
 */
static void
glw_container_set(glw_t *w, va_list ap)
{
  glw_attribute_t attrib;
  glw_container_t *co = (glw_container_t *)w;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SPACING:
      co->co_spacing = va_arg(ap, int);
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
set_padding(glw_t *w, const int16_t *v)
{
  glw_container_t *co = (glw_container_t *)w;
  co->co_padding_left   = v[0];
  co->co_padding_top    = v[1];
  co->co_padding_right  = v[2];
  co->co_padding_bottom = v[3];
}


/**
 *
 */
static glw_class_t glw_container_x = {
  .gc_name = "container_x",
  .gc_instance_size = sizeof(glw_container_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_set = glw_container_set,
  .gc_render = glw_container_x_render,
  .gc_signal_handler = glw_container_x_callback,
  .gc_child_orientation = GLW_ORIENTATION_HORIZONTAL,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_padding = set_padding,
};

static glw_class_t glw_container_y = {
  .gc_name = "container_y",
  .gc_instance_size = sizeof(glw_container_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_set = glw_container_set,
  .gc_render = glw_container_y_render,
  .gc_signal_handler = glw_container_y_callback,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION,
  .gc_default_alignment = LAYOUT_ALIGN_TOP,
  .gc_set_padding = set_padding,
};

static glw_class_t glw_container_z = {
  .gc_name = "container_z",
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_instance_size = sizeof(glw_container_t),
  .gc_set = glw_container_set,
  .gc_render = glw_container_z_render,
  .gc_signal_handler = glw_container_z_callback,
};



GLW_REGISTER_CLASS(glw_container_x);
GLW_REGISTER_CLASS(glw_container_y);
GLW_REGISTER_CLASS(glw_container_z);
