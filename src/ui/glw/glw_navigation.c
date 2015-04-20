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
    if(to_focus != NULL && to_focus->glw_flags2 & GLW2_NAV_FOCUSABLE) {
      glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE,
                    "NavFirst");
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
  glw_t *c = glw_last_widget(parent);

  while(c != NULL) {
    glw_t *to_focus = glw_get_focusable_child(c);
    if(to_focus != NULL && to_focus->glw_flags2 & GLW2_NAV_FOCUSABLE) {
      glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE,
                    "NavLast");
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
      if(!(tentative->glw_flags2 & GLW2_NAV_FOCUSABLE))
        continue;

      to_focus = tentative;
      count--;
      if(count == 0)
         break;
    }
  }

  if(to_focus != NULL) {
    glw_focus_set(to_focus->glw_root, to_focus, GLW_FOCUS_SET_INTERACTIVE,
                  "NavStep");
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
  if(!(w->glw_parent->glw_flags2 & GLW2_NAV_WRAP)) {
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

  const int may_wrap = glw_navigate_may_wrap(c);

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

  if(c == NULL)
    return 0;

  const int may_wrap = glw_navigate_may_wrap(c);

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


/**
 *
 */
typedef struct navigate_matrix_aux {
  int cur_x;
  int cur_y;

  int direction; // 0,1,2,3 left,up,right,down

  glw_t *best;
  int distance;

} navigate_matrix_aux_t;


/**
 *
 */
static void
glw_navigate_matrix_search(glw_t *w, navigate_matrix_aux_t *nma)
{
  glw_t *c;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_navigate_matrix_search(c, nma);

  if(w->glw_root->gr_current_focus == w)
    return; // Don't consider ourself

  if(w->glw_matrix == NULL)
    return;


  glw_rect_t rect;

  Mtx m;
  memcpy(m, w->glw_matrix, sizeof(float) * 16);

  glw_project_matrix(&rect, m, w->glw_root);


  // Default current/target cordinates are center of focus
  int tgt_x = (rect.x2 + rect.x1) / 2;
  int tgt_y = (rect.y2 + rect.y1) / 2;


  // .. but will be adjusted to edge based on how we're moving

  switch(nma->direction) {
  case 0: // Moving left
    tgt_x = rect.x2;

    if(tgt_x >= nma->cur_x)
      return;
    break;

  case 1: // Moving up
    tgt_y = rect.y2;
    if(tgt_y >= nma->cur_y)
      return;
    break;

  case 2: // Moving right
    tgt_x = rect.x1;
    if(tgt_x <= nma->cur_x)
      return;
    break;

  case 3: // Moving down
    tgt_y = rect.y1;
    if(tgt_y <= nma->cur_y)
      return;
    break;
  default:
    abort();
  }

  const int dx = tgt_x - nma->cur_x;
  const int dy = tgt_y - nma->cur_y;

  int distance = sqrt(dx * dx + dy * dy);
  if(distance > nma->distance)
    return;

  nma->best = w;
  nma->distance = distance;
}




/**
 * This function tries to navigate based on the projected cordinates
 * of widgets. Basically it tries to find a widget that's a decendant
 * (in the view tree) of the parameter 'w' and is as close as possible
 * to the currently focused widget.
 *
 * This is called from the main event send/bubble loop in glw.c and only
 * if a widget has the 'navPositional' attribute set.
 */
int
glw_navigate_matrix(struct glw *w, struct event *e)
{

  navigate_matrix_aux_t nma = {
    .distance = INT32_MAX,
  };

  if(event_is_action(e, ACTION_LEFT)) {
    nma.direction = 0;
  } else if(event_is_action(e, ACTION_UP)) {
    nma.direction = 1;
  } else if(event_is_action(e, ACTION_RIGHT)) {
    nma.direction = 2;
  } else if(event_is_action(e, ACTION_DOWN)) {
    nma.direction = 3;
  } else {
    return 0;
  }

  glw_root_t *gr = w->glw_root;
  glw_t *cur = gr->gr_current_focus;
  if(cur == NULL || cur->glw_matrix == NULL)
    return 0;

  glw_rect_t rect;

  Mtx m;
  memcpy(m, cur->glw_matrix, sizeof(float) * 16);

  glw_project_matrix(&rect, m, gr);

  // Default current cordinates are center of focused area

  nma.cur_x = (rect.x2 + rect.x1) / 2;
  nma.cur_y = (rect.y2 + rect.y1) / 2;

  // Adjust center based on how we're moving

  switch(nma.direction) {
  case 0: // Moving left
    nma.cur_x = rect.x1;
    break;

  case 1: // Moving up
    nma.cur_y = rect.y1;
    break;

  case 2: // Moving right
    nma.cur_x = rect.x2;
    break;

  case 3: // Moving down
    nma.cur_y = rect.y2;
    break;
  default:
    abort();
  }


  glw_navigate_matrix_search(w, &nma);

  if(nma.best == NULL)
    return 0;

  glw_focus_set(gr, nma.best, GLW_FOCUS_SET_INTERACTIVE, "NavPositional");
  return 1;
}
