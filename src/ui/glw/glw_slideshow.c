/*
 *  GL Widgets, slideshow
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
glw_slideshow_layout(glw_slideshow_t *s, glw_rctx_t *rc)
{
  glw_t *c, *p, *n;
  float delta;

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
glw_slideshow_event(glw_slideshow_t *s, event_t *e)
{
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
  glw_slideshow_t *s = (glw_slideshow_t *)w;

  switch(signal) {
   case GLW_SIGNAL_LAYOUT:
     w->glw_root->gr_screensaver_counter = 0;
    glw_slideshow_layout(s, extra);
    return 0;

  case GLW_SIGNAL_EVENT_BUBBLE:
    return glw_slideshow_event(s, extra);

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
static void
glw_slideshow_set(glw_t *w, va_list ap)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_attribute_t attrib;
  prop_t *p;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_TIME:
      s->time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_TRANSITION_TIME:
      s->transition_time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_PROPROOTS3:
      p = va_arg(ap, void *);
      s->playstatus = prop_create(prop_create(p, "slideshow"), "playstatus");

      (void)va_arg(ap, void *); // Parent, just throw it away
      (void)va_arg(ap, void *); // Clone, just throw it away
      glw_slideshow_update_playstatus(s);
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
static glw_class_t glw_slideshow = {
  .gc_name = "slideshow",
  .gc_instance_size = sizeof(glw_slideshow_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_ctor = glw_slideshow_ctor,
  .gc_set = glw_slideshow_set,
  .gc_render = glw_slideshow_render,
  .gc_signal_handler = glw_slideshow_callback,
};

GLW_REGISTER_CLASS(glw_slideshow);
