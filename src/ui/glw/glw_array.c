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


typedef struct glw_array {
  glw_t w;

  glw_slider_metrics_t metrics;

  int child_tiles_x;
  int child_tiles_y;

  int child_width_px;
  int child_height_px;

  int xentries;

  int16_t saved_height;
  int16_t saved_width;

  int16_t xspacing;
  int16_t yspacing;

  int16_t scroll_threshold;

  int num_visible_childs;

  glw_scroll_control_t gsc;

} glw_array_t;

typedef struct glw_array_item {

  int pos_y;
  int pos_x;

  float pos_fx;
  float pos_fy;

  uint16_t width;
  uint16_t height;

  uint8_t just_inserted;
  int8_t col;

} glw_array_item_t;


/**
 *
 */
static int
grid_layout_row(glw_array_t *a, glw_rctx_t *rc,
                glw_t **rowvector, int *num_columnsp,
                int *req_row_heightp, int height)
{
  const int cols = *num_columnsp;
  if(cols == 0)
    return 0;

  int rh = *req_row_heightp;
  if(rh == INT32_MAX)
    rh = a->child_height_px;

  assert(rh >= 0);

  for(int i = 0; i < cols; i++) {
    glw_t *c = rowvector[i];

    glw_array_item_t *cd = glw_parent_data(c, glw_array_item_t);

    cd->height = rh;

    if(c == a->gsc.scroll_to_me) {
      const int ypos = cd->pos_y;
      const int screen_pos = ypos - a->gsc.rounded_pos;
      const int bottom_scroll_pos = height - a->gsc.scroll_threshold_post;


      a->gsc.scroll_to_me = NULL;
      if(screen_pos < a->gsc.scroll_threshold_pre) {
        a->gsc.target_pos = ypos - a->gsc.scroll_threshold_pre;
        if(glw_is_focused(&a->w))
          a->w.glw_flags |= GLW_UPDATE_METRICS;
        glw_schedule_refresh(a->w.glw_root, 0);
      } else if(screen_pos + rh > bottom_scroll_pos) {
        a->gsc.target_pos = ypos + rh - bottom_scroll_pos;
        if(glw_is_focused(&a->w))
          a->w.glw_flags |= GLW_UPDATE_METRICS;
        glw_schedule_refresh(a->w.glw_root, 0);
      }
    }

    if(cd->pos_fy - a->gsc.rounded_pos > -height &&
       cd->pos_fy - a->gsc.rounded_pos <  height * 2) {
      rc->rc_width = cd->width;
      rc->rc_height = cd->height;
      glw_layout0(c, rc);
    }
  }
  *num_columnsp = 0;
  *req_row_heightp = 0;
  return rh;
}


/**
 *
 */
