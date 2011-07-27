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

typedef struct glw_list {
  glw_t w;

  float child_aspect;

  float filtered_pos;
  int total_size;
  int current_pos;
  int page_size;

  glw_t *scroll_to_me;

  glw_t *suggested;
  int suggest_cnt;

  glw_slider_metrics_t metrics;

  int16_t saved_height;
  int16_t saved_width;
  int16_t spacing;

  int16_t padding_left;
  int16_t padding_right;
  int16_t padding_top;
  int16_t padding_bottom;

} glw_list_t;

#define glw_parent_height glw_parent_val[0].i32
#define glw_parent_width  glw_parent_val[1].i32
#define glw_parent_pos    glw_parent_val[2].f

/**
 *
 */
static void
glw_list_update_metrics(glw_list_t *l)
{
  float v;
  int do_update = 0;

  l->w.glw_flags &= ~GLW_UPDATE_METRICS;

  v = GLW_MIN(1.0f, (float)l->page_size / l->total_size);

  if(v != l->metrics.knob_size) {
    do_update = 1;
    l->metrics.knob_size = v;
  }
  
  v = GLW_MAX(0, (float)l->current_pos / (l->total_size - l->page_size));

  if(v != l->metrics.position) {
    do_update = 1;
    l->metrics.position = v;
  }
  
  if(!do_update)
    return;

  if(l->total_size > l->page_size && !(l->w.glw_flags & GLW_CAN_SCROLL)) {
    l->w.glw_flags |= GLW_CAN_SCROLL;
    glw_signal0(&l->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
    
  } else if(l->total_size <= l->page_size &&
	    l->w.glw_flags & GLW_CAN_SCROLL) {
    l->w.glw_flags &= ~GLW_CAN_SCROLL;
    glw_signal0(&l->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
  }

  glw_signal0(&l->w, GLW_SIGNAL_SLIDER_METRICS, &l->metrics);
}


/**
 *
 */
static int
glw_list_layout_y(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  int ypos = 0;
  glw_rctx_t rc0 = *rc;

  glw_reposition(&rc0, l->padding_left, rc->rc_height - l->padding_top,
		 rc->rc_width  - l->padding_right, l->padding_bottom);
  int height = rc0.rc_height;

  float IH = 1.0f / rc0.rc_height;

  if(l->saved_height != rc0.rc_height) {
    l->saved_height = rc0.rc_height;
    l->page_size = rc0.rc_height;
    l->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      l->scroll_to_me = w->glw_focused;
  }

  
  l->current_pos = GLW_MAX(0, GLW_MIN(l->current_pos,
				      l->total_size - l->page_size));
  l->filtered_pos = GLW_LP(6, l->filtered_pos, l->current_pos);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c->glw_flags);

    if(f & GLW_CONSTRAINT_Y) {
      rc0.rc_height = c->glw_req_size_y;
    } else {
      rc0.rc_height = rc0.rc_width / 10;
    }

    c->glw_parent_pos = ypos;
    c->glw_parent_height = rc0.rc_height;
    c->glw_norm_weight = rc0.rc_height * IH;
    

    if(ypos - l->filtered_pos > -height &&
       ypos - l->filtered_pos <  height * 2)
      glw_layout0(c, &rc0);

    if(c == l->scroll_to_me) {
      l->scroll_to_me = NULL;
     
      if(ypos - l->filtered_pos < 0) {
	l->current_pos = ypos;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(ypos - l->filtered_pos + rc0.rc_height > height) {
	l->current_pos = ypos + rc0.rc_height - height;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    ypos += rc0.rc_height;
    ypos += l->spacing;
  }

  if(l->total_size != ypos) {
    l->total_size = ypos;
    l->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_list_update_metrics(l);

  return 0;
}



/**
 *
 */
static int
glw_list_layout_x(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  int xpos = 0;
  glw_rctx_t rc0 = *rc;

  glw_reposition(&rc0, l->padding_left, rc->rc_height - l->padding_top,
		 rc->rc_width  - l->padding_right, l->padding_bottom);
  int width = rc0.rc_width;

  float IW = 1.0f / rc0.rc_width;

  if(l->saved_width != rc0.rc_width) {
    l->saved_width = rc0.rc_width;
    l->page_size = rc0.rc_width;
    l->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      l->scroll_to_me = w->glw_focused;
  }

  
  l->current_pos = GLW_MAX(0, GLW_MIN(l->current_pos,
				      l->total_size - l->page_size));
  l->filtered_pos = GLW_LP(6, l->filtered_pos, l->current_pos);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c->glw_flags);

    if(f & GLW_CONSTRAINT_X) {
      rc0.rc_width = c->glw_req_size_x;
    } else {
      rc0.rc_width = rc0.rc_height;
    }

    c->glw_parent_pos = xpos;
    c->glw_parent_width = rc0.rc_width;
    c->glw_norm_weight = rc0.rc_width * IW;
    

    if(xpos - l->filtered_pos > -width &&
       xpos - l->filtered_pos <  width * 2)
      glw_layout0(c, &rc0);

    if(c == l->scroll_to_me) {
      l->scroll_to_me = NULL;
     
      if(xpos - l->filtered_pos < 0) {
	l->current_pos = xpos;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(xpos - l->filtered_pos + rc0.rc_width > width) {
	l->current_pos = xpos + rc0.rc_width - width;
	l->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    xpos += rc0.rc_width;
    xpos += l->spacing;
  }

  if(l->total_size != xpos) {
    l->total_size = xpos;
    l->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_list_update_metrics(l);

  return 0;
}



/**
 *
 */
static void
glw_list_render_y(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_list_t *l = (glw_list_t *)w;
  glw_rctx_t rc0, rc1, rc2;
  int t, b, height, width;
  float y;

  if(rc->rc_alpha < 0.01f)
    return;


  rc0 = *rc;
  glw_reposition(&rc0, l->padding_left, rc->rc_height - l->padding_top,
		 rc->rc_width  - l->padding_right, l->padding_bottom);
  height = rc0.rc_height;
  width = rc0.rc_width;

  glw_store_matrix(w, &rc0);
  rc1 = rc0;

  glw_Translatef(&rc1, 0, 2.0f * l->filtered_pos / height, 0);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    y = c->glw_parent_pos - l->filtered_pos;
    if(y + c->glw_parent_height < 0 || y > height) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    if(y < 0)
      t = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_TOP, 0);
    else
      t = -1;

    if(y + c->glw_parent_height > height)
      b = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_BOTTOM, 0);
    else
      b = -1;

    rc2 = rc1;
    glw_reposition(&rc2,
		   0,
		   height - c->glw_parent_pos,
		   width,
		   height - c->glw_parent_pos - c->glw_parent_height);

    glw_render0(c, &rc2);

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
glw_list_render_x(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_list_t *l = (glw_list_t *)w;
  glw_rctx_t rc0, rc1, rc2;
  int lclip, rclip, height, width;
  float x;

  if(rc->rc_alpha < 0.01f)
    return;

  rc0 = *rc;
  glw_reposition(&rc0, l->padding_left, rc->rc_height - l->padding_top,
		 rc->rc_width  - l->padding_right, l->padding_bottom);
  height = rc0.rc_height;
  width = rc0.rc_width;

  glw_store_matrix(w, &rc0);
  rc1 = rc0;

  glw_Translatef(&rc1, -2.0f * l->filtered_pos / width, 0, 0);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    x = c->glw_parent_pos - l->filtered_pos;
    if(x + c->glw_parent_width < 0 || x > width) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    if(x < 0)
      lclip = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_LEFT, 0);
    else
      lclip = -1;

    if(x + c->glw_parent_width > width)
      rclip = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_RIGHT, 0);
    else
      rclip = -1;

    rc2 = rc1;
    glw_reposition(&rc2,
		   c->glw_parent_pos,
		   height,
		   c->glw_parent_pos + c->glw_parent_width,
		   0);
    
    glw_render0(c, &rc2);

    if(lclip != -1)
      glw_clip_disable(w->glw_root, &rc0, lclip);
    if(rclip != -1)
      glw_clip_disable(w->glw_root, &rc0, rclip);
  }
}


/**
 *
 */
static void
glw_list_scroll(glw_list_t *l, glw_scroll_t *gs)
{
  l->current_pos = GLW_MAX(gs->value * (l->total_size - l->page_size), 0);
}


/**
 *
 */
static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_list_t *l = (void *)w;
  glw_pointer_event_t *gpe;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    l->scroll_to_me = extra;
    l->suggest_cnt = 0;
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(l->scroll_to_me == extra)
      l->scroll_to_me = NULL;
    if(l->suggested == extra)
      l->suggested = NULL;

    break;

  case GLW_SIGNAL_POINTER_EVENT:
    gpe = extra;

    if(gpe->type == GLW_POINTER_SCROLL) {
      l->current_pos += l->page_size * gpe->delta_y;
      l->w.glw_flags |= GLW_UPDATE_METRICS;
    }
    break;

  case GLW_SIGNAL_SCROLL:
    glw_list_scroll(l, extra);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_focused == extra) {
      l->scroll_to_me = extra;
    }
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
static int
glw_list_callback_y(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_LAYOUT)
    return glw_list_layout_y((glw_list_t *)w, extra);
  else
    return glw_list_callback(w, opaque, signal, extra);
}


