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

  float center_y, center_y_target, center_y_max;
  float center_x, center_x_target, center_x_max;

  glw_t *scroll_to_me;

  glw_t *suggested;
  int suggest_cnt;

  glw_slider_metrics_t metrics;

  int size_y;
  int size_x;

  prop_t *append_prop;

  int child_num;
  int child_num_append_req;
  int append_thres;

} glw_list_t;

#define glw_parent_size_x glw_parent_misc[0]
#define glw_parent_size_y glw_parent_misc[1]

/**
 *
 */
static void
glw_list_update_metrics(glw_list_t *l, float max, float val)
{
  float v;
  int do_update = 0;

  l->w.glw_flags &= ~GLW_UPDATE_METRICS;

  v = 2.0 / (max + 2.0);
  if(v != l->metrics.knob_size) {
    do_update = 1;
    l->metrics.knob_size = v;
  }
  
  v = max > 0 ? val / max : 0;

  if(v != l->metrics.position) {
    do_update = 1;
    l->metrics.position = v;
  }
  
  if(!do_update)
    return;

  if(max > 0 && !(l->w.glw_flags & GLW_CAN_SCROLL)) {
    l->w.glw_flags |= GLW_CAN_SCROLL;
    glw_signal0(&l->w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
    
  } else if(max == 0 && l->w.glw_flags & GLW_CAN_SCROLL) {
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
  float y = 0;
  float sa, size_y, t, vy;
  glw_rctx_t rc0 = *rc;

  if(l->size_y != rc->rc_size_y) {
    l->size_y = rc->rc_size_y;

    if(w->glw_focused != NULL)
      l->scroll_to_me = w->glw_focused;
  }

  sa = rc->rc_size_x / rc->rc_size_y;

  t = GLW_MIN(GLW_MAX(0, l->center_y_target), l->center_y_max);
  t = 2 * floorf(t / 2 * l->size_y) / l->size_y;
  l->center_y = GLW_LP(6, l->center_y, t);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

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
    c->glw_norm_weight = size_y;

    if(c->glw_parent_pos.y - c->glw_parent_size_y <= 1.5f &&
       c->glw_parent_pos.y + c->glw_parent_size_y >= -1.5f) {

      c->glw_parent_pos.z = 0;
      c->glw_parent_pos.x = 0;

      c->glw_parent_scale.y = size_y;
      c->glw_parent_scale.z = size_y;
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

  if(y > 0 && t > 0.75 * y && l->append_prop != NULL &&
     l->child_num != l->child_num_append_req) {

    if(l->append_thres == 5) {
      l->child_num_append_req = l->child_num;

      event_t  *e = event_create_type(EVENT_APPEND_REQUEST);
      prop_send_ext_event(l->append_prop, e);
      event_unref(e);
    } else {
      l->append_thres++;
    }
  }
  return 0;
}



/**
 *
 */
static int
glw_list_layout_x(glw_list_t *l, glw_rctx_t *rc)
{
  glw_t *c, *w = &l->w;
  float x = 0;
  float isa, size_x, t, vx;
  glw_rctx_t rc0 = *rc;

  if(l->size_x != rc->rc_size_x) {
    l->size_x = rc->rc_size_x;

    if(w->glw_focused != NULL)
      l->scroll_to_me = w->glw_focused;
  }

  isa = rc->rc_size_y / rc->rc_size_x;

  t = GLW_MIN(GLW_MAX(0, l->center_x_target), l->center_x_max);
  t = 2 * floorf(t / 2 * l->size_x) / l->size_x;
  l->center_x = GLW_LP(6, l->center_x, t);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

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
    c->glw_parent_scale.z = size_x;

    rc0.rc_size_x = rc->rc_size_x * size_x;

    c->glw_norm_weight = size_x;

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
  return 0;
}


/**
 *
 */
static void
glw_list_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;
  if(rc->rc_alpha < 0.01)
    return;

  glw_store_matrix(w, rc);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    if(c->glw_parent_pos.y - c->glw_parent_size_y <= 1.0f &&
       c->glw_parent_pos.y + c->glw_parent_size_y >= -1.0f &&
       c->glw_parent_pos.x - c->glw_parent_size_x <= 1.0f &&
       c->glw_parent_pos.x + c->glw_parent_size_x >= -1.0f) {
      rc0 = *rc;
      glw_render_TS(c, &rc0, rc);
      c->glw_flags &= ~GLW_FOCUS_BLOCKED;
    } else if(w->glw_focused != c) {
      c->glw_flags |= GLW_FOCUS_BLOCKED;
    }
  }
}


/**
 *
 */
static void
glw_list_render_x(glw_t *w, glw_rctx_t *rc)
{
  int l = glw_clip_enable(rc, GLW_CLIP_LEFT);
  int r = glw_clip_enable(rc, GLW_CLIP_RIGHT);
  glw_list_render(w, rc);
  glw_clip_disable(rc, l);
  glw_clip_disable(rc, r);
}

/**
 *
 */
static void
glw_list_render_y(glw_t *w, glw_rctx_t *rc)
{
  int t = glw_clip_enable(rc, GLW_CLIP_TOP);
  int b = glw_clip_enable(rc, GLW_CLIP_BOTTOM);
  glw_list_render(w, rc);
  glw_clip_disable(rc, t);
  glw_clip_disable(rc, b);
}

/**
 *
 */
static void
glw_list_scroll(glw_list_t *l, glw_scroll_t *gs)
{
  l->center_y_target = gs->value * l->center_y_max;
  l->center_x_target = gs->value * l->center_x_max;
}


/**
 *
 */
static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_list_t *l = (void *)w;
  glw_pointer_event_t *gpe;
  glw_t *c;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    l->scroll_to_me = extra;
    l->suggest_cnt = 0;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    c->glw_parent_size_x = 0.5;
    c->glw_parent_size_y = 0.5;

    c->glw_parent_scale.x = 1.0;
    c->glw_parent_scale.y = 1.0;
    c->glw_parent_scale.z = 1.0;

    l->child_num++;
    l->append_thres = 0;
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(l->scroll_to_me == extra)
      l->scroll_to_me = NULL;
    if(l->suggested == extra)
      l->suggested = NULL;

    l->child_num--;
    l->append_thres = 0;
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

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_focused == extra) {
      l->scroll_to_me = extra;
    }
    break;

  case GLW_SIGNAL_DESTROY:
    if(l->append_prop != NULL)
      prop_ref_dec(l->append_prop);
    break;
  }
  return 0;
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
static void 
glw_list_set(glw_t *w, va_list ap)
{
  glw_attribute_t attrib;
  glw_list_t *l = (void *)w;
  prop_t *p;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_CHILD_ASPECT:
      l->child_aspect = va_arg(ap, double);
      break;

    case GLW_ATTRIB_APPEND_EVENT_SINK:
      p = va_arg(ap, prop_t *);

      if(l->append_prop != NULL)
	prop_ref_dec(l->append_prop );

      l->append_prop = p;
      if(p != NULL)
	prop_ref_inc(p);
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
glw_list_set_x(glw_t *w, int init, va_list ap)
{
  glw_list_t *l = (void *)w;

  if(init)
    l->child_aspect = 1;
  glw_list_set(w, ap);
}

/**
 *
 */
static void
glw_list_set_y(glw_t *w, int init, va_list ap)
{
  glw_list_t *l = (void *)w;

  if(init)
    l->child_aspect = 20;
  glw_list_set(w, ap);
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
      glw_focus_set(c->glw_root, c, 1);
    l->suggest_cnt = 1;
  }
  l->suggested = c;
  l->suggest_cnt++;
}



static glw_class_t glw_list_x = {
  .gc_name = "list_x",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_child_orientation = GLW_ORIENTATION_HORIZONTAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,

  .gc_render = glw_list_render_x,
  .gc_set = glw_list_set_x,
  .gc_signal_handler = glw_list_callback_x,
  .gc_escape_score = 100,
  .gc_suggest_focus = glw_list_suggest_focus,
};

static glw_class_t glw_list_y = {
  .gc_name = "list_y",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS | 
  GLW_TRANSFORM_LR_TO_UD,
  .gc_child_orientation = GLW_ORIENTATION_VERTICAL,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_FOCUSED,
  .gc_nav_search_mode = GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING,

  .gc_render = glw_list_render_y,
  .gc_set = glw_list_set_y,
  .gc_signal_handler = glw_list_callback_y,
  .gc_escape_score = 100,
  .gc_suggest_focus = glw_list_suggest_focus,
};

GLW_REGISTER_CLASS(glw_list_x);
GLW_REGISTER_CLASS(glw_list_y);
