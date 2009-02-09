/*
 *  GL Widgets, Navigation
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
#include "glw_event.h"


typedef struct query {
  float x, xmin, xmax;
  float y, ymin, ymax;

  int direction;
  int orientation;

  glw_t *best;
  float score;

} query_t;



/**
 *
 */
static float
compute_position(glw_t *w, int horizontal)
{
  glw_t *p, *c;
  float a, x;

  x = 0;

  for(; (p = w->glw_parent) != NULL; w = p) {
    switch(p->glw_class) {
    case GLW_CONTAINER_X:
    case GLW_STACK_X:
      if(!horizontal)
	continue;
      break;

    case GLW_CONTAINER_Y:
    case GLW_STACK_Y:
      if(horizontal)
	continue;
      break;

    default:
      continue;
    }
    
    a = w->glw_norm_weight / 2;

    TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
      if(c == w)
	break;
      a += c->glw_norm_weight;
    }

    x = (2 * a) - 1 + (x * w->glw_norm_weight);
  }
  return x;
}


/**
 *
 */
static void
find_candidate(glw_t *w, query_t *query)
{
  glw_t *c;
  float x, y, distance, dx, dy;

  if(w->glw_focus_mode == GLW_FOCUS_TARGET) {
    
    x = compute_position(w, 1);
    y = compute_position(w, 0);

    dx = query->x - x;
    dy = query->y - y;
    distance = sqrt(dx * dx + dy * dy);

    if(distance < query->score) {
      query->score = distance;
      query->best = w;
    }
  }

  switch(w->glw_class) {
  default:
    return;

  case GLW_DECK:
    if((c = w->glw_selected) != NULL)
      find_candidate(c, query);
    break;

  case GLW_ANIMATOR:
  case GLW_IMAGE:
  case GLW_MODEL:
    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
      find_candidate(c, query);
    break;

  case GLW_LIST_X:
  case GLW_LIST_Y:
    /* We end up here if we try to enter a GLW_LIST from outside */
    if(query->direction) {
      c = TAILQ_FIRST(&w->glw_childs);
    } else {
      c = TAILQ_LAST(&w->glw_childs, glw_queue);
    }

    if(c != NULL)
      find_candidate(c, query);

    break;

  case GLW_CONTAINER_Z:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query);
    break;

  case GLW_EXPANDER:
  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
  case GLW_STACK_X:
  case GLW_STACK_Y:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query);
    break;
  }
}


/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e)
{
  glw_t  *p, *c, *t = NULL, *l;
  float x, y;
  int direction;
  int orientation;
  query_t query;
  struct glw_queue *q;

  x = compute_position(w, 1);
  y = compute_position(w, 0);

  memset(&query, 0, sizeof(query));

  query.x = x;
  query.y = y;
  query.score = 100000000;

  l = w->glw_focus_parent;
  q = l != NULL ? &l->glw_focus_childs : &w->glw_root->gr_focus_childs;

  switch(e->e_type) {
  default:
    return 0;

  case EVENT_FOCUS_PREV:
    w = TAILQ_LAST(q, glw_queue);
    TAILQ_REMOVE(q, w, glw_focus_parent_link);
    TAILQ_INSERT_HEAD(q, w, glw_focus_parent_link);
    glw_signal0(w, GLW_SIGNAL_FOCUS_CHANGED, NULL);
    return 0;

  case EVENT_FOCUS_NEXT:
    TAILQ_REMOVE(q, w, glw_focus_parent_link);
    TAILQ_INSERT_TAIL(q, w, glw_focus_parent_link);
    w = TAILQ_FIRST(q);
    glw_signal0(w, GLW_SIGNAL_FOCUS_CHANGED, NULL);
    return 1;

  case EVENT_UP:
    orientation = 0;
    direction   = 0;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;
    break;

  case EVENT_DOWN:
    orientation = 0;
    direction   = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;
    break;

  case EVENT_LEFT:
    orientation = 1;
    direction   = 0;

    query.xmin = -1;
    query.xmax = x - 0.0001;
    query.ymin = -1;
    query.ymax = 1;
    break;

  case EVENT_RIGHT:
    orientation = 1;
    direction   = 1;

    query.xmin = x + 0.0001;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = 1;
    break;
  }


  query.orientation = orientation;
  query.direction   = direction;

  c = NULL;
  for(; (p = w->glw_parent) != NULL; w = p) {

    switch(p->glw_class) {
      
    default:

    case GLW_ANIMATOR:
    case GLW_IMAGE:
    case GLW_DECK:
    case GLW_MODEL:
      break;

    case GLW_CONTAINER_Z:
    case GLW_EXPANDER:
      break;

    case GLW_CONTAINER_X:
    case GLW_CONTAINER_Y:
      if(p->glw_class == (orientation ? GLW_CONTAINER_X : GLW_CONTAINER_Y))
	goto container;
      break;
	      
    case GLW_STACK_X:
    case GLW_STACK_Y:
      if(p->glw_class == (orientation ? GLW_STACK_X : GLW_STACK_Y))
	goto container;
      break;

    case GLW_LIST_X:
    case GLW_LIST_Y:

      if(p->glw_class != (orientation ? GLW_LIST_X : GLW_LIST_Y))
	break;

      container:
      c = w;
      while(1) {
	if(direction == 1)
	  c = TAILQ_NEXT(c, glw_parent_link);
	else
	  c = TAILQ_PREV(c, glw_queue, glw_parent_link);
	if(c == NULL)
	  break;
	find_candidate(c, &query);
	t = query.best;
	if(t != NULL)
	  break;
      }
      break;

    }
    if(t != NULL)
      break;
  }

  if(t != NULL)
    glw_focus_set(t);

  return 0;
}