static void
glw_array_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  float xspacing = 0, yspacing = 0;
  int rows;
  int xpos = 0, ypos = a->gsc.scroll_threshold_pre;

  const int height = rc0.rc_height;
  const int width = rc0.rc_width;

  if(a->child_tiles_x && a->child_tiles_y) {

    xspacing = a->xspacing;
    yspacing = a->yspacing;

    a->xentries = a->child_tiles_x;
    int yentries = a->child_tiles_y;

    if(yentries == 0) {
      yentries = a->xentries * height / (width ?: 1);
    } else if(a->xentries == 0) {
      a->xentries = yentries * width / (height ?: 1);
    }

    a->child_width_px  = (rc0.rc_width - (a->xentries - 1) * xspacing) /
      (a->xentries ?: 1);

    a->child_height_px = (rc0.rc_height - (yentries - 1) * yspacing) /
      (yentries ?: 1);

    rows = (a->num_visible_childs - 1) / a->child_tiles_x + 1;

    if(w->glw_alignment == LAYOUT_ALIGN_CENTER && rows < a->child_tiles_y)
      ypos = (a->child_tiles_y - rows) * (yspacing + a->child_height_px) / 2;

  } else if(a->child_tiles_x) {

    xspacing = a->xspacing;
    yspacing = a->yspacing;

    a->xentries = a->child_tiles_x;

    a->child_width_px  = (rc0.rc_width - (a->xentries - 1) * xspacing) /
      (a->xentries ?: 1);

    a->child_height_px = a->child_width_px;

  } else {

    int width  = 100;
    int height = 100;

    a->xentries = GLW_MAX(1, rc0.rc_width  / width);
    int yentries = GLW_MAX(1, rc0.rc_height / height);

    a->child_width_px  = width;
    a->child_height_px = height;

    int xspill = rc0.rc_width  - (a->xentries * width);
    int yspill = rc0.rc_height - (yentries * height);

    xspacing = xspill / (a->xentries + 1);
    yspacing = yspill / (yentries + 1);
  }

  if(a->saved_height != rc0.rc_height) {
    a->saved_height = rc0.rc_height;
    a->gsc.page_size = rc0.rc_height;
    a->w.glw_flags |= GLW_UPDATE_METRICS;

    if(w->glw_focused != NULL)
      a->gsc.scroll_to_me = w->glw_focused;
  }


  glw_scroll_layout(&a->gsc, w, rc->rc_height);

  glw_t **rowvector = alloca(a->xentries * sizeof(glw_t *));
  int column = 0;
  int req_row_height = 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    glw_array_item_t *cd = glw_parent_data(c, glw_array_item_t);

    if(c->glw_flags & GLW_CONSTRAINT_D) {
      ypos += grid_layout_row(a, &rc0, rowvector, &column,
                              &req_row_height, height);

      cd->width = width;
      if(c->glw_flags & GLW_CONSTRAINT_Y)
	req_row_height = glw_req_height(c);

      cd->col = -1;

    } else {

      if(column == a->xentries) {

        ypos += a->yspacing + grid_layout_row(a, &rc0, rowvector, &column,
                                              &req_row_height, height);
        req_row_height = 0;
        column = 0;
      }

      cd->width = a->child_width_px;
      cd->col = column;

      if(c->glw_flags & GLW_CONSTRAINT_Y) {
        const int rh = glw_req_height(c);
        req_row_height = GLW_MAX(req_row_height, rh);
      } else if(c->glw_flags & GLW_CONSTRAINT_W && c->glw_req_weight < 0) {
        const int h = a->child_width_px / -c->glw_req_weight;
        req_row_height = GLW_MAX(req_row_height, h);
      } else {
        req_row_height = INT32_MAX;
      }
    }

    cd->pos_y = ypos;
    cd->pos_x = column * (xspacing + a->child_width_px) + xpos;
    cd->height = rc0.rc_height;

    if(cd->just_inserted) {
      cd->pos_fy = cd->pos_y;
      cd->pos_fx = cd->pos_x;
      cd->just_inserted = 0;
    } else {
      glw_lp(&cd->pos_fy, w->glw_root, cd->pos_y, 0.25);
      glw_lp(&cd->pos_fx, w->glw_root, cd->pos_x, 0.25);
    }

    rowvector[column] = c;
    column++;

    if(c->glw_flags & GLW_CONSTRAINT_D) {
      ypos += grid_layout_row(a, &rc0, rowvector, &column,
                              &req_row_height, height);
    }

  }

  ypos += grid_layout_row(a, &rc0, rowvector, &column,
                          &req_row_height, height);

  if(a->gsc.total_size != ypos) {
    a->gsc.total_size = ypos;
    a->w.glw_flags |= GLW_UPDATE_METRICS;
  }

  if(a->w.glw_flags & GLW_UPDATE_METRICS)
    glw_scroll_update_metrics(&a->gsc, w);
}


/**
 *
 */
