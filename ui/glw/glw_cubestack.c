/*
 *  GL Widgets, GLW_CUBESTACK widget
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

#include <math.h>
#include "glw.h"
#include "glw_cubestack.h"

#define glw_parent_x      glw_parent_misc[0]
#define glw_parent_y      glw_parent_misc[1]
#define glw_parent_z      glw_parent_misc[2]
#define glw_parent_a      glw_parent_misc[3]

static void
glw_cs_render(glw_cubestack_t *cs, glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  rc0 = *rc;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
    if(c->glw_parent_alpha < 0.01)
      continue;

    rc0.rc_alpha = rc->rc_alpha * c->glw_parent_alpha;
    rc0.rc_focused = rc->rc_focused && c == w->glw_focused;
    glw_render_T(c, &rc0, rc);
  }

  rc0.rc_focused = 0;

  TAILQ_FOREACH(c, &cs->fadeout, glw_parent_link) {
    rc0.rc_alpha = rc->rc_alpha * c->glw_parent_alpha;
    glw_render_T(c, &rc0, rc);
  }

}

static void
glw_cs_layout(glw_cubestack_t *cs, glw_t *w, glw_rctx_t *rc)
{
  glw_t *c, *next;
  glw_rctx_t rc0;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
    c->glw_parent_pos.z = GLW_LP(16, c->glw_parent_pos.z, c->glw_parent_z);
    c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, c->glw_parent_a);

    rc0 = *rc;
    rc0.rc_focused &= c == w->glw_focused;

    glw_layout0(c, &rc0);
  }

  for(c = TAILQ_FIRST(&cs->fadeout); c != NULL; c = next) {
    next = TAILQ_NEXT(c, glw_parent_link);

    c->glw_parent_pos.y = GLW_LP(16, c->glw_parent_pos.y, c->glw_parent_y);
    c->glw_parent_pos.z = GLW_LP(16, c->glw_parent_pos.z, c->glw_parent_z);

    c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, 0);

    if(c->glw_parent_alpha < 0.01) {
      TAILQ_REMOVE(&cs->fadeout, c, glw_parent_link);
      glw_destroy0(c);
      continue;
    }

    rc0 = *rc;
    rc0.rc_focused = 0;

    glw_layout0(c, &rc0);
  }
}

static void
glw_cs_reposition(glw_t *w, glw_t *skip)
{
  glw_t *c;
  float z = 0.0f;
  float alpha = 1.0f;

  w->glw_focused = TAILQ_LAST(&w->glw_childs, glw_queue);
  if(skip != NULL && w->glw_focused == skip)
      w->glw_focused = TAILQ_PREV(w->glw_focused,
				   glw_queue, glw_parent_link);

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {
    if(c == skip)
      continue;

    c->glw_parent_a = alpha;
    alpha = 0;

    c->glw_parent_z = z;
    z -= 2.0f;
  }
}


static int
glw_cubestack_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_cubestack_t *cs = (glw_cubestack_t *)w;
  glw_rctx_t *rc = extra;
  glw_t *c;
  int how;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_cs_layout(cs, w, rc);
    return 1;

  case GLW_SIGNAL_RENDER:
    glw_cs_render(cs, w, rc);
    return 1;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;
    c->glw_parent_pos.z = 2.0f;
    glw_cs_reposition(w, NULL);
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = extra;
    glw_cs_reposition(w, c);
    return 1;

  case GLW_SIGNAL_DETACH_CHILD:
    /* Make child disappear in a nice fashion */

    c = extra;
    
    how = TAILQ_LAST(&w->glw_childs, glw_queue) == c;

    TAILQ_REMOVE(&w->glw_childs, c, glw_parent_link);
    c->glw_parent = NULL;

    TAILQ_INSERT_HEAD(&cs->fadeout, c, glw_parent_link);
    c->glw_parent_a = 0;

    if(how) {
      c->glw_parent_z = 2.0f;
    } else {
      c->glw_parent_y = -2.0f;
    }

    glw_cs_reposition(w, NULL);
    return 1;

  case GLW_SIGNAL_EVENT:
    if((c = w->glw_focused) == NULL)
      return 0;
    return glw_signal0(c, GLW_SIGNAL_EVENT, extra);
  }
  return 0;
}


void 
glw_cubestack_ctor(glw_t *w, int init, va_list ap)
{
  glw_cubestack_t *cs = (glw_cubestack_t *)w;

  if(init) {
    TAILQ_INIT(&cs->fadeout);
    glw_signal_handler_int(w, glw_cubestack_callback);
  }
}
