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


typedef struct glw_array {
  glw_t w;

  float filtered_pos;
  int total_size;
  int current_pos;
  int page_size;

  glw_t *scroll_to_me;

  glw_slider_metrics_t metrics;
  
  int child_width_fixed;
  int child_height_fixed;

  int child_tiles_x;
  int child_tiles_y;

  int child_width_px;
  int child_height_px;

  int xentries;
  int yentries;

  int16_t saved_height;
  int16_t saved_width;

  int16_t xspacing;
  int16_t yspacing;

  int16_t margin_left;
  int16_t margin_right;
  int16_t margin_top;
  int16_t margin_bottom;

  int16_t border_left;
  int16_t border_right;
  int16_t border_top;
  int16_t border_bottom;

  int num_visible_childs;

} glw_array_t;

#define glw_parent_pos_x glw_parent_val[0].i32
#define glw_parent_pos_y glw_parent_val[1].i32

/**
 *
 */
static void
glw_array_update_metrics(glw_array_t *a)
{
  float v;
  int do_update = 0;

  a->w.glw_flags &= ~GLW_UPDATE_METRICS;

  v = GLW_MIN(1.0f, (float)a->page_size / a->total_size);

  if(v != a->metrics.knob_size) {
    do_update = 1;
    a->metrics.knob_size = v;
  }
  
  v = GLW_MAX(0, (float)a->current_pos / (a->total_size - a->page_size));

  if(v != a->metrics.position) {
    do_update = 1;
    a->metrics.position = v;
  }
  
  if(!do_update)
    return;

  if(a->total_size > a->page_size && !(a->w.glw_flags & GLW_CAN_SCROLL)) {
    a->w.glw_flags |= GLW_CAN_SCROLL;
    glw_signal0(&a->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
    
  } else if(a->total_size <= a->page_size &&
	    a->w.glw_flags & GLW_CAN_SCROLL) {
    a->w.glw_flags &= ~GLW_CAN_SCROLL;
    glw_signal0(&a->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
  }

  glw_signal0(&a->w, GLW_SIGNAL_SLIDER_METRICS, &a->metrics);
}


/**
 *
 */
static void
glw_array_layout(glw_array_t *a, glw_rctx_t *rc)
{
  glw_t *c, *w = &a->w, *last;
  glw_rctx_t rc0 = *rc;
  int column = 0;
  int topedge = 1;
  int xspacing = 0, yspacing = 0;
  int height, width, rows;
  int xpos = 0, ypos = 0;

  glw_reposition(&rc0,
		 (a->margin_left + a->border_left),
		 rc->rc_height - (a->margin_top + a->border_top),
		 rc->rc_width - (a->margin_right + a->border_right),
		 a->margin_bottom + a->border_bottom);

  height = rc0.rc_height;
  width = rc0.rc_width;

  if(a->child_tiles_x || a->child_tiles_y) {

    xspacing = a->xspacing;
    yspacing = a->yspacing;

    a->xentries = a->child_tiles_x;
    a->yentries = a->child_tiles_y;

    if(a->yentries == 0) {
      a->yentries = a->xentries * height / width;
    } else if(a->xentries == 0) {
      a->xentries = a->yentries * width / height;
    }

    a->child_width_px  = (rc0.rc_width - (a->xentries - 1) * xspacing) /
      a->xentries;

    a->child_height_px = (rc0.rc_height - (a->yentries - 1) * yspacing) /
      a->yentries;

    if(a->child_width_fixed && a->child_width_px > a->child_width_fixed) {
      int e = a->child_width_px - a->child_width_fixed;
      xspacing += (e * a->xentries) / (a->xentries - 1);
      a->child_width_px = a->child_width_fixed;
    }

    if(a->child_height_fixed && a->child_height_px > a->child_height_fixed) {
      int e = a->child_height_px - a->child_height_fixed;
      yspacing += (e * a->yentries) / (a->yentries - 1);
      a->child_height_px = a->child_height_fixed;
    }
      
    if(a->num_visible_childs < a->child_tiles_x)
      xpos = (a->child_tiles_x - a->num_visible_childs) * 
	(xspacing + a->child_width_px) / 2;

    rows = (a->num_visible_childs - 1) / a->child_tiles_x + 1;

    if(rows < a->child_tiles_y)
      ypos = (a->child_tiles_y - rows) * 
	(yspacing + a->child_height_px) / 2;

  } else {

    int width  = a->child_width_fixed  ?: 100;
    int height = a->child_height_fixed ?: 100;

    a->xentries = GLW_MAX(1, rc0.rc_width  / width);
    a->yentries = GLW_MAX(1, rc0.rc_height / height);

    a->child_width_px  = width;
    a->child_height_px = height;

    int xspill = rc0.rc_width  - (a->xentries * width);
    int yspill = rc0.rc_height - (a->yentries * height);

    xspacing = xspill / (a->xentries + 1);
    yspacing = yspill / (a->yentries + 1);
  }

  if(a->saved_height != rc0.rc_height) {
    a->saved_height = rc0.rc_height;
    a->page_size = rc0.rc_height;
    a->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      a->scroll_to_me = w->glw_focused;
  }


  a->current_pos = GLW_MAX(0, GLW_MIN(a->current_pos,
				      a->total_size - a->page_size));
  a->filtered_pos = GLW_LP(6, a->filtered_pos, a->current_pos);

  rc0.rc_width  = a->child_width_px;
  rc0.rc_height = a->child_height_px;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    c->glw_parent_pos_y = ypos;
    c->glw_parent_pos_x = column * (xspacing + a->child_width_px) + xpos;

    if(ypos - a->filtered_pos > -height &&
       ypos - a->filtered_pos <  height * 2)
      glw_layout0(c, &rc0);

    if(c == a->scroll_to_me) {
      a->scroll_to_me = NULL;
     
      if(ypos - a->filtered_pos < 0) {
	a->current_pos = ypos;
	a->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(ypos - a->filtered_pos + rc0.rc_height > height) {
	a->current_pos = ypos + rc0.rc_height - height;
	a->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    if(column == 0) {
      c->glw_flags2 |= GLW2_LEFT_EDGE;
    } else {
      c->glw_flags2 &= ~GLW2_LEFT_EDGE;
    }

    if(column == a->xentries - 1) {
      c->glw_flags2 |= GLW2_RIGHT_EDGE;
    } else {
      c->glw_flags2 &= ~GLW2_RIGHT_EDGE;
    }

    if(topedge) {
      c->glw_flags2 |= GLW2_TOP_EDGE;
    } else {
      c->glw_flags2 &= ~GLW2_TOP_EDGE;
    }

    c->glw_flags2 &= ~GLW2_BOTTOM_EDGE; // Will be set later

    column++;
    if(column == a->xentries) {
      ypos += a->child_height_px + yspacing;
      column = 0;
      topedge = 0;
    }
  }

  if(column != 0)
    ypos += a->child_height_px;


  last = TAILQ_LAST(&w->glw_childs, glw_queue);
  if(last != NULL) {
    last->glw_flags2 |= GLW2_BOTTOM_EDGE | GLW2_RIGHT_EDGE;
    c = last;
    while((c = TAILQ_PREV(c, glw_queue, glw_parent_link)) != NULL) {
      if(c->glw_parent_pos_y == last->glw_parent_pos_y)
	c->glw_flags2 |= GLW2_BOTTOM_EDGE;
      else
	break;
    }
  }

 if(a->total_size != ypos) {
    a->total_size = ypos;
    a->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(a->w.glw_flags & GLW_UPDATE_METRICS)
    glw_array_update_metrics(a);
}


/**
 *
 */
static void
glw_array_render(glw_t *w, glw_rctx_t *rc)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;
  glw_rctx_t rc0, rc1, rc2, rc3;
  int t, b, height;
  float y;

  if(rc->rc_alpha < 0.01f)
    return;

  rc0 = *rc;
  glw_reposition(&rc0, a->margin_left, rc->rc_height - a->margin_top,
		 rc->rc_width  - a->margin_right, a->margin_bottom);

  glw_store_matrix(w, &rc0);
  rc1 = rc0;

  glw_reposition(&rc1,
		 a->border_left, rc->rc_height - a->border_top,
		 rc->rc_width  - a->border_right, a->border_bottom);

  height = rc1.rc_height;

  rc2 = rc1;
  
  glw_Translatef(&rc2, 0, 2.0f * a->filtered_pos / height, 0);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    y = c->glw_parent_pos_y - a->filtered_pos;

    if(y + a->child_height_px < 0 || y > height) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    if(y < 0)
      t = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_TOP, 0);
    else
      t = -1;

    if(y + a->child_height_px > height)
      b = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_BOTTOM, 0);
    else
      b = -1;

    rc3 = rc2;
    glw_reposition(&rc3,
		   c->glw_parent_pos_x,
		   height - c->glw_parent_pos_y,
		   c->glw_parent_pos_x + a->child_width_px,
		   height - c->glw_parent_pos_y - a->child_height_px);

    glw_render0(c, &rc3);

    if(t != -1)
      glw_clip_disable(w->glw_root, &rc0, t);
    if(b != -1)
      glw_clip_disable(w->glw_root, &rc0, b);

  }
}


/**
 *
 */
static void
glw_array_scroll(glw_array_t *a, glw_scroll_t *gs)
{
  a->current_pos = GLW_MAX(gs->value * (a->total_size - a->page_size), 0);
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

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_array_layout(a, rc);
    return 0;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    a->scroll_to_me = extra;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    a->num_visible_childs++;
    break;


  case GLW_SIGNAL_CHILD_DESTROYED:
    if(a->scroll_to_me == extra)
      a->scroll_to_me = NULL;
  case GLW_SIGNAL_CHILD_HIDDEN:
    a->num_visible_childs--;
    break;

  case GLW_SIGNAL_POINTER_EVENT:
    gpe = extra;

    if(gpe->type == GLW_POINTER_SCROLL) {
      a->current_pos += a->page_size * gpe->delta_y;
      a->w.glw_flags |= GLW_UPDATE_METRICS;
    }
    break;

  case GLW_SIGNAL_SCROLL:
    glw_array_scroll(a, extra);
    break;

  case GLW_SIGNAL_EVENT_BUBBLE:
    w->glw_flags2 &= ~GLW2_FLOATING_FOCUS;
    break;

  }
  return 0;
}