/**
 *
 */
static int
glw_list_callback_x(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_LAYOUT)
    return glw_list_layout_x((glw_list_t *)w, extra);
  else
    return glw_list_callback(w, opaque, signal, extra);
}

/**
 *
 */
static void 
glw_list_set(glw_t *w, va_list ap)
{
  glw_attribute_t attrib;
  glw_list_t *l = (void *)w;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_CHILD_ASPECT:
      l->child_aspect = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SPACING:
      l->spacing = va_arg(ap, int);
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
glw_list_y_ctor(glw_t *w)
{
  glw_list_t *l = (void *)w;
  
  l->child_aspect = 20;
  w->glw_flags2 |= GLW2_FLOATING_FOCUS;
}


/**
 *
 */
static void
glw_list_x_ctor(glw_t *w)
{
  glw_list_t *l = (void *)w;
  
  l->child_aspect = 1;
  w->glw_flags2 |= GLW2_FLOATING_FOCUS;
}

/**
 *
 */
static void
glw_list_suggest_focus(glw_t *w, glw_t *c)
{
  glw_list_t *l = (glw_list_t *)w;

  if(l->suggested == w->glw_focused || l->suggest_cnt > 0) {
    c = glw_focus_by_path(c);
    if(c != NULL)
      glw_focus_set(c->glw_root, c, GLW_FOCUS_SET_SUGGESTED);
    l->suggest_cnt = 1;
  }
  l->suggested = c;
  l->suggest_cnt++;
}


/**
 *
 */
static void
set_padding(glw_t *w, const int16_t *v)
{
  glw_list_t *l = (glw_list_t *)w;
  l->padding_left   = v[0];
  l->padding_top    = v[1];
  l->padding_right  = v[2];
  l->padding_bottom = v[3];
}


static glw_class_t glw_list_y = {
  .gc_name = "list_y",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS | 
  GLW_TRANSFORM_LR_TO_UD,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,

  .gc_render = glw_list_render_y,
  .gc_set = glw_list_set,
  .gc_ctor = glw_list_y_ctor,
  .gc_signal_handler = glw_list_callback_y,
  .gc_escape_score = 100,
  .gc_suggest_focus = glw_list_suggest_focus,
  .gc_set_padding = set_padding,
};

GLW_REGISTER_CLASS(glw_list_y);



static glw_class_t glw_list_x = {
  .gc_name = "list_x",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_child_orientation = GLW_ORIENTATION_HORIZONTAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,

  .gc_render = glw_list_render_x,
  .gc_set = glw_list_set,
  .gc_ctor = glw_list_x_ctor,
  .gc_signal_handler = glw_list_callback_x,
  .gc_escape_score = 100,
  .gc_suggest_focus = glw_list_suggest_focus,
  .gc_set_padding = set_padding,
};

GLW_REGISTER_CLASS(glw_list_x);
