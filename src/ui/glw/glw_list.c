/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include "glw_scroll.h"
#include "glw_navigation.h"

typedef struct glw_list {
  glw_t w;

  float child_aspect;

  int16_t saved_height;
  int16_t saved_width;
  int16_t spacing;

  int16_t padding[4];

  glw_scroll_control_t gsc;

} glw_list_t;


typedef struct glw_list_item {
  float pos;
  int16_t height;
  int16_t width;
  char inst;
} glw_list_item_t;


/**
 *
 */
static void
glw_list_layout_y(glw_t *w, const glw_rctx_t *rc)
{
  glw_list_t *l = (glw_list_t *)w;
  glw_t *c;
  int ypos;

  glw_rctx_t rc0 = *rc;

  glw_reposition(&rc0, l->padding[0], rc->rc_height - l->padding[1],
		 rc->rc_width - l->padding[2], l->padding[3]);

  const int bottom_scroll_pos = rc0.rc_height - l->gsc.scroll_threshold_post;

  if(l->saved_height != rc0.rc_height) {
    l->saved_height = rc0.rc_height;
    l->gsc.page_size = rc0.rc_height;
    l->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      l->gsc.scroll_to_me = w->glw_focused;
  }

  if(l->gsc.scroll_to_me != NULL) {

    ypos = l->gsc.scroll_threshold_pre;
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c->glw_flags & GLW_HIDDEN)
        continue;

      int f = glw_filter_constraints(c);
      int height;

      if(f & GLW_CONSTRAINT_Y) {
        height = c->glw_req_size_y;
      } else {
        height = rc0.rc_width / 10;
      }

      if(c == l->gsc.scroll_to_me) {

        int screen_pos = ypos - l->gsc.rounded_pos;
        if(screen_pos < l->gsc.scroll_threshold_pre) {
          l->gsc.target_pos = ypos - l->gsc.scroll_threshold_pre;
          if(glw_is_focused(w))
            l->w.glw_flags |= GLW_UPDATE_METRICS;
          glw_schedule_refresh(w->glw_root, 0);
        } else if(screen_pos + height > bottom_scroll_pos) {
          l->gsc.target_pos = ypos + height - bottom_scroll_pos;
          if(glw_is_focused(w))
            l->w.glw_flags |= GLW_UPDATE_METRICS;
          glw_schedule_refresh(w->glw_root, 0);
        }
      }

      ypos += height;
      ypos += l->spacing;
    }
    l->gsc.scroll_to_me = NULL;
  }

  glw_scroll_layout(&l->gsc, w, rc->rc_height);

  ypos = l->gsc.scroll_threshold_pre;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c);

    if(f & GLW_CONSTRAINT_Y) {
      rc0.rc_height = c->glw_req_size_y;
    } else {
      rc0.rc_height = rc0.rc_width / 10;
    }

    glw_list_item_t *cd = glw_parent_data(c, glw_list_item_t);

    if(cd->inst) {
      cd->pos = ypos;
      cd->inst = 0;
    } else {
      glw_lp(&cd->pos, w->glw_root, ypos, 0.25);
    }

    cd->height = rc0.rc_height;

    if(ypos - l->gsc.rounded_pos > -rc->rc_height &&
       ypos - l->gsc.rounded_pos <  rc->rc_height * 2)
      glw_layout0(c, &rc0);

    ypos += rc0.rc_height;
    ypos += l->spacing;
  }

  if(l->gsc.total_size != ypos) {
    l->gsc.total_size = ypos;
    l->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_scroll_update_metrics(&l->gsc, w);
}



/**
 *
 */