static void
glw_array_render_one(glw_array_t *a, glw_t *c, int width, int height,
		     const glw_rctx_t *rc0, const glw_rctx_t *rc2,
                     int clip_top, int clip_bottom)
{
  glw_rctx_t rc3;
  glw_array_item_t *cd = glw_parent_data(c, glw_array_item_t);
  const float y = cd->pos_fy - a->gsc.rounded_pos;
  int ct, cb;
  glw_root_t *gr = a->w.glw_root;
  int ch = cd->height;
  int cw = cd->width;

  if(y + ch * 2 < 0 || y - ch > height) {
    c->glw_flags |= GLW_CLIPPED;
    return;
  } else {
    c->glw_flags &= ~GLW_CLIPPED;
  }

  ct = cb = -1;

  if(y < clip_top) {
    const float k = (float)clip_top / rc0->rc_height;
    ct = glw_clip_enable(gr, rc0, GLW_CLIP_TOP, k,
                         a->gsc.clip_alpha, 1.0f - a->gsc.clip_blur);
  }

  if(y + cd->height > rc0->rc_height - clip_bottom) {
    const float k = (float)clip_bottom / rc0->rc_height;
    cb = glw_clip_enable(gr, rc0, GLW_CLIP_BOTTOM, k,
                         a->gsc.clip_alpha, 1.0f - a->gsc.clip_blur);
  }
  rc3 = *rc2;
  glw_reposition(&rc3,
		 cd->pos_fx,
		 height - cd->pos_fy,
		 cd->pos_fx + cw,
		 height - cd->pos_fy - ch);

  glw_render0(c, &rc3);

  if(ct != -1)
    glw_clip_disable(gr, ct);
  if(cb != -1)
    glw_clip_disable(gr, cb);
}

/**
 *
 */
static void
glw_array_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;
  glw_rctx_t rc0, rc1;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  if(rc0.rc_alpha < GLW_ALPHA_EPSILON)
    return;

  rc1 = rc0;
  glw_reposition(&rc1, 0,
                 rc1.rc_height - a->gsc.clip_offset_pre,
		 rc1.rc_width,
                 a->gsc.clip_offset_post);

  glw_store_matrix(w, &rc1);


  rc1 = rc0;

  int width = rc1.rc_width;
  int height = rc1.rc_height;

  const int clip_top    = a->gsc.clip_offset_pre - 1;
  const int clip_bottom = a->gsc.clip_offset_post - 1;

  glw_Translatef(&rc1, 0, 2.0f * a->gsc.rounded_pos / height, 0);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_array_render_one(a, c, width, height, &rc0, &rc1,
                         clip_top, clip_bottom);
  }
}


/**
 *
 */
static void
glw_array_scroll(glw_array_t *a, glw_scroll_t *gs)
{
  glw_scroll_handle_scroll(&a->gsc, &a->w, gs);
}



/**
 * This is a helper to make sure we can show items in list that are not
 * focusable even if they are at the top
 */
static void
scroll_to_me(glw_array_t *a, glw_t *c)
{
  glw_schedule_refresh(a->w.glw_root, 0);

  while(c != NULL && glw_parent_data(c, glw_array_item_t)->col > 0)
    c = TAILQ_PREV(c, glw_queue, glw_parent_link);

  if(c == NULL)
    return;

  glw_t *d = c, *e = c;

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
  a->gsc.scroll_to_me = c;
}



/**
 *
 */
static int
glw_array_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    scroll_to_me(a, extra);
    a->gsc.suggest_cnt = 0;
    w->glw_flags &= ~GLW_FLOATING_FOCUS;
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(a->gsc.scroll_to_me == extra)
      a->gsc.scroll_to_me = NULL;
    if(a->gsc.suggested == extra)
      a->gsc.suggested = NULL;

    a->num_visible_childs--;
    break;

  case GLW_SIGNAL_CHILD_HIDDEN:
    a->num_visible_childs--;
    break;

  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    c = extra;
    a->num_visible_childs++;
    glw_parent_data(c, glw_array_item_t)->just_inserted = 1;
    break;

  case GLW_SIGNAL_SCROLL:
    glw_array_scroll(a, extra);
    break;

  case GLW_SIGNAL_FHP_PATH_CHANGED:
    if(!glw_is_focused(w))
      a->gsc.suggest_cnt = 1;
    break;

  }
  return 0;
}


