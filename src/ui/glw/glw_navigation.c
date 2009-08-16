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
      if(!horizontal)
	continue;
      break;

    case GLW_CONTAINER_Y:
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

  if(glw_is_focusable(w)) {
    
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
  case GLW_BACKDROP:
  case GLW_MODEL:
    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
      find_candidate(c, query);
    break;

  case GLW_LIST_X:
  case GLW_LIST_Y:
    if(w->glw_focused) {
      c = w->glw_focused;
    } else if(query->direction) {
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

  case GLW_EXPANDER_Y:
  case GLW_CONTAINER_X:
  case GLW_CONTAINER_Y:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query);
    break;
  }
}

/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e, int local)
{
  glw_t  *p, *c, *t = NULL, *d;
  float x, y;
  int direction;
  int orientation;
  int pagemode = 0;
  int pagecnt;
  query_t query;


  x = compute_position(w, 1);
  y = compute_position(w, 0);

  memset(&query, 0, sizeof(query));

  query.x = x;
  query.y = y;
  query.score = 100000000;

  if(event_is_action(e, ACTION_FOCUS_PREV)) {

    glw_focus_crawl(w, 0);
    event_unref(e);
    return 1;

  } else if(event_is_action(e, ACTION_FOCUS_NEXT)) {

    glw_focus_crawl(w, 1);
    event_unref(e);
    return 1;

  } else if(event_is_action(e, ACTION_UP)) {

    orientation = 0;
    direction   = 0;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;

  } else if(event_is_action(e, ACTION_PAGE_UP)) {

    orientation = 0;
    direction   = 0;
    pagemode    = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;

  } else if(event_is_action(e, ACTION_TOP)) {

    orientation = 0;
    direction   = 0;
    pagemode    = 2;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = y - 0.0001;


  } else if(event_is_action(e, ACTION_DOWN)) {

    orientation = 0;
    direction   = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_PAGE_DOWN)) {

    orientation = 0;
    direction   = 1;
    pagemode    = 1;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_BOTTOM)) {

    orientation = 0;
    direction   = 1;
    pagemode    = 2;

    query.xmin = -1;
    query.xmax = 1;
    query.ymin = y + 0.0001;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_LEFT)) {

    orientation = 1;
    direction   = 0;

    query.xmin = -1;
    query.xmax = x - 0.0001;
    query.ymin = -1;
    query.ymax = 1;

  } else if(event_is_action(e, ACTION_RIGHT)) {

    orientation = 1;
    direction   = 1;

    query.xmin = x + 0.0001;
    query.xmax = 1;
    query.ymin = -1;
    query.ymax = 1;

  } else {

    return 0;

  }


  query.orientation = orientation;
  query.direction   = direction;
  pagecnt = 10;

  c = NULL;
  for(; (p = w->glw_parent) != NULL; w = p) {

    if(local) {
      switch(w->glw_class) {
      case GLW_LIST_X:
      case GLW_LIST_Y:
	return 0;
      default:
	break;
      }
    }

    switch(p->glw_class) {
      
    default:

    case GLW_ANIMATOR:
    case GLW_IMAGE:
    case GLW_BACKDROP:
    case GLW_DECK:
    case GLW_MODEL:
    case GLW_CONTAINER_Z:
    case GLW_EXPANDER_Y:
      break;

    case GLW_CONTAINER_X:
    case GLW_CONTAINER_Y:
      if(pagemode)
	break;

      if(p->glw_class == (orientation ? GLW_CONTAINER_X : GLW_CONTAINER_Y))
	goto container;
      break;
	      
    case GLW_LIST_X:
    case GLW_LIST_Y:
      if(p->glw_class != (orientation ? GLW_LIST_X : GLW_LIST_Y))
	break;

      container:
      c = w;
      while(1) {
	if(direction == 1) {
	  /* Down / Right */
	  if(pagemode == 1) {

	    d = TAILQ_NEXT(c, glw_parent_link);
	    if(d != NULL) {

	      while(pagecnt--) {
		c = d;
		d = TAILQ_NEXT(c, glw_parent_link);
		if(d == NULL)
		  break;
	      }
	    }

	  } else if(pagemode == 2) {

	    c = TAILQ_LAST(&p->glw_childs, glw_queue);

	  } else {
	    c = TAILQ_NEXT(c, glw_parent_link);
	  }

	} else {
	  /* Up / Left */
	  if(pagemode == 1) {

	    d = TAILQ_PREV(c, glw_queue, glw_parent_link);
	    if(d != NULL) {

	      while(pagecnt--) {
		c = d;
		d = TAILQ_PREV(c, glw_queue, glw_parent_link);
		if(d == NULL)
		  break;
	      }
	    }

	  } else if(pagemode == 2) {

	    c = TAILQ_FIRST(&p->glw_childs);

	  } else {
	    c = TAILQ_PREV(c, glw_queue, glw_parent_link);
	  }

	}

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

  if(t != NULL) {
    glw_focus_set(t->glw_root, t, 1);
    event_unref(e);
    return 1;
  }

  return 0;
}