/**
 *
 */
static void
glw_array_ctor(glw_t *w)
{
  w->glw_flags2 |= GLW2_FLOATING_FOCUS;
}

/**
 *
 */
static void 
glw_array_set(glw_t *w, va_list ap)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_CHILD_HEIGHT:
      a->child_height_fixed = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_WIDTH:
      a->child_width_fixed  = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_TILES_X:
      a->child_tiles_x = va_arg(ap, int);
      break;
    case GLW_ATTRIB_CHILD_TILES_Y:
      a->child_tiles_y = va_arg(ap, int);
      break;
    case GLW_ATTRIB_X_SPACING:
      a->xspacing = va_arg(ap, int);
      break;
    case GLW_ATTRIB_Y_SPACING:
      a->yspacing = va_arg(ap, int);
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
static int
glw_array_get_num_children_x(glw_t *w)
{
  glw_array_t *a = (glw_array_t *)w;
  return a->xentries;
}


/**
 *
 */
static void
set_margin(glw_t *w, const int16_t *v)
{
  glw_array_t *a = (glw_array_t *)w;
  a->margin_left   = v[0];
  a->margin_top    = v[1];
  a->margin_right  = v[2];
  a->margin_bottom = v[3];
}


/**
 *
 */
static void
set_border(glw_t *w, const int16_t *v)
{
  glw_array_t *a = (glw_array_t *)w;
  a->border_left   = v[0];
  a->border_top    = v[1];
  a->border_right  = v[2];
  a->border_bottom = v[3];
}


/**
 *
 */
static glw_class_t glw_array = {
  .gc_name = "array",
  .gc_instance_size = sizeof(glw_array_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_ARRAY,
  .gc_render = glw_array_render,
  .gc_ctor = glw_array_ctor,
  .gc_set = glw_array_set,
  .gc_signal_handler = glw_array_callback,
  .gc_get_num_children_x = glw_array_get_num_children_x,
  .gc_set_margin = set_margin,
  .gc_set_border = set_border,
};

GLW_REGISTER_CLASS(glw_array);
