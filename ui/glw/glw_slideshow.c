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

#include "glw.h"
#include "glw_i.h"
#include "glw_slideshow.h"

/*
 *
 */
static int
glw_slideshow_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_slideshow_t *s = (glw_slideshow_t *)w;
  glw_t *c, *p, *n;
  glw_rctx_t *rc = extra, rc0;
  float delta;
  event_t *e;

  switch(signal) {
  case GLW_SIGNAL_RENDER:
    if((c = w->glw_selected) == NULL)
      return 1;

    p = TAILQ_PREV(c, glw_queue, glw_parent_link);
    if(p == NULL)
      p = TAILQ_LAST(&w->glw_childs, glw_queue);
    if(p != NULL && p != c) {
      if(p->glw_parent_alpha > 0.01) {
	rc0 = *rc;
	rc0.rc_alpha *= p->glw_parent_alpha;
	glw_render(p, &rc0);
      }
    }

    rc0 = *rc;
    rc0.rc_alpha *= c->glw_parent_alpha;
    glw_render(c, &rc0);

    n = TAILQ_NEXT(c, glw_parent_link);
    if(n == NULL)
      n = TAILQ_FIRST(&w->glw_childs);
    if(n != NULL && n != c) {
      if(n->glw_parent_alpha > 0.01) {
	rc0 = *rc;
	rc0.rc_alpha *= n->glw_parent_alpha;
	glw_render(n, &rc0);
      }
    }
    return 1;

  case GLW_SIGNAL_LAYOUT:

    if(w->glw_time == 0) {
      s->displaytime = INT32_MAX;
      delta = 0.1f;
    } else {
      s->displaytime = glw_framerate * w->glw_time;
      delta = 1 / (s->displaytime * 0.1);
    }

    
    if((c = w->glw_selected) == NULL)
      c = w->glw_selected = TAILQ_FIRST(&w->glw_childs);
    if(c == NULL)
      return 1;

    if(s->timer >= s->displaytime) {
      c = w->glw_selected = TAILQ_NEXT(c, glw_parent_link);
      if(c == NULL)
	c = w->glw_selected = TAILQ_FIRST(&w->glw_childs);
      s->timer = 0;
    }
  
    s->timer++;

    glw_layout(c, rc);
    c->glw_parent_alpha = GLW_MIN(c->glw_parent_alpha + delta, 1.0f);

    /**
     * Keep previous and next images 'hot' (ie, loaded into texture memroy)
     */
    p = TAILQ_PREV(c, glw_queue, glw_parent_link);
    if(p == NULL)
      p = TAILQ_LAST(&w->glw_childs, glw_queue);
    if(p != NULL && p != c) {
      p->glw_parent_alpha = GLW_MAX(p->glw_parent_alpha - delta, 0.0f);
      glw_layout(p, rc);
    }

    n = TAILQ_NEXT(c, glw_parent_link);
    if(n == NULL)
      n = TAILQ_FIRST(&w->glw_childs);
    if(n != NULL && n != c) {
      n->glw_parent_alpha = GLW_MAX(n->glw_parent_alpha - delta, 0.0f);
      glw_layout(n, rc);
    }
    return 1;

  case GLW_SIGNAL_EVENT:
    e = extra;
    switch(e->e_type) {
    case EVENT_INCR:
      c = w->glw_selected ? TAILQ_NEXT(w->glw_selected, glw_parent_link) : NULL;
      if(c == NULL)
	c = TAILQ_FIRST(&w->glw_childs);
      w->glw_selected = c;
      s->timer = 0;
      return 1;

    case EVENT_DECR:
      c = w->glw_selected ? TAILQ_PREV(w->glw_selected, glw_queue,
				       glw_parent_link) : NULL;
      if(c == NULL)
	c = TAILQ_LAST(&w->glw_childs, glw_queue);
      w->glw_selected = c;
      s->timer = 0;
      return 1;
    default:
      return 0;
    }
  default:
    break;
  }

  return 0;
}

void
glw_slideshow_ctor(glw_t *w, int init, va_list ap)
{
  //  glw_slideshow_t *s = (glw_slideshow_t *)w;

  if(init) {
    glw_signal_handler_int(w, glw_slideshow_callback);
  }
}