static void
glw_list_layout_x(glw_t *w, const glw_rctx_t *rc)
{
  glw_list_t *l = (glw_list_t *)w;
  glw_t *c;
  int xpos = l->gsc.scroll_threshold_pre;;
  glw_rctx_t rc0 = *rc;

  glw_reposition(&rc0, l->padding[0], rc->rc_height - l->padding[1],
		 rc->rc_width - l->padding[2], l->padding[3]);
  int width0 = rc0.rc_width -
    l->gsc.scroll_threshold_pre -
    l->gsc.scroll_threshold_post;

  if(l->saved_width != rc0.rc_width) {
    l->saved_width = rc0.rc_width;
    l->gsc.page_size = rc0.rc_width;
    l->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      l->gsc.scroll_to_me = w->glw_focused;
  }

  l->gsc.target_pos = GLW_MAX(0, GLW_MIN(l->gsc.target_pos,
                                         l->gsc.total_size - l->gsc.page_size));

  if(fabsf(l->gsc.target_pos - l->gsc.filtered_pos) > rc->rc_width * 2) {
    l->gsc.filtered_pos = l->gsc.target_pos;
  } else {
    glw_lp(&l->gsc.filtered_pos, w->glw_root, l->gsc.target_pos, 0.25);
  }

  l->gsc.rounded_pos = l->gsc.filtered_pos;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c);

    if(f & GLW_CONSTRAINT_X) {
      rc0.rc_width = c->glw_req_size_x;
    } else {
      rc0.rc_width = rc0.rc_height;
    }

    glw_list_item_t *cd = glw_parent_data(c, glw_list_item_t);

    cd->pos = xpos;
    cd->width = rc0.rc_width;

    if(xpos - l->gsc.rounded_pos > -width0 &&
       xpos - l->gsc.rounded_pos <  width0 * 2) {
      glw_layout0(c, &rc0);
    }

    if(c == l->gsc.scroll_to_me) {
      l->gsc.scroll_to_me = NULL;
      if(xpos - l->gsc.rounded_pos < l->gsc.scroll_threshold_pre) {
	l->gsc.target_pos = xpos - l->gsc.scroll_threshold_pre;
        if(glw_is_focused(w))
          l->w.glw_flags |= GLW_UPDATE_METRICS;
      } else if(xpos - l->gsc.rounded_pos + rc0.rc_width > width0) {
	l->gsc.target_pos = xpos + rc0.rc_width - width0;
        if(glw_is_focused(w))
          l->w.glw_flags |= GLW_UPDATE_METRICS;
      }
    }

    xpos += rc0.rc_width;
    xpos += l->spacing;
  }

  xpos += l->gsc.scroll_threshold_post;

  if(l->gsc.total_size != xpos) {
    l->gsc.total_size = xpos;
    l->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(l->w.glw_flags & GLW_UPDATE_METRICS)
    glw_scroll_update_metrics(&l->gsc, w);
}


/**
 *
 */
static void
glw_list_y_render_one(glw_list_t *l, glw_t *c, int width, int height,
                      const glw_rctx_t *rc0, const glw_rctx_t *rc1,
                      int clip_top, int clip_bottom)
{
  glw_list_item_t *cd = glw_parent_data(c, glw_list_item_t);

  int ct, cb;
  glw_root_t *gr = l->w.glw_root;
  float y = cd->pos - l->gsc.rounded_pos;
  glw_rctx_t rc2;

  if((y + cd->height < 0 || y > height)) {
    c->glw_flags |= GLW_CLIPPED;
    return;
  } else {
    c->glw_flags &= ~GLW_CLIPPED;
  }

  ct = cb = -1;

  if(y < clip_top) {
    const float k = (float)clip_top / rc0->rc_height;
    ct = glw_clip_enable(gr, rc0, GLW_CLIP_TOP, k,
                         l->gsc.clip_alpha, 1.0f - l->gsc.clip_blur);
  }

  if(y + cd->height > rc0->rc_height - clip_bottom) {
    const float k = (float)clip_bottom / rc0->rc_height;
    cb = glw_clip_enable(gr, rc0, GLW_CLIP_BOTTOM, k,
                         l->gsc.clip_alpha, 1.0f - l->gsc.clip_blur);
  }

  rc2 = *rc1;
  glw_reposition(&rc2,
                 0,
                 height - cd->pos,
                 width,
                 height - cd->pos - cd->height);

  glw_render0(c, &rc2);

  if(ct != -1)
    glw_clip_disable(gr, ct);
  if(cb != -1)
    glw_clip_disable(gr, cb);
}


/**
 *
 */
static void
glw_list_render_y(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_list_t *l = (glw_list_t *)w;
  glw_rctx_t rc0, rc1;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  glw_reposition(&rc0, l->padding[0], rc->rc_height - l->padding[1],
		 rc->rc_width  - l->padding[2], l->padding[3]);

  glw_store_matrix(w, &rc0);

  rc1 = rc0;
  glw_reposition(&rc1, 0,
                 rc1.rc_height - l->gsc.clip_offset_pre,
		 rc1.rc_width,
                 l->gsc.clip_offset_post);

  glw_store_matrix(w, &rc1);


  if(rc->rc_alpha < GLW_ALPHA_EPSILON)
    return;

  const int clip_top    = l->gsc.clip_offset_pre - 1;
  const int clip_bottom = l->gsc.clip_offset_post - 1;

  rc1 = rc0;
  glw_Translatef(&rc1, 0, 2.0f * l->gsc.rounded_pos / rc0.rc_height, 0);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_list_y_render_one(l, c, rc0.rc_width, rc0.rc_height, &rc0, &rc1,
                          clip_top, clip_bottom);
  }
}





