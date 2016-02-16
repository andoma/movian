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

/**
 *
 */
int
glw_scroll_handle_pointer_event_filter(glw_scroll_control_t *gs,
                                       glw_t *w,
                                       const glw_pointer_event_t *gpe)
{
  glw_root_t *gr = w->glw_root;
  const int grabbed = gr->gr_pointer_grab_scroll == w;

  switch(gpe->type) {
  default:
    return 0;
  case GLW_POINTER_TOUCH_START:
    gr->gr_pointer_grab_scroll = w;

    gs->initial_pos = gs->target_pos;
    gs->initial_touch_x = gpe->x;
    gs->initial_touch_y = gpe->y;
    gs->last_touch_x = gpe->x;
    gs->last_touch_y = gpe->y;
    gs->last_touch_time = gpe->ts;
    gs->touch_velocity = 0;
    gs->kinetic_scroll = 0;
    return 0;

  case GLW_POINTER_TOUCH_END:
    if(fabsf(gs->touch_velocity) > 10)
      gs->kinetic_scroll = gs->touch_velocity;

    if(grabbed)
      gr->gr_pointer_grab_scroll = NULL;
    return 0;
  }

  return 0;
}



/**
 *
 */
int
glw_scroll_handle_pointer_event(glw_scroll_control_t *gs,
                                glw_t *w,
                                const glw_pointer_event_t *gpe)
{
  glw_root_t *gr = w->glw_root;
  int64_t dt;
  const int grabbed = gr->gr_pointer_grab_scroll == w;
  float v;
  switch(gpe->type) {

  case GLW_POINTER_SCROLL:
    gs->bottom_anchored = 0;
    gs->target_pos += gs->page_size * gpe->delta_y;
    w->glw_flags |= GLW_UPDATE_METRICS;
    glw_schedule_refresh(w->glw_root, 0);
    return 1;

  case GLW_POINTER_FINE_SCROLL:
    gs->bottom_anchored = 0;
    gs->target_pos += gpe->delta_y;
    w->glw_flags |= GLW_UPDATE_METRICS;
    glw_schedule_refresh(w->glw_root, 0);
    return 1;

  case GLW_POINTER_TOUCH_CANCEL:
    if(grabbed)
      gr->gr_pointer_grab_scroll = NULL;
    return 1;

  case GLW_POINTER_FOCUS_MOTION:
    if(!grabbed)
      return 0;

    gs->bottom_anchored = 0;
    gs->target_pos = (gpe->y - gs->initial_touch_y) * gs->page_size * 0.5 +
      gs->initial_pos;

    if(abs(gs->target_pos - gs->initial_pos) > 15) {
      if(gr->gr_pointer_press != NULL) {
        glw_path_modify(gr->gr_pointer_press, 0, GLW_IN_PRESSED_PATH, NULL);
        gr->gr_pointer_press = NULL;
      }
    }

    dt = gpe->ts - gs->last_touch_time;
    if(dt > 100) {
      v = 1000000.0 * (gpe->y - gs->last_touch_y) / dt;
      gs->touch_velocity = v * 10;
    }
    gs->last_touch_time = gpe->ts;
    gs->last_touch_x = gpe->x;
    gs->last_touch_y = gpe->y;
    w->glw_flags |= GLW_UPDATE_METRICS;
    glw_schedule_refresh(w->glw_root, 0);
    break;

  default:
    return 0;
  }
  return 0;
}


/**
 *
 */
void
glw_scroll_layout(glw_scroll_control_t *gsc, glw_t *w, int height)
{
  const int max_value =
    MAX(0, gsc->total_size - gsc->page_size + gsc->scroll_threshold_post);

  if(w->glw_root->gr_pointer_grab_scroll == w) {

    gsc->filtered_pos = gsc->target_pos;

    gsc->filtered_pos = GLW_CLAMP(gsc->filtered_pos, 0, max_value);

  } else if(gsc->kinetic_scroll) {

    gsc->filtered_pos += gsc->kinetic_scroll;
    gsc->target_pos = gsc->filtered_pos;
    gsc->kinetic_scroll *= 0.95;
    gsc->bottom_anchored = 0;

    gsc->filtered_pos = GLW_CLAMP(gsc->filtered_pos, 0, max_value);

  } else {

    if(gsc->bottom_gravity) {

      if(gsc->target_pos == max_value) {
        gsc->bottom_anchored = 1;
      }
      if(gsc->bottom_anchored) {
        if(gsc->target_pos != max_value) {
          w->glw_flags |= GLW_UPDATE_METRICS;
        }
        gsc->target_pos = max_value;
      }
    }

    gsc->target_pos = GLW_CLAMP(gsc->target_pos, 0, max_value);

    if(fabsf(gsc->target_pos - gsc->filtered_pos) > height * 2) {
      gsc->filtered_pos = gsc->target_pos;
    } else {
      glw_lp(&gsc->filtered_pos, w->glw_root, gsc->target_pos, 0.25);
    }
  }

  gsc->rounded_pos = rintf(gsc->filtered_pos * 5.0f) / 5.0f;
}



/**
 *
 */
