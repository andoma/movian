/*
 *  GL Widgets, Navigation
 *  Copyright (C) 2008 - 2010 Andreas Öman
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

#include <limits.h>

#include "glw.h"
#include "event.h"
#include "glw_event.h"

typedef struct query {
  float x1, y1, x2, y2, xc, yc;

  int direction;
  glw_orientation_t orientation;

  glw_t *best;
  float score;

} query_t;

/**
 *
 */
static float 
distance_to_line_segment(float cx, float cy,
			 float ax, float ay,
			 float bx, float by)
{
  float r_num = (cx - ax) * (bx - ax) + (cy - ay) * (by - ay);
  float r_den = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
  float r = r_num / r_den;

  if(r >= 0 && r <= 1) {
    float s = ((ay - cy) * (bx - ax) - (ax - cx) * (by - ay)) / r_den;
    return fabsf(s) * sqrtf(r_den);
  }

  float d1 = (cx - ax) * (cx - ax) + (cy - ay) * (cy - ay);
  float d2 = (cx - bx) * (cx - bx) + (cy - by) * (cy - by);

  return sqrtf(GLW_MIN(d1, d2));
}


/**
 *
 */
static float
compute_position(glw_t *w, glw_orientation_t o, float n)
{
  glw_t *p, *c;
  float a, x = 0, m, t, d;

  for(; (p = w->glw_parent) != NULL; w = p) {
    if(p->glw_class->gc_child_orientation != o)
      continue;

    if(p->glw_class->gc_get_child_pos != NULL) {

      a = p->glw_class->gc_get_child_pos(p, w);

    } else {

      a = n * w->glw_norm_weight;
      n = 0.5;
      m = 1;
      t = 0;

      TAILQ_FOREACH(c, &p->glw_childs, glw_parent_link) {
	if(c->glw_flags & GLW_HIDDEN)
	  continue;

	if(c == w)
	  m = 0;
	a += c->glw_norm_weight * m;
	t += c->glw_norm_weight;
      }

      d = 1 - t;
 
      if(d < 1.0) {
	if(o == GLW_ORIENTATION_HORIZONTAL) {
	  switch(p->glw_alignment) {
	  default:
	    break;
	  case LAYOUT_ALIGN_CENTER:
	    a += d / 2;
	    break;
	  case LAYOUT_ALIGN_RIGHT:
	    a += d;
	    break;
	  }
	} else {
	  switch(p->glw_alignment) {
	  default:
	    break;
	  case LAYOUT_ALIGN_CENTER:
	    a += d / 2;
	    break;
	  case LAYOUT_ALIGN_TOP:
	    a += d;
	    break;
	  }
	}
      }
    }

    x = (2 * a) - 1 + (x * w->glw_norm_weight);
  }
  return x;
}


/**
 *
 */
static void
find_candidate(glw_t *w, query_t *query, float d_mul)
{
  glw_t *c;
  float x1, y1, x2, y2, d, d0, d1, dc;

  if(w->glw_flags & (GLW_HIDDEN | GLW_FOCUS_BLOCKED))
    return;
  
  if(glw_is_focusable(w) && w->glw_flags & GLW_NAV_FOCUSABLE) {

    x1 = compute_position(w, GLW_ORIENTATION_HORIZONTAL, 0);
    x2 = compute_position(w, GLW_ORIENTATION_HORIZONTAL, 1);

    y1 = compute_position(w, GLW_ORIENTATION_VERTICAL, 0);
    y2 = compute_position(w, GLW_ORIENTATION_VERTICAL, 1);

    if(query->orientation == GLW_ORIENTATION_VERTICAL) {
      if(query->direction) {
	y2 = y1;
      } else {
	y1 = y2;
      }
    } else {
      if(query->direction) {
	x2 = x1;
      } else {
	x1 = x2;
      }
    }

    d0 = distance_to_line_segment(query->x1, query->y1, x1, y1, x2, y2);
    dc = distance_to_line_segment(query->xc, query->yc, x1, y1, x2, y2);
    d1 = distance_to_line_segment(query->x2, query->y2, x1, y1, x2, y2);

    d = sqrtf(d0 * d0 + dc * dc + d1 * d1);
    d *= d_mul;

    if(d < query->score) {
      query->score = d;
      query->best = w;
    }
  }

  switch(w->glw_class->gc_nav_descend_mode) {

  case GLW_NAV_DESCEND_ALL:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      find_candidate(c, query, d_mul);
    break;
    
  case GLW_NAV_DESCEND_SELECTED:
    if((c = w->glw_selected) != NULL)
      find_candidate(c, query, d_mul);
    break;

  case GLW_NAV_DESCEND_FOCUSED:
    if(w->glw_focused) {
      c = w->glw_focused;
    } else if(query->direction) {
      c = TAILQ_FIRST(&w->glw_childs);
    } else {
      c = TAILQ_LAST(&w->glw_childs, glw_queue);
    }

    if(c != NULL)
      find_candidate(c, query, d_mul);
    break;
  }
}

/**
 *
 */