/**
 *
 */
static int
glw_array_pointer_event(glw_t *w, const glw_pointer_event_t *gpe)
{
  glw_array_t *a = (glw_array_t *)w;
  return glw_scroll_handle_pointer_event(&a->gsc, w, gpe);
}


static int
handle_pointer_event_filter(struct glw *w, const glw_pointer_event_t *gpe)
{
  glw_array_t *a = (glw_array_t *)w;
  return glw_scroll_handle_pointer_event_filter(&a->gsc, w, gpe);
}

/**
 *
 */
static void
glw_array_ctor(glw_t *w)
{
  glw_array_t *a = (glw_array_t *)w;
  w->glw_flags |= GLW_FLOATING_FOCUS;
  a->gsc.suggest_cnt = 1;
}

/**
 *
 */
static int
glw_array_set_int(glw_t *w, glw_attribute_t attrib, int value,
                  glw_style_t *gs)
{
  glw_array_t *a = (glw_array_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_CHILD_TILES_X:
    if(a->child_tiles_x == value)
      return 0;
    a->child_tiles_x = value;
    break;

  case GLW_ATTRIB_CHILD_TILES_Y:
    if(a->child_tiles_y == value)
      return 0;

    a->child_tiles_y = value;
    break;

  case GLW_ATTRIB_X_SPACING:
    if(a->xspacing == value)
      return 0;
    a->xspacing = value;
    break;

  case GLW_ATTRIB_Y_SPACING:
    if(a->yspacing == value)
      return 0;

    a->yspacing = value;
    break;

  default:
    return -1;
  }

  return 1;
}



/**
 *
 */
static glw_t *
glw_array_get_next_row(glw_t *c, int reverse)
{
  int current_col = glw_parent_data(c, glw_array_item_t)->col;
  if(current_col == -1)
    current_col = 0;
  if(reverse) {
    while((c = glw_get_prev_n(c, 1)) != NULL) {
      if((glw_parent_data(c, glw_array_item_t)->col == current_col ||
          c->glw_flags & GLW_CONSTRAINT_D) &&
         glw_get_focusable_child(c))
	return c;
    }
  } else {
    while((c = glw_get_next_n(c, 1)) != NULL) {
      if(glw_parent_data(c, glw_array_item_t)->col == current_col &&
         glw_get_focusable_child(c))
	return c;
    }
  }

  return NULL;
}


/**
 *
 */
static int
glw_array_bubble_event(struct glw *w, struct event *e)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_t *c = w->glw_focused;

  if(c == NULL)
    return 0;

  const int may_wrap = glw_navigate_may_wrap(c);
  const int current_col = glw_parent_data(c, glw_array_item_t)->col;

  if(event_is_action(e, ACTION_RIGHT)) {
    if(current_col == a->xentries - 1)
      return 0;

    return glw_navigate_step(c, 1, may_wrap);

  } else if(event_is_action(e, ACTION_LEFT)) {
    if(current_col == 0)
      return 0;

    return glw_navigate_step(c, -1, may_wrap);

  } else if(event_is_action(e, ACTION_UP)) {
    return glw_focus_child(glw_array_get_next_row(c, 1));

  } else if(event_is_action(e, ACTION_DOWN)) {
    return glw_focus_child(glw_array_get_next_row(c, 0));

  } else if(event_is_action(e, ACTION_PAGE_UP)) {
    const int rows = MAX(1, a->child_tiles_y - 1);
    for(int i = 0; i < rows; i++) {
      c = glw_array_get_next_row(c, 1) ?: c;
    }
    return glw_focus_child(c);


  } else if(event_is_action(e, ACTION_PAGE_DOWN)) {
    const int rows = MAX(1, a->child_tiles_y - 1);
    for(int i = 0; i < rows; i++) {
      c = glw_array_get_next_row(c, 0) ?: c;
    }
    return glw_focus_child(c);

  } else if(event_is_action(e, ACTION_MOVE_RIGHT)) {
    return glw_navigate_move(c, 1);

  } else if(event_is_action(e, ACTION_MOVE_LEFT)) {
    return glw_navigate_move(c, -1);

  } else if(event_is_action(e, ACTION_MOVE_DOWN)) {
    return glw_navigate_move(c, a->xentries);

  } else if(event_is_action(e, ACTION_MOVE_UP)) {
    return glw_navigate_move(c, -a->xentries);

  } else if(event_is_action(e, ACTION_TOP)) {
    return glw_navigate_first(w);

  } else if(event_is_action(e, ACTION_BOTTOM)) {
    return glw_navigate_last(w);
  }

  return 0;
}


