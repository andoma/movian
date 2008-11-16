/*
 *  GL Widgets, Various helpers
 *  Copyright (C) 2007 Andreas Öman
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

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "glw.h"
#include "glw_i.h"


/**
 * Find a target we can move to
 *
 * It must have a layout_form_callback attached to it so we
 * can 'escape' from it
 */

static glw_t *
find_anything_with_id(glw_t *w, glw_class_t skip_class, int skip)
{
  glw_t *c, *r, *best;

  if(w == NULL)
    return w;

  if(w->glw_flags & GLW_SELECTABLE) {
    /* Candidate for selection */

    switch(w->glw_class) {
    case GLW_LIST:
      /* We never want to switch to a list with no childs in it.
	 There is nothing the user can do there, so skip over it */
      if(TAILQ_FIRST(&w->glw_childs) != NULL)
	return w;
      break;
    default:
      return w;
    }
  }

  if(w->glw_class == skip_class) {
    best = NULL;
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if((r = find_anything_with_id(c, 0, 0)) != NULL) {
	best = r;
	if(skip == 0)
	  return r;
	skip--;
      }
    }
    if(best != NULL)
      return best;
  } else {
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(w->glw_class == GLW_DECK && c != w->glw_selected)
	continue;
      if((r = find_anything_with_id(c, skip_class, skip)) != NULL)
	return r;
    }
  }

  return NULL;
}




/**
 *
 */
static glw_t *
layout_form_find(glw_t *w, int rev, glw_class_t cont_class, 
		 glw_class_t skip_class, int skip)
{
  glw_t *c, *p, *s;
  
  while(1) {

    p = w->glw_parent;
    if(p == NULL)
      return NULL;

    if(p->glw_class == GLW_LIST)
      return NULL;

    if(p->glw_class == cont_class) {
      s = w;
      while(1) {
	if(rev)
	  s = TAILQ_PREV(s, glw_queue, glw_parent_link);
	else
	  s = TAILQ_NEXT(s, glw_parent_link);

	if(s == NULL)
	  break;

	if((c = find_anything_with_id(s, skip_class, skip)) != NULL)
	  return c;
      }
    }
    w = w->glw_parent;
  }
}


/**
 * Compute the "X and Y" offset in the current widget tree
 *
 * We use these values so we arrive aligned with the old position when
 * we cross verital / horizontal containers
 */
static void
layout_form_get_xy(glw_t *w, int *xp, int *yp)
{
  glw_t *p = w->glw_parent;

  *xp = 0;
  *yp = 0;

  switch(p->glw_class) {
  default:
    break;

  case GLW_CONTAINER_X:
    while((w = TAILQ_PREV(w, glw_queue, glw_parent_link)) != NULL) {
      if(w->glw_flags & GLW_SELECTABLE)
	(*xp)++;
    }
    break;

  case GLW_CONTAINER_Y:
    while((w = TAILQ_PREV(w, glw_queue, glw_parent_link)) != NULL) {
      if(w->glw_flags & GLW_SELECTABLE)
	(*yp)++;
    }
    break;
  }
}



/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e)
{
  int x = 0, y = 0;
  glw_t *n = NULL;

  if(glw_event_map_intercept(w, e))
    return 1;

  if(w->glw_selected != NULL)
    if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, e))
      return 1;
  
  if(w->glw_flags & GLW_SELECTABLE) {

    layout_form_get_xy(w, &x, &y);

    switch(e->e_type) {
    default:
      break;

    case EVENT_UP:
      n = layout_form_find(w, 1, GLW_CONTAINER_Y, GLW_CONTAINER_X, x);
      break;
    case EVENT_DOWN:
      n = layout_form_find(w, 0, GLW_CONTAINER_Y, GLW_CONTAINER_X, x);
      break;
    case EVENT_LEFT:
      n = layout_form_find(w, 1, GLW_CONTAINER_X, GLW_CONTAINER_Y, y);
      break;
    case EVENT_RIGHT:
      n = layout_form_find(w, 0, GLW_CONTAINER_X, GLW_CONTAINER_Y, y);
      break;
    }

    if(n != NULL) {
      while(n->glw_parent != NULL) {
	n->glw_parent->glw_selected = n;
	n = n->glw_parent;
      }
      return 1;
    }
  }
  return 0;
}