int
glw_navigate(glw_t *w, event_t *e, int local)
{
  glw_t *w0 = w, *p, *c, *t = NULL, *d, *r = NULL;
  int pagemode = 0, retried = 0;
  int pagecnt;
  query_t query = {0};
  int loop = 1;
  float escape_score = 1;

  query.x1 = compute_position(w, GLW_ORIENTATION_HORIZONTAL, 0);
  query.y1 = compute_position(w, GLW_ORIENTATION_VERTICAL,   0);

  query.x2 = compute_position(w, GLW_ORIENTATION_HORIZONTAL, 1);
  query.y2 = compute_position(w, GLW_ORIENTATION_VERTICAL,   1);

  query.xc = (query.x1 + query.x2) / 2;
  query.yc = (query.y1 + query.y2) / 2;

  if(event_is_action(e, ACTION_FOCUS_PREV)) {

    glw_focus_crawl(w, 0);
    return 1;

  } else if(event_is_action(e, ACTION_FOCUS_NEXT)) {

    glw_focus_crawl(w, 1);
    return 1;

  } else if(event_is_action(e, ACTION_UP)) {
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;

  } else if(event_is_action(e, ACTION_PAGE_UP)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;
    pagemode    = 1;

  } else if(event_is_action(e, ACTION_TOP)) {
    
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 0;
    pagemode    = 2;

  } else if(event_is_action(e, ACTION_DOWN)) {
    
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;

  } else if(event_is_action(e, ACTION_PAGE_DOWN)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;
    pagemode    = 1;

  } else if(event_is_action(e, ACTION_BOTTOM)) {

    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.direction   = 1;
    pagemode    = 2;

  } else if(event_is_action(e, ACTION_LEFT)) {

    query.orientation = GLW_ORIENTATION_HORIZONTAL;
    query.direction   = 0;

  } else if(event_is_action(e, ACTION_RIGHT)) {

    query.orientation = GLW_ORIENTATION_HORIZONTAL;
    query.direction   = 1;

  } else {
    return 0;
  }

 retry:

  query.score = INT_MAX;
  pagecnt = 10;
  c = NULL;

  for(; (p = w->glw_parent) != NULL; w = p) {

    if(local && w->glw_class->gc_flags & GLW_NAVIGATION_SEARCH_BOUNDARY)
      return 0;

    if(w->glw_class->gc_escape_score)
      escape_score *= w->glw_class->gc_escape_score;

    if(w->glw_class->gc_flags & GLW_TRANSFORM_LR_TO_UD && r == NULL)
      r = w;

    switch(p->glw_class->gc_nav_search_mode) {
    case GLW_NAV_SEARCH_NONE:
      break;

    case GLW_NAV_SEARCH_ARRAY:

      if(query.orientation == GLW_ORIENTATION_VERTICAL
	 && query.direction == 0 && w->glw_flags2 & GLW2_TOP_EDGE)
	break;
      if(query.orientation == GLW_ORIENTATION_VERTICAL
	 && query.direction == 1 && w->glw_flags2 & GLW2_BOTTOM_EDGE)
	break;

      if(query.orientation == GLW_ORIENTATION_VERTICAL) {

	int xentries = p->glw_class->gc_get_num_children_x(p);
	if(pagemode == 0) {
	  pagemode = 1;
	  pagecnt = xentries;
	} else if(pagemode == 1) {
	  pagecnt *= xentries;
	}
      }
      goto container;


    case GLW_NAV_SEARCH_BY_ORIENTATION:
      if(pagemode)
	break;
      /* FALLTHROUGH */
    case GLW_NAV_SEARCH_BY_ORIENTATION_WITH_PAGING:
      if(p->glw_class->gc_child_orientation != query.orientation)
	break;
    container:
      c = w;
      loop = 1;
      while(loop) {
	if(query.direction == 1) {
	  /* Down / Right */
	  if(pagemode == 1) {

	    d = glw_next_widget(c);
	    if(d != NULL) {
	      while(pagecnt--) {
		c = d;
		d = glw_next_widget(c);
		if(d == NULL) {
		  loop = 0;
		  break;
		}
	      }
	    } else {
	      loop = 0;
	    }

	  } else if(pagemode == 2) {

	    c = glw_last_widget(p);
	    loop = 0;

	  } else {
	    c = glw_next_widget(c);
	  }

	} else {
	  /* Up / Left */
	  if(pagemode == 1) {

	    d = glw_prev_widget(c);
	    if(d != NULL) {

	      while(pagecnt--) {
		c = d;
		d = glw_prev_widget(c);
		if(d == NULL) {
		  loop = 0;
		  break;
		}
	      }
	    } else {
	      loop = 0;
	    }

	  } else if(pagemode == 2) {
	    
	    c = glw_first_widget(p);
	    loop = 0;

	  } else {
	    c = glw_prev_widget(c);
	  }

	}

	if(c == NULL)
	  break;
	find_candidate(c, &query, escape_score);
	if(query.best)
	  break;
      }
      break;
    }
  }

  t = query.best;


  if(t != NULL) {
    glw_focus_set(t->glw_root, t, GLW_FOCUS_SET_INTERACTIVE);
    return 1;
  } else if(retried == 0 && 
	    r != NULL && query.orientation == GLW_ORIENTATION_HORIZONTAL) {
    retried = 1;
    w = r;
    query.orientation = GLW_ORIENTATION_VERTICAL;
    query.x1 = query.x2 = query.xc = 1 - query.direction * 2;
    goto retry;
  } else if(query.orientation == GLW_ORIENTATION_HORIZONTAL) {
    query.orientation = GLW_ORIENTATION_VERTICAL;
    w = w0;
    query.x1 = query.x2 = query.xc = 1 - query.direction * 2;
    goto retry;
  }

  return 0;
}