void
glw_scroll_update_metrics(glw_scroll_control_t *gsc, glw_t *w)
{
  float v;
  int do_update = 0;

  w->glw_flags &= ~GLW_UPDATE_METRICS;

  v = GLW_MIN(1.0f, (float)gsc->page_size / gsc->total_size);

  if(v != gsc->metrics.knob_size) {
    do_update = 1;
    gsc->metrics.knob_size = v;
  }

  v = GLW_MAX(0, (float)gsc->target_pos / (gsc->total_size - gsc->page_size + gsc->scroll_threshold_post));

  if(v != gsc->metrics.position) {
    do_update = 1;
    gsc->metrics.position = v;
  }
  if(!do_update)
    return;

  if(gsc->total_size > gsc->page_size && !(w->glw_flags & GLW_CAN_SCROLL)) {
    w->glw_flags |= GLW_CAN_SCROLL;
    glw_signal0(w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);

  } else if(gsc->total_size <= gsc->page_size &&
	    w->glw_flags & GLW_CAN_SCROLL) {
    w->glw_flags &= ~GLW_CAN_SCROLL;
    glw_signal0(w, GLW_SIGNAL_CAN_SCROLL_CHANGED, NULL);
  }

  glw_signal0(w, GLW_SIGNAL_SLIDER_METRICS, &gsc->metrics);
}


/**
 *
 */
int
glw_scroll_set_float_attributes(glw_scroll_control_t *gsc, const char *a,
                                float value)
{
  if(!strcmp(a, "clipAlpha")) {
    if(value == gsc->clip_alpha)
      return GLW_SET_NO_CHANGE;

    gsc->clip_alpha = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "clipBlur")) {
    if(value == gsc->clip_blur)
      return GLW_SET_NO_CHANGE;

    gsc->clip_blur = value;
    return GLW_SET_RERENDER_REQUIRED;
  }
  return GLW_SET_NOT_RESPONDING;
}


/**
 *
 */
int
glw_scroll_set_int_attributes(glw_scroll_control_t *gsc, const char *a,
                              int value)
{
  if(!strcmp(a, "chaseFocus")) {
    gsc->chase_focus = value;
    return GLW_SET_NO_CHANGE;
  }

  if(!strcmp(a, "scrollThresholdTop")) {
    if(gsc->scroll_threshold_pre == value)
      return GLW_SET_NO_CHANGE;

    gsc->scroll_threshold_pre = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "scrollThresholdBottom")) {
    if(gsc->scroll_threshold_post == value)
      return GLW_SET_NO_CHANGE;

    gsc->scroll_threshold_post = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "clipOffsetTop")) {
    if(gsc->clip_offset_pre == value)
      return GLW_SET_NO_CHANGE;

    gsc->clip_offset_pre = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "clipOffsetBottom")) {
    if(gsc->clip_offset_post == value)
      return GLW_SET_NO_CHANGE;

    gsc->clip_offset_post = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "bottomGravity")) {
    if(gsc->bottom_gravity == !!value)
      return GLW_SET_NO_CHANGE;

    gsc->bottom_gravity = !!value;
    gsc->bottom_anchored = gsc->bottom_gravity;
    return GLW_SET_RERENDER_REQUIRED;
  }

  return GLW_SET_NOT_RESPONDING;
}


/**
 *
 */
void
glw_scroll_suggest_focus(glw_scroll_control_t *gsc, glw_t *w, glw_t *c)
{
  if(!glw_is_focused(w)) {
    w->glw_focused = c;
    glw_signal0(w, GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE, c);
    gsc->scroll_to_me = c;
    return;
  }

  if(gsc->suggested == w->glw_focused || gsc->suggest_cnt > 0) {
    c = glw_focus_by_path(c);
    if(c != NULL)
      glw_focus_set(c->glw_root, c, GLW_FOCUS_SET_SUGGESTED, "Suggested");
    gsc->suggest_cnt = 1;
  }
  gsc->suggested = c;
  gsc->suggest_cnt++;
}


/**
 *
 */
void
glw_scroll_handle_scroll(glw_scroll_control_t *gsc, glw_t *w, glw_scroll_t *gs)
{
  gsc->bottom_anchored = 0;
  int top = GLW_MAX(gs->value * (gsc->total_size - gsc->page_size
                                 + gsc->scroll_threshold_post), 0);
  gsc->target_pos = top;
  glw_schedule_refresh(w->glw_root, 0);

  if(gsc->chase_focus == 0)
    return;

  glw_t *c = w->glw_class->gc_find_visible_child(w);
  if(c == NULL)
    return;

  if(glw_is_focused(w)) {
    glw_focus_set(w->glw_root, c, GLW_FOCUS_SET_SUGGESTED, "Scroll");
  } else {
    w->glw_focused = c;
    glw_signal0(w, GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE, c);
  }
}


/**
 *
 */
int
glw_scroll_handle_event(glw_scroll_control_t *gs,
                        glw_t *w, event_t *e)
{
  if(event_is_type(e, EVENT_SCROLL)) {
    event_scroll_t *es = (event_scroll_t *)e;

    gs->bottom_anchored = 0;
    gs->target_pos += es->dY;
    w->glw_flags |= GLW_UPDATE_METRICS;
    glw_schedule_refresh(w->glw_root, 0);
    return 1;
  }
  return 0;
}
