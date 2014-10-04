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

#include <limits.h>

#include "glw.h"
#include "event.h"
#include "glw_event.h"
#include "glw_settings.h"
#include "glw_navigation.h"



static glw_t *
glw_step_widget(glw_t *c, int forward)
{
  if(forward)
    return glw_next_widget(c);
  else
    return glw_prev_widget(c);
}


/**
 *
 */
static int
glw_nav_first(glw_t *parent)
{
  glw_t *c = glw_first_widget(parent);

  while(c != NULL) {
    glw_t *to_focus = glw_get_focusable_child(c);
    if(to_focus != NULL) {
      glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE);
      return 1;
    }
    c = glw_next_widget(c);
  }
  return 0;
}


/**
 *
 */
static int
glw_nav_last(glw_t *parent)
{
  glw_t *c = glw_last_widget(parent, 0);

  while(c != NULL) {
    glw_t *to_focus = glw_get_focusable_child(c);
    if(to_focus != NULL) {
      glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE);
      return 1;
    }
    c = glw_prev_widget(c);
  }
  return 0;
}


/**
 *
 */
int
glw_navigate_step(glw_t *c, int count, int may_wrap)
{
  glw_t *parent = c->glw_parent;
  glw_t *to_focus = NULL;
  int forward = 1;

  if(count < 0) {
    forward = 0;
    count = -count;
  }

  while((c = glw_step_widget(c, forward)) != NULL) {
    glw_t *tentative = glw_get_focusable_child(c);
    if(tentative != NULL) {
      to_focus = tentative;
      count--;
      if(count == 0)
         break;
    }
  }

  if(to_focus != NULL) {
    glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE);
    return 1;
  } else if(may_wrap) {
    if(forward) {
      return glw_nav_first(parent);
    } else {
      return glw_nav_last(parent);
    }
  }

  return 0;
}


/**
 *
 */
int
glw_navigate_move(glw_t *w, int steps)
{
  glw_move_op_t mop = {0};
  mop.steps = steps;
  glw_signal0(w, GLW_SIGNAL_MOVE, &mop);
  return mop.did_move;
}


/**
 *
 */
int
glw_navigate_may_wrap(glw_t *w)
{
  if(!(w->glw_flags2 & GLW2_NAV_WRAP)) {
    return 0;
  }
  if(!glw_settings.gs_wrap) {
    return 0;
  }
  int may_wrap = 1;
  glw_signal0(w, GLW_SIGNAL_WRAP_CHECK, &may_wrap);
  return may_wrap;

}


/**
 *
 */
int
glw_navigate_vertical(struct glw *w, struct event *e)
{
  glw_t *c = w->glw_focused;

  if(c == NULL)
    return 0;

  const int may_wrap = glw_navigate_may_wrap(w);

  if(event_is_action(e, ACTION_DOWN)) {
    return glw_navigate_step(c, 1, may_wrap);

  } else if(event_is_action(e, ACTION_UP)) {
    return glw_navigate_step(c, -1, may_wrap);

  } else if(event_is_action(e, ACTION_PAGE_UP)) {
    return glw_navigate_step(c, -10, 0);

  } else if(event_is_action(e, ACTION_PAGE_DOWN)) {
    return glw_navigate_step(c, 10, 0);

  } else if(event_is_action(e, ACTION_TOP)) {
    return glw_nav_first(w);

  } else if(event_is_action(e, ACTION_BOTTOM)) {
    return glw_nav_last(w);

  } else if(event_is_action(e, ACTION_MOVE_DOWN)) {
    return glw_navigate_move(c, 1);

  } else if(event_is_action(e, ACTION_MOVE_UP)) {
    return glw_navigate_move(c, -1);

  }
  return 0;
}


/**
 *
 */
int
glw_navigate_horizontal(struct glw *w, struct event *e)
{
  glw_t *c = w->glw_focused;

  const int may_wrap = glw_navigate_may_wrap(w);

  if(c == NULL)
    return 0;

  if(event_is_action(e, ACTION_LEFT)) {
    return glw_navigate_step(c, -1, may_wrap);

  } else if(event_is_action(e, ACTION_RIGHT)) {
    return glw_navigate_step(c, 1, may_wrap);

  } else if(event_is_action(e, ACTION_MOVE_RIGHT)) {
    return glw_navigate_move(c, 1);

  } else if(event_is_action(e, ACTION_MOVE_LEFT)) {
    return glw_navigate_move(c, -1);

  }
  return 0;
}
