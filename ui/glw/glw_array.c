/*
 *  GL Widgets, GLW_ARRAY widget
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

#include <math.h>
#include "glw.h"
#include "glw_array.h"

#define glw_parent_x glw_parent_misc[0]
#define glw_parent_y glw_parent_misc[1]

static void
glw_array_reposition_childs(glw_array_t *a)
{
  glw_t *w = &a->w;
  glw_t *c;
  float xs = 1.0f / (float)a->xvisible;
  float ys = 1.0f / (float)a->yvisible;
  int x = 0, y = 0;
  float xx;

  if(a->visiblechilds < a->xvisible) {
    /* if the number of childs does not even span a row, we 
       center them instead */

    xx = -xs * (float)a->visiblechilds + xs;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

      if(c->glw_flags & GLW_HIDE)
	continue;

      c->glw_parent_x = xx;
      c->glw_parent_y =  1.0 - ys;

      xx += 2 * xs;
    }
    return;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_flags & GLW_HIDE)
      continue;

    c->glw_parent_x = -1.0 + xs + 2 * xs * x;
    c->glw_parent_y =  1.0 - ys - 2 * ys * y;

    x++;
    if(x == a->xvisible) {
      x = 0;
      y++;
    }
  }
}


static int
glw_array_layout_child(glw_array_t *a, glw_t *c, glw_rctx_t *rc,
		       float xs, float ys)
{
  glw_rctx_t rc0 = *rc;
  int issel = c == a->w.glw_focused;

  c->glw_parent_pos.x = c->glw_parent_x;
  c->glw_parent_pos.y = c->glw_parent_y - a->ycenter;

  if(c->glw_parent_pos.y < -4 || c->glw_parent_pos.y > 4)
    return 1;

  c->glw_parent_scale.x = xs;
  c->glw_parent_scale.y = ys;
  c->glw_parent_scale.z = 1.0f; //(c->glw_scale.x + c->glw_scale.y) / 2;
  rc0.rc_aspect = rc->rc_aspect * c->glw_parent_scale.x / c->glw_parent_scale.y;
  rc0.rc_focused = rc->rc_focused && issel;

  glw_layout0(c, &rc0);

  if(c->glw_flags & GLW_HIDE)
    return 0;

  glw_link_render_list(&a->w, c);
  return 0;
}





static void
glw_array_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_array_t *a = (void *)w;
  glw_t *c, *n, *p;
  float xs = 1.0f / (float)a->xvisible;
  float ys = 1.0f / (float)a->yvisible;
  float d;

  glw_flush_render_list(w);

  do {

    if(a->reposition_needed) {
      a->reposition_needed = 0;
      glw_array_reposition_childs(a);
    }

    if(w->glw_focused == NULL) {
      c = TAILQ_FIRST(&w->glw_childs);
      if(c == NULL) {
	/* If we have nothing to layout we should make sure our
	   parent does not have us selected, it will mess up focus */
	
	if(w->glw_parent->glw_focused == w)
	  w->glw_parent->glw_focused = NULL;
	return;
      }

      a->curx = c->glw_parent_pos.x;
      a->cury = c->glw_parent_pos.y;
      w->glw_focused = c;
      glw_signal0(c, GLW_SIGNAL_FOCUSED_UPDATE_ADVISORY, NULL);
    }
    c = w->glw_focused;

    glw_array_layout_child(a, c, rc, xs, ys);

    p = c;
    do {
      p = TAILQ_PREV(p, glw_queue, glw_parent_link);
    } while(p != NULL && !glw_array_layout_child(a, p, rc, xs, ys));

    n = c;
    do {
      n = TAILQ_NEXT(n, glw_parent_link);
    } while(n != NULL && !glw_array_layout_child(a, n, rc, xs, ys));
  

    d = c->glw_parent_y - a->ycenter_target;
    if(d > 0.7f)
      a->ycenter_target += ys * 2;
    if(d < -0.7f) 
      a->ycenter_target -= ys * 2;

    a->ycenter_target = GLW_MIN(a->ycenter_target, 0.0f);
    a->ycenter = GLW_LP(8, a->ycenter, a->ycenter_target);
  } while(0 && a->reposition_needed);
}




static void
glw_array_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0 = *rc;

  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link) {

    if(c->glw_parent_pos.y < -1.5 || c->glw_parent_pos.y > 1.5)
      continue;

    rc0.rc_focused = rc->rc_focused && c == w->glw_focused;
    glw_render_TS(c, &rc0, rc);
  }
}



static int
glw_array_callback(glw_t *w, void *opaque, glw_signal_t signal,
		   void *extra)
{
  glw_array_t *a = (void *)w;
  glw_t *n, *c = w->glw_focused;
  event_t *e;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_array_layout(w, extra);
    return 0;

  case GLW_SIGNAL_RENDER:
    glw_array_render(w, extra);
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    if(w->glw_focused == NULL) {
      c = w->glw_focused = extra;
      glw_signal0(c, GLW_SIGNAL_FOCUSED_UPDATE_ADVISORY, NULL);
    }
    /* FALLTHRU */
  case GLW_SIGNAL_CHILD_VISIBLE:
    a->visiblechilds++;
    a->reposition_needed = 1;
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
  case GLW_SIGNAL_CHILD_HIDDEN:
    a->visiblechilds--;
    a->reposition_needed = 1;
    return 0;

  case GLW_SIGNAL_FOCUS:
    w->glw_focused = c;
    a->curx = c->glw_parent_x;
    a->cury = c->glw_parent_y;
    return 0;

  case GLW_SIGNAL_EVENT:
    if(c == NULL)
      return 0;

    e = extra;
    n = NULL;

    if(glw_signal0(c, GLW_SIGNAL_EVENT, e))
      return 1;

    switch(e->e_type) {
    case EVENT_UP:
      n = glw_get_prev_n_all(c, a->xvisible);
      break;
    case EVENT_DOWN:
      n = glw_get_next_n_all(c, a->xvisible);
      break;
    case EVENT_LEFT:
      n = glw_get_prev_n(c, 1);
      break;
    case EVENT_RIGHT:
      n = glw_get_next_n(c, 1);
      break;
    default:
      break;
    }

    if(n != NULL) {
      glw_signal0(n, GLW_SIGNAL_FOCUSED_UPDATE, NULL);
      w->glw_focused = n;
      a->curx = n->glw_parent_x;
      a->cury = n->glw_parent_y;
      return 1;
    }
    return glw_navigate(w, extra);
  }
  return 0;
}



void 
glw_array_ctor(glw_t *w, int init, va_list ap)
{
  glw_array_t *a = (void *)w;
  glw_attribute_t attrib;

  if(init) {
    glw_signal_handler_int(w, glw_array_callback);
    w->glw_flags |= GLW_FOCUSABLE;
    a->cursor_width = 0.1f;
    a->curx = -2;
    a->cury = -2;

    a->xvisible = 1;
    a->yvisible = 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_X_SLICES:
      a->xvisible = va_arg(ap, int);
      break;
    case GLW_ATTRIB_Y_SLICES:
      a->yvisible = va_arg(ap, int);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