/**
 *
 */
static void
glw_list_render_x(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_list_t *l = (glw_list_t *)w;
  glw_rctx_t rc0, rc1, rc2;
  int lc, rclip, lf, rf, height, width;
  float x;


  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  glw_reposition(&rc0, l->padding[0], rc->rc_height - l->padding[1],
		 rc->rc_width  - l->padding[2], l->padding[3]);

  height = rc0.rc_height;
  width = rc0.rc_width;

  glw_store_matrix(w, &rc0);

  if(rc->rc_alpha < GLW_ALPHA_EPSILON)
    return;

  rc1 = rc0;

  glw_Translatef(&rc1, -2.0f * l->gsc.rounded_pos / width, 0, 0);
  
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    glw_list_item_t *cd = glw_parent_data(c, glw_list_item_t);

    x = cd->pos - l->gsc.rounded_pos;
    if(x + cd->width < 0 || x > width) {
      c->glw_flags |= GLW_CLIPPED;
      continue;
    } else {
      c->glw_flags &= ~GLW_CLIPPED;
    }

    lc = rclip = lf = rf = -1;

    if(x < 0)
      lc = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_LEFT, 0, 0, 1);

    if(x + cd->width > width)
      rclip = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_RIGHT, 0, 0, 1);

    rc2 = rc1;
    glw_reposition(&rc2,
		   cd->pos,
		   height,
		   cd->pos + cd->width,
		   0);
    
    glw_render0(c, &rc2);

    if(lc != -1)
      glw_clip_disable(w->glw_root, lc);
    if(rclip != -1)
      glw_clip_disable(w->glw_root, rclip);
  }
}



/**
 * Try to find a child widget that's visible. This is used when scrolling
 * to maintain focus on screen
 */
static glw_t *
glw_list_find_visible_child(glw_t *w)
{
  glw_list_t *l = (glw_list_t *)w;
  const int top = l->gsc.target_pos + l->gsc.scroll_threshold_pre;
  const int bottom = l->gsc.target_pos + l->gsc.page_size -
    l->gsc.scroll_threshold_pre;
  glw_t *c = l->w.glw_focused;

  if(c == NULL)
    return NULL;

  if(glw_parent_data(c, glw_list_item_t)->pos < top) {

    while(c != NULL && glw_parent_data(c, glw_list_item_t)->pos < top)
      c = glw_next_widget(c);

    if(c != NULL && glw_get_focusable_child(c) == NULL)
      c = glw_next_widget(c);

  } else if(glw_parent_data(c, glw_list_item_t)->pos > bottom) {

    while(c != NULL && glw_parent_data(c, glw_list_item_t)->pos > bottom)
      c = glw_prev_widget(c);

    if(c != NULL && glw_get_focusable_child(c) == NULL)
      c = glw_prev_widget(c);

  }
  return c;
}


/**
 *
 */
static void
glw_list_scroll(glw_list_t *l, glw_scroll_t *gs)
{
  glw_scroll_handle_scroll(&l->gsc, &l->w, gs);
}


/**
 * This is a helper to make sure we can show items in list that are not
 * focusable even if they are at the top
 */
static void
scroll_to_me(glw_list_t *l, glw_t *c)
{
  glw_t *d = c, *e = c;
  if(c == NULL)
    return;

  while(1) {
    d = TAILQ_PREV(d, glw_queue, glw_parent_link);
    if(d == NULL) {
      c = e;
      break;
    }

    if(d->glw_flags & GLW_HIDDEN)
      continue;
    if(glw_is_child_focusable(d))
      break;
    e = d;
  }
  l->gsc.scroll_to_me = c;
  glw_schedule_refresh(l->w.glw_root, 0);
}


/**
 *
 */
static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_list_t *l = (void *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(l, extra);
    l->gsc.suggest_cnt = 0;
    w->glw_flags &= ~GLW_FLOATING_FOCUS;
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(l->gsc.scroll_to_me == extra)
      l->gsc.scroll_to_me = NULL;
    if(l->gsc.suggested == extra)
      l->gsc.suggested = NULL;

    if(extra == TAILQ_FIRST(&w->glw_childs) && glw_next_widget(extra) == NULL) {
      // Last item went away, make sure to reset
      l->gsc.target_pos = 0;
      l->gsc.filtered_pos = 0;
      w->glw_flags |= GLW_FLOATING_FOCUS;
      l->gsc.suggest_cnt = 1;
    }
    break;

  case GLW_SIGNAL_SCROLL:
    glw_list_scroll(l, extra);
    break;

  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    c = extra;
    glw_parent_data(c, glw_list_item_t)->inst = 1;
    break;

  case GLW_SIGNAL_FHP_PATH_CHANGED:
    if(!glw_is_focused(w))
      l->gsc.suggest_cnt = 1;
    break;
  }
  return 0;
}


