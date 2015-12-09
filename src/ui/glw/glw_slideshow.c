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
#include "event.h"
#include "glw.h"


typedef struct glw_slideshow {
  glw_t w;

  int64_t deadline;

  int hold;
  int display_time;
  int transition_time;
  prop_t *playstatus;

} glw_slideshow_t;


typedef struct glw_slideshow_item {
  float alpha;
} glw_slideshow_item_t;

#define itemdata(w) glw_parent_data(w, glw_slideshow_item_t)

/**
 *
 */
static void
glw_slideshow_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c, *p, *n;
  glw_rctx_t rc0;
  float sa = w->glw_alpha;

  if((c = w->glw_focused) == NULL)
    return;

  p = glw_prev_widget(c);
  if(p == NULL)
    p = glw_last_widget(w);
  if(p != NULL && p != c) {
    float a = sa * glw_parent_data(p, glw_slideshow_item_t)->alpha;
    if(a > GLW_ALPHA_EPSILON) {
      rc0 = *rc;
      rc0.rc_alpha *= a;
      glw_render0(p, &rc0);
    }
  }

  rc0 = *rc;
  rc0.rc_alpha *= sa * glw_parent_data(c, glw_slideshow_item_t)->alpha;
  glw_render0(c, &rc0);

  n = glw_next_widget(c);
  if(n == NULL)
    n = glw_first_widget(w);
  if(n != NULL && n != c) {
    float a = sa * glw_parent_data(n, glw_slideshow_item_t)->alpha;
    if(a > GLW_ALPHA_EPSILON) {
      rc0 = *rc;
      rc0.rc_alpha *= a;
      glw_render0(n, &rc0);
    }
  }
}


/**
 *
 */
static int
update_parent_alpha(glw_t *w, float v)
{
  if(itemdata(w)->alpha == v)
    return 0;
  itemdata(w)->alpha = v;
  return 1;
}


/**
 *
 */
static void
glw_slideshow_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_root_t *gr = w->glw_root;
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_t *c, *p, *n;
  float delta, a;
  int r = 0;

  delta = s->w.glw_root->gr_frameduration / (float)s->transition_time;

  if((c = s->w.glw_focused) == NULL) {
    c = s->w.glw_focused = glw_first_widget(&s->w);
    s->deadline = gr->gr_frame_start + s->display_time;
    if(c)
      glw_copy_constraints(&s->w, c);
  }

  if(c == NULL)
    return;

  glw_schedule_refresh(gr, s->deadline);
  if(s->deadline <= gr->gr_frame_start) {
    s->deadline = gr->gr_frame_start + s->display_time;
    c = glw_next_widget(c);
    if(c == NULL)
      c = glw_first_widget(&s->w);
    if(c != NULL) {
      s->w.glw_focused = c;
      glw_focus_open_path_close_all_other(c);
      glw_copy_constraints(&s->w, c);
    }
  }

  glw_layout0(c, rc);
  a = itemdata(c)->alpha;
  r |= update_parent_alpha(c, GLW_MIN(a + delta, 1.0f));

  /**
   * Keep previous and next images 'hot' (ie, loaded into texture memory)
   */

  glw_rctx_t rc0 = *rc;

  p = glw_prev_widget(c);
  if(p == NULL)
    p = glw_last_widget(&s->w);
  if(p != NULL && p != c) {
    a = itemdata(p)->alpha;
    r |= update_parent_alpha(p, GLW_MAX(a - delta, 0.0f));
    rc0.rc_invisible = itemdata(p)->alpha < GLW_ALPHA_EPSILON;
    glw_layout0(p, &rc0);
  }

  n = glw_next_widget(c);
  if(n == NULL)
    n = glw_first_widget(&s->w);
  if(n != NULL && n != c) {
    a = itemdata(n)->alpha;
    r |= update_parent_alpha(n, GLW_MAX(a - delta, 0.0f));
    rc0.rc_invisible = itemdata(n)->alpha < GLW_ALPHA_EPSILON;
    glw_layout0(n, &rc0);
  }

  if(r)
    glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
glw_slideshow_update_playstatus(glw_slideshow_t *s)
{
  s->deadline = s->hold ? INT64_MAX : 0;
  glw_need_refresh(s->w.glw_root, 0);

  prop_set_string(s->playstatus, s->hold ? "pause" : "play");
}


/**
 *
 */
static int
glw_slideshow_event(glw_t *w, event_t *e)
{
  glw_root_t *gr = w->glw_root;
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_t *c;
  event_int_t *eu = (event_int_t *)e;

  if(event_is_action(e, ACTION_SKIP_FORWARD) ||
     event_is_action(e, ACTION_RIGHT)) {
    c = w->glw_focused ? glw_next_widget(w->glw_focused) : NULL;
    if(c == NULL)
      c = glw_first_widget(w);
    w->glw_focused = c;
    s->deadline = 0;
    glw_need_refresh(gr, 0);

  } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
	    event_is_action(e, ACTION_LEFT)) {

    c = w->glw_focused ? glw_prev_widget(w->glw_focused) : NULL;
    if(c == NULL)
      c = glw_last_widget(w);
    w->glw_focused = c;
    s->deadline = 0;
    glw_need_refresh(gr, 0);

  } else if(event_is_type(e, EVENT_UNICODE) && eu->val == 32) {

    s->hold = !s->hold;
    glw_slideshow_update_playstatus(s);

  } else if(event_is_action(e, ACTION_PLAYPAUSE) ||
	    event_is_action(e, ACTION_PLAY) ||
	    event_is_action(e, ACTION_PAUSE)) {

    s->hold = action_update_hold_by_event(s->hold, e);
    glw_slideshow_update_playstatus(s);

  } else if(event_is_action(e, ACTION_STOP)) {

    prop_set_string(s->playstatus, "stop");

  } else
    return 0;

  return 1;
}



/**
 *
 */
static int
glw_slideshow_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_focused == extra)
      glw_copy_constraints(w, extra);
    return 1;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_slideshow_ctor(glw_t *w)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  s->display_time = 5000000;
  s->transition_time = 500000;
}


/**
 *
 */
static int
glw_slideshow_set_float(glw_t *w, glw_attribute_t attrib, float value,
                        glw_style_t *gs)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  const int v = value * 1000000;

  switch(attrib) {

  case GLW_ATTRIB_TIME:
    if(s->display_time == v)
      return 0;

    s->display_time = v;
    break;

  case GLW_ATTRIB_TRANSITION_TIME:
    if(s->transition_time == v)
      return 0;

    s->transition_time = v;
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
glw_slideshow_set_roots(glw_t *w, prop_t *self, prop_t *parent, prop_t *clone,
                        prop_t *core)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;

  s->playstatus = prop_create(prop_create(self, "slideshow"), "playstatus");
}


/**
 *
 */
static glw_class_t glw_slideshow = {
  .gc_name = "slideshow",
  .gc_instance_size = sizeof(glw_slideshow_t),
  .gc_parent_data_size = sizeof(glw_slideshow_item_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_ctor = glw_slideshow_ctor,
  .gc_set_float = glw_slideshow_set_float,
  .gc_set_roots = glw_slideshow_set_roots,
  .gc_layout = glw_slideshow_layout,
  .gc_render = glw_slideshow_render,
  .gc_signal_handler = glw_slideshow_callback,
  .gc_bubble_event = glw_slideshow_event,
};

GLW_REGISTER_CLASS(glw_slideshow);
