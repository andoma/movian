/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

  int hold;

  int timer;

  float time;
  float transition_time;

  int displaytime;

  prop_t *playstatus;

} glw_slideshow_t;

#define glw_parent_alpha glw_parent_val[0].f

/**
 *
 */
static void
glw_slideshow_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c, *p, *n;
  glw_rctx_t rc0;

  if((c = w->glw_focused) == NULL)
    return;

  p = glw_prev_widget(c);
  if(p == NULL)
    p = glw_last_widget(w);
  if(p != NULL && p != c) {
    if(p->glw_parent_alpha > 0.01) {
      rc0 = *rc;
      rc0.rc_alpha *= p->glw_parent_alpha;
      glw_render0(p, &rc0);
    }
  }

  rc0 = *rc;
  rc0.rc_alpha *= c->glw_parent_alpha;
  glw_render0(c, &rc0);

  n = glw_next_widget(c);
  if(n == NULL)
    n = glw_first_widget(w);
  if(n != NULL && n != c) {
    if(n->glw_parent_alpha > 0.01) {
      rc0 = *rc;
      rc0.rc_alpha *= n->glw_parent_alpha;
      glw_render0(n, &rc0);
    }
  }
}


/**
 *
 */
static void
glw_slideshow_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_t *c, *p, *n;
  float delta;

  glw_reset_screensaver(w->glw_root);

  delta = s->w.glw_root->gr_frameduration / (1000000.0 * s->transition_time);
  if(s->time == 0) {
    s->displaytime = INT32_MAX;
  } else {
    s->displaytime = 1000000 * s->time / s->w.glw_root->gr_frameduration;
  }

    
  if((c = s->w.glw_focused) == NULL) {
    c = s->w.glw_focused = glw_first_widget(&s->w);
    if(c)
      glw_copy_constraints(&s->w, c);
  }
  if(c == NULL)
    return;

  if(s->timer >= s->displaytime) {
    c = glw_next_widget(c);
    if(c == NULL)
      c = glw_first_widget(&s->w);
    s->timer = 0;
    if(c != NULL) {
      glw_focus_open_path_close_all_other(c);
      glw_copy_constraints(&s->w, c);
    }
  }
  
  if(!s->hold)
    s->timer++;

  glw_layout0(c, rc);
  c->glw_parent_alpha = GLW_MIN(c->glw_parent_alpha + delta, 1.0f);

  /**
   * Keep previous and next images 'hot' (ie, loaded into texture memory)
   */
  p = glw_prev_widget(c);
  if(p == NULL)
    p = glw_last_widget(&s->w);
  if(p != NULL && p != c) {
    p->glw_parent_alpha = GLW_MAX(p->glw_parent_alpha - delta, 0.0f);
    glw_layout0(p, rc);
  }

  n = glw_next_widget(c);
  if(n == NULL)
    n = glw_first_widget(&s->w);
  if(n != NULL && n != c) {
    n->glw_parent_alpha = GLW_MAX(n->glw_parent_alpha - delta, 0.0f);
    glw_layout0(n, rc);
  }
}


/**
 *
 */
static void
glw_slideshow_update_playstatus(glw_slideshow_t *s)
{
  prop_set_string(s->playstatus, s->hold ? "pause" : "play");
}


/**
 *
 */
static int
glw_slideshow_event(glw_t *w, event_t *e)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_t *c;
  event_int_t *eu = (event_int_t *)e;

  if(event_is_action(e, ACTION_SKIP_FORWARD) ||
     event_is_action(e, ACTION_RIGHT)) {
    c = s->w.glw_focused ? glw_next_widget(s->w.glw_focused) : NULL;
    if(c == NULL)
      c = glw_first_widget(&s->w);
    s->w.glw_focused = c;
    s->timer = 0;

  } else if(event_is_action(e, ACTION_SKIP_BACKWARD) ||
	    event_is_action(e, ACTION_LEFT)) {

    c = s->w.glw_focused ? glw_prev_widget(s->w.glw_focused) : NULL;
    if(c == NULL)
      c = glw_last_widget(&s->w);
    s->w.glw_focused = c;
    s->timer = 0;

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
  s->time = 5.0;
  s->transition_time = 0.5;
}


/**
 *
 */
static int
glw_slideshow_set_float(glw_t *w, glw_attribute_t attrib, float value)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_TIME:
    if(s->time == value)
      return 0;

    s->time = value;
    break;

  case GLW_ATTRIB_TRANSITION_TIME:
    if(s->transition_time == value)
      return 0;

    s->transition_time = value;
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
glw_slideshow_set_roots(glw_t *w, prop_t *self, prop_t *parent, prop_t *clone)
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
