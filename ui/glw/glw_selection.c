/*
 *  GL Widgets, GLW_SELECTION widget
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
#include "glw_i.h"
#include "glw_selection.h"
#include "glw_form.h"

#define glw_parent_y glw_parent_misc[0]

static void
glw_selection_reposition_childs(glw_t *w)
{
  glw_t *c;
  float vd, v;
  
  vd = 2;
  v = -1.0f + vd * 0.5;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDE)
      continue;

    c->glw_parent_y = -v;
    v += vd;
  }
}



/**
 * Return size consumed by child,
 *
 * If zero, we should no longer do any layouting
 */
static int
glw_selection_layout_child(glw_selection_t *s, glw_t *c, glw_rctx_t *rc,
			   float *xdp, float *ydp)
{
  glw_rctx_t rc0 = *rc;
  int issel = c == s->w.glw_selected;

  c->glw_parent_pos.y = c->glw_parent_y - s->ycenter - *ydp;

  if(c->glw_parent_pos.y < -6 || c->glw_parent_pos.y > 6)
    return -1;

  c->glw_parent_alpha = (6 - fabs(c->glw_parent_pos.y)) / 6;


  rc0.rc_focused = rc->rc_focused && issel;

  glw_layout0(c, &rc0);

  if(c->glw_flags & GLW_HIDE)
    return 0;
  glw_link_render_list(&s->w, c);
  return 0;
}



static void
glw_selection_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_selection_t *s = (void *)w;
  glw_t *c, *p, *n, *t;
  float d, xd = 0, yd = 0;

  glw_flush_render_list(w);

  if(rc->rc_focused == 0)
    s->active = 0;

  glw_form_alpha_update(w, rc);

  s->active_prim = GLW_LP(8, s->active_prim, 
			  rc->rc_focused && s->active ? 1 : 0);

  t = w;
  while(t->glw_parent != NULL) {
    if(t->glw_parent->glw_class == GLW_LIST)
      break;

    if(t->glw_parent->glw_class == GLW_CONTAINER_Y) {
      if(TAILQ_FIRST(&t->glw_parent->glw_childs) == t &&
	 TAILQ_NEXT(t, glw_parent_link) == NULL) {
	/* An y-container with only one child is not much use
	   for this expansion model, skip it */
      } else {
	t->glw_weight_extra = s->active_prim * s->expandfactor;
	break;
      }
    }
    t = t->glw_parent;
  }

  if(s->reposition_needed) {
    s->reposition_needed = 0;
    glw_selection_reposition_childs(w);
  }

  if((c = w->glw_selected) == NULL) {
    c = TAILQ_FIRST(&w->glw_childs);
    if(c == NULL) {
      w->glw_flags &= ~GLW_SELECTABLE;
      return;
    }
    w->glw_selected = c;
  }


  glw_selection_layout_child(s, c, rc, &xd, &yd);
  if(s->active) {
    p = c;
    do {
      p = TAILQ_PREV(p, glw_queue, glw_parent_link);
    } while(p != NULL && !glw_selection_layout_child(s, p, rc, &xd, &yd));
    
    n = c;
    do {
      n = TAILQ_NEXT(n, glw_parent_link);
    } while(n != NULL && !glw_selection_layout_child(s, n, rc, &xd, &yd));
  }
  d = c->glw_parent_y - s->ycenter_target;
  if(d > 0.7f)
    s->ycenter_target += 2;
  if(d < -0.7f) 
    s->ycenter_target -= 2;
  
  s->ycenter = GLW_LP(6, s->ycenter, s->ycenter_target);
}





static void
glw_selection_render(glw_t *w, glw_rctx_t *rc)
{
  glw_rctx_t rc0 = *rc;
  glw_t *c;
  float alpha = glw_form_alpha_get(w) * rc->rc_alpha;

  if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && rc->rc_focused)
    glw_form_cursor_set(rc);

  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link) {

    rc0.rc_alpha = alpha * c->glw_parent_alpha;
    rc0.rc_focused = rc->rc_focused && c == w->glw_selected;

    glw_render_T(c, &rc0, rc);
  }
}



static int
glw_selection_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_selection_t *s = (void *)w;
  glw_t *c = w->glw_selected, *n;
  glw_event_t *ge;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_selection_layout(w, rc);
    return 1;

  case GLW_SIGNAL_RENDER:
    glw_selection_render(w, rc);
    return 1;

  case GLW_SIGNAL_CHILD_CREATED:
    if(w->glw_selected == NULL) {
      c = w->glw_selected = extra;
      glw_signal0(w, GLW_SIGNAL_SELECTED_UPDATE_ADVISORY, c);
    }
    w->glw_flags |= GLW_SELECTABLE;
    s->reposition_needed = 1;
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(TAILQ_FIRST(&w->glw_childs) == NULL)
      w->glw_flags &= ~GLW_SELECTABLE;
    
    return 1;

  case GLW_SIGNAL_CHILD_VISIBLE:
  case GLW_SIGNAL_CHILD_HIDDEN:
    s->reposition_needed = 1;
    return 1;

  case GLW_SIGNAL_EVENT:
    if(c == NULL)
      return 0;

    ge = extra;
    n = NULL;

    if(ge->ge_type == GEV_ENTER) {
      s->active = !s->active;

      if(s->active)
	return 1;
    }

    if(glw_signal0(c, GLW_SIGNAL_EVENT, ge))
      return 1;

    switch(ge->ge_type) {
    default:
      break;

    case GEV_UP:
      if(!s->active)
	break;
      if((n = glw_get_prev_n(c, 1)) == NULL)
	return 1;
      break;

    case GEV_DOWN:
      if(!s->active)
	break;
      if((n = glw_get_next_n(c, 1)) == NULL)
	return 1;
      break;

    case GEV_INCR:
      if((n = glw_get_next_n(c, 1)) == NULL)
	n = TAILQ_FIRST(&w->glw_childs);

      glw_event_signal_simple(n, GEV_ENTER);
      glw_signal0(n, GLW_SIGNAL_SELECTED_SELF, NULL);
      break;

    case GEV_DECR:
      if((n = glw_get_prev_n(c, 1)) == NULL)
	n = TAILQ_LAST(&w->glw_childs, glw_queue);

      glw_event_signal_simple(n, GEV_ENTER);
      glw_signal0(n, GLW_SIGNAL_SELECTED_SELF, NULL);
      break;
    }

    if(n != NULL) {
      if(n != c) {
	w->glw_selected = n;
	glw_signal0(w, GLW_SIGNAL_SELECTED_CHANGED, n);
      }
      return 1;
    }

    return glw_navigate(w, extra);
  }
  return 0;
}



void 
glw_selection_ctor(glw_t *w, int init, va_list ap)
{
  glw_selection_t *s = (void *)w;
  glw_attribute_t attrib;

  if(init) {
    s->expandfactor = 4;
    glw_signal_handler_int(w, glw_selection_callback);
  }


  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_EXPAND:
      s->expandfactor = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