/**
 *
 */
static int
glw_array_set_float_unresolved(glw_t *w, const char *a, float value,
                              glw_style_t *gs)
{
  glw_array_t *l = (glw_array_t *)w;

  return glw_scroll_set_float_attributes(&l->gsc, a, value);

}

/**
 *
 */
static int
glw_array_set_int_unresolved(glw_t *w, const char *a, int value,
                            glw_style_t *gs)
{
  glw_array_t *l = (glw_array_t *)w;

  return glw_scroll_set_int_attributes(&l->gsc, a, value);
}


/**
 *
 */
static void
glw_array_suggest_focus(glw_t *w, glw_t *c)
{
  glw_array_t *a = (glw_array_t *)w;
  glw_scroll_suggest_focus(&a->gsc, w, c);
}


/**
 * Try to find a child widget that's visible. This is used when scrolling
 * to maintain focus on screen
 */
static glw_t *
glw_array_find_visible_child(glw_t *w)
{
  glw_array_t *l = (glw_array_t *)w;
  const int top = l->gsc.target_pos + l->gsc.scroll_threshold_pre;
  const int bottom = l->gsc.target_pos + l->gsc.page_size -
    l->gsc.scroll_threshold_pre;
  glw_t *c = l->w.glw_focused;

  if(c == NULL)
    return NULL;

  if(glw_parent_data(c, glw_array_item_t)->pos_y < top) {

    while(c != NULL && glw_parent_data(c, glw_array_item_t)->pos_y < top)
      c = glw_next_widget(c);

    if(c != NULL && glw_get_focusable_child(c) == NULL)
      c = glw_next_widget(c);

  } else if(glw_parent_data(c, glw_array_item_t)->pos_y > bottom) {

    while(c != NULL && glw_parent_data(c, glw_array_item_t)->pos_y > bottom)
      c = glw_prev_widget(c);

    if(c != NULL && glw_get_focusable_child(c) == NULL)
      c = glw_prev_widget(c);

    while(c != NULL) {
      glw_t *p = glw_prev_widget(c);
      if(p == NULL)
        break;
      if(glw_parent_data(p, glw_array_item_t)->pos_y !=
         glw_parent_data(c, glw_array_item_t)->pos_y)
        break;
      c = p;
    }

  }
  return c;
}

/**
 *
 */
static glw_class_t glw_array = {
  .gc_name = "array",
  .gc_instance_size = sizeof(glw_array_t),
  .gc_parent_data_size = sizeof(glw_array_item_t),
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_render = glw_array_render,
  .gc_ctor = glw_array_ctor,
  .gc_set_int = glw_array_set_int,
  .gc_signal_handler = glw_array_callback,
  .gc_layout = glw_array_layout,
  .gc_pointer_event = glw_array_pointer_event,
  .gc_pointer_event_filter = handle_pointer_event_filter,
  .gc_bubble_event = glw_array_bubble_event,
  .gc_set_int_unresolved = glw_array_set_int_unresolved,
  .gc_set_float_unresolved = glw_array_set_float_unresolved,
  .gc_suggest_focus = glw_array_suggest_focus,
  .gc_find_visible_child = glw_array_find_visible_child,
};

GLW_REGISTER_CLASS(glw_array);