/**
 *
 */
static int
glw_list_set_float(glw_t *w, glw_attribute_t attrib, float value,
                   glw_style_t *gs)
{
  glw_list_t *l = (glw_list_t *)w;
  switch(attrib) {

  case GLW_ATTRIB_CHILD_ASPECT:
    if(l->child_aspect == value)
      return 0;

    l->child_aspect = value;
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_list_set_int(glw_t *w, glw_attribute_t attrib, int value,
                 glw_style_t *gs)
{
  glw_list_t *l = (glw_list_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_SPACING:
    if(l->spacing == value)
      return 0;

    l->spacing = value;
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
glw_list_y_ctor(glw_t *w)
{
  glw_list_t *l = (void *)w;
  l->child_aspect = 20;
  l->gsc.suggest_cnt = 1;
  w->glw_flags |= GLW_FLOATING_FOCUS;
}


/**
 *
 */
static void
glw_list_x_ctor(glw_t *w)
{
  glw_list_t *l = (void *)w;
  
  l->child_aspect = 1;
  l->gsc.suggest_cnt = 1;
  w->glw_flags |= GLW_FLOATING_FOCUS;
}


/**
 *
 */
static void
glw_list_suggest_focus(glw_t *w, glw_t *c)
{
  glw_list_t *l = (glw_list_t *)w;
  glw_scroll_suggest_focus(&l->gsc, w, c);
}


/**
 *
 */
static int
glw_list_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                     glw_style_t *gs)
{
  glw_list_t *l = (glw_list_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_PADDING:
    return glw_attrib_set_int16_4(l->padding, v);
  default:
    return -1;
  }
}


/**
 *
 */
static int
glw_list_set_float_unresolved(glw_t *w, const char *a, float value,
                              glw_style_t *gs)
{
  glw_list_t *l = (glw_list_t *)w;

  return glw_scroll_set_float_attributes(&l->gsc, a, value);

}

/**
 *
 */
static int
glw_list_set_int_unresolved(glw_t *w, const char *a, int value,
                            glw_style_t *gs)
{
  glw_list_t *l = (glw_list_t *)w;

  return glw_scroll_set_int_attributes(&l->gsc, a, value);
}


static int
handle_pointer_event(struct glw *w, const glw_pointer_event_t *gpe)
{
  glw_list_t *l = (glw_list_t *)w;
  return glw_scroll_handle_pointer_event(&l->gsc, w, gpe);
}

static int
handle_pointer_event_filter(struct glw *w, const glw_pointer_event_t *gpe)
{
  glw_list_t *l = (glw_list_t *)w;
  return glw_scroll_handle_pointer_event_filter(&l->gsc, w, gpe);
}



static glw_class_t glw_list_y = {
  .gc_name = "list_y",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_parent_data_size = sizeof(glw_list_item_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_layout = glw_list_layout_y,
  .gc_render = glw_list_render_y,
  .gc_set_int = glw_list_set_int,
  .gc_set_float = glw_list_set_float,
  .gc_ctor = glw_list_y_ctor,
  .gc_signal_handler = glw_list_callback,
  .gc_suggest_focus = glw_list_suggest_focus,
  .gc_set_int16_4 = glw_list_set_int16_4,
  .gc_pointer_event = handle_pointer_event,
  .gc_pointer_event_filter = handle_pointer_event_filter,
  .gc_bubble_event = glw_navigate_vertical,
  .gc_set_int_unresolved = glw_list_set_int_unresolved,
  .gc_set_float_unresolved = glw_list_set_float_unresolved,
  .gc_find_visible_child = glw_list_find_visible_child,
};

GLW_REGISTER_CLASS(glw_list_y);



static glw_class_t glw_list_x = {
  .gc_name = "list_x",
  .gc_instance_size = sizeof(glw_list_t),
  .gc_parent_data_size = sizeof(glw_list_item_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,

  .gc_layout = glw_list_layout_x,
  .gc_render = glw_list_render_x,
  .gc_set_int = glw_list_set_int,
  .gc_set_float = glw_list_set_float,
  .gc_ctor = glw_list_x_ctor,
  .gc_signal_handler = glw_list_callback,
  .gc_suggest_focus = glw_list_suggest_focus,
  .gc_set_int16_4 = glw_list_set_int16_4,
  .gc_bubble_event = glw_navigate_horizontal,
  .gc_set_int_unresolved = glw_list_set_int_unresolved,
  .gc_set_float_unresolved = glw_list_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_list_x);
