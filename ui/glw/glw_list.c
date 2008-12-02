/*
 *  GL Widgets, GLW_LIST widget
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
#include "glw_list.h"
#include "glw_form.h"

#define glw_parent_x      glw_parent_misc[0]
#define glw_parent_y      glw_parent_misc[1]
#define glw_parent_zoom   glw_parent_misc[2]

static void
glw_list_reposition_childs(glw_list_t *l)
{
  glw_t *c, *w = &l->w;
  float vd, v;
  
  vd = 2. / l->visible;
  v = -1.0f + vd * 0.5;

  if(l->orientation == 0)
    return;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDE)
      continue;

    if(l->orientation == GLW_ORIENTATION_HORIZONTAL) {
      c->glw_parent_x = w->glw_displacement.x + v;
      c->glw_parent_y = w->glw_displacement.y;
    } else {
      c->glw_parent_x = w->glw_displacement.x;
      c->glw_parent_y = w->glw_displacement.y - v;
    }
    c->glw_parent_pos.z = w->glw_displacement.z;
    v += vd;
  }
}



/**
 * Return size consumed by child,
 *
 * If zero, we should no longer do any layouting
 */
static int
glw_list_layout_child(glw_list_t *l, glw_t *c, glw_rctx_t *rc,
		      float *xdp, float *ydp, float thres,
		      float expansion_factor)
{
  glw_rctx_t rc0 = *rc;
  float scale, xs, ys, zdisplace;
  int issel = c == l->w.glw_focused;
  float alpha = 1.0;
  float v;

  c->glw_parent_pos.x = c->glw_parent_x - l->xcenter - *xdp;
  c->glw_parent_pos.y = c->glw_parent_y - l->ycenter - *ydp;

  if(c->glw_parent_pos.y < -thres || c->glw_parent_pos.y > thres ||
     c->glw_parent_pos.x < -thres || c->glw_parent_pos.x > thres)
    return -1;

  if(l->w.glw_flags & GLW_EXPAND_CHILDS) {
    if(l->orientation == GLW_ORIENTATION_HORIZONTAL) {
      c->glw_parent_pos.x -= expansion_factor / l->visible;
    } else {
      c->glw_parent_pos.y += expansion_factor / l->visible;
    }
  }

  if(l->w.glw_flags & GLW_SCALE_CHILDS) {
    scale     = issel ? 1.0 : 0.5;
    alpha    *= issel ? 2.0 : 0.5;
    zdisplace = issel ? 0.0 : -1.0;
    zdisplace = 0.0f;
  } else {
    scale = 1.0f;
    zdisplace = 0.0f;
  }

  xs = ys = scale;

  /**
   * We abuse tpos.z for zoom ramping
   */
  if(l->w.glw_flags & GLW_EXPAND_CHILDS) {
    c->glw_parent_zoom = GLW_LP(8, c->glw_parent_zoom, 
				issel ? c->glw_weight - 1.0 : 0);
    rc0.rc_zoom = c->glw_parent_zoom;

    if(l->orientation == GLW_ORIENTATION_HORIZONTAL) {
      xs *= c->glw_parent_zoom + 1.;
      *xdp -= 2 * c->glw_parent_zoom / (float)l->visible;
      c->glw_parent_pos.x += c->glw_parent_zoom / (float)l->visible;

    } else {
      ys *= c->glw_parent_zoom + 1.;
      *ydp += 2 * c->glw_parent_zoom / (float)l->visible;
      c->glw_parent_pos.y -= c->glw_parent_zoom / (float)l->visible;

    }

    c->glw_parent_scale.x = l->xs * xs;
    c->glw_parent_scale.y = l->ys * ys;
  } else {
    c->glw_parent_scale.x = GLW_LP(8, c->glw_parent_scale.x, l->xs * xs);
    c->glw_parent_scale.y = GLW_LP(8, c->glw_parent_scale.y, l->ys * ys);
  }

  c->glw_parent_scale.z = (c->glw_parent_scale.x + c->glw_parent_scale.y) / 2;

  c->glw_parent_pos.z   = GLW_LP(8, c->glw_parent_pos.z,    zdisplace);

  rc0.rc_aspect = rc->rc_aspect * c->glw_parent_scale.x / c->glw_parent_scale.y;
  rc0.rc_focused = rc->rc_focused && issel;

  c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, alpha);

  switch(l->orientation) {
  case GLW_ORIENTATION_HORIZONTAL:
    v = fabs(c->glw_parent_pos.x) - 1.0f;
    break;
      
  case GLW_ORIENTATION_VERTICAL:
    v = fabs(c->glw_parent_pos.y) - 1.0f;
    break;

  default:
    v = 0;
    break;
  }

  v = GLW_LERP(GLW_MIN(GLW_MAX(v * 6., 0), 1), 1.0f, 0.0f);
  c->glw_parent_alpha *= v;
  glw_layout0(c, &rc0);

  if(c->glw_parent_alpha < 0.02 || c->glw_flags & GLW_HIDE)
    return 0;

  glw_link_render_list(&l->w, c);
  return 0;
}



static void
glw_list_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_list_t *l = (void *)w;
  glw_t *c, *p, *n;
  float d, xx, yy, xd = 0, yd = 0, thres;
  glw_orientation_t o;
  float expansion_factor;
  
  if(w->glw_alpha < 0.01)
    return;

  glw_flush_render_list(w);

  if((o = l->fixed_orientation) == GLW_ORIENTATION_UNKNOWN)
    o = rc->rc_aspect > 1.0 ? GLW_ORIENTATION_HORIZONTAL : 
      GLW_ORIENTATION_VERTICAL;
  
  if(o != l->orientation) {
    /* Orientation changed */
    l->orientation = o;
    l->reposition_needed = 1;
  }

  if(l->reposition_needed) {

    switch(o) {
    case GLW_ORIENTATION_HORIZONTAL:
      l->xs = 1.0f / (float)l->visible;
      l->ys = 1.0f;
      l->ycenter = l->ycenter_target = 0;
      break;
      
    case GLW_ORIENTATION_VERTICAL:
      l->xs = 1.0f;
      l->ys = 1.0f / (float)l->visible;
      l->xcenter = l->xcenter_target = 0;
      break;
      
    default:
      return;
    }
    l->reposition_needed = 0;
    glw_list_reposition_childs(l);
  }

  if((c = w->glw_focused) == NULL) {
    c = TAILQ_FIRST(&w->glw_childs);
    if(c == NULL) {
      /* If we have nothing to layout we should make sure our
	 parent does not have us focused, it will mess up focus */
    
      if(w->glw_parent->glw_focused == w)
	w->glw_parent->glw_focused = NULL;
      return;
    }

    w->glw_focused = c;
    glw_signal0(c, GLW_SIGNAL_FOCUSED_UPDATE_ADVISORY, NULL);
  }

  thres = 1 + 10 * 1.0f / (float)l->visible;

  if(l->w.glw_flags & GLW_EXPAND_CHILDS) {

    l->expansion_factor = GLW_LP(8, l->expansion_factor, c->glw_weight - 1.0f);

    expansion_factor = l->expansion_factor;

    n = c;
    while(1) {
 
      p = TAILQ_PREV(n, glw_queue, glw_parent_link);
      if(p == NULL)
	break;

      xx = p->glw_parent_x - l->xcenter;
      yy = p->glw_parent_y - l->ycenter;

      if(yy < -thres || yy > thres || xx < -thres || xx > thres)
	break;
    
      n = p;
    }


    while(1) {
      if(glw_list_layout_child(l, n, rc, &xd, &yd, thres, expansion_factor))
	break;

      n = TAILQ_NEXT(n, glw_parent_link);
      if(n == NULL)
	break;
    }

  } else {

   glw_list_layout_child(l, c, rc, &xd, &yd, thres, 0);
    p = c;
    do {
      p = TAILQ_PREV(p, glw_queue, glw_parent_link);
    } while(p != NULL && !glw_list_layout_child(l, p, rc, &xd, &yd, thres, 0));
  
    n = c;
    do {
      n = TAILQ_NEXT(n, glw_parent_link);
    } while(n != NULL && !glw_list_layout_child(l, n, rc, &xd, &yd, thres, 0));
  }



  d = c->glw_parent_y - l->ycenter_target;
  if(d > 0.7f)
    l->ycenter_target += l->ys * 2;
  if(d < -0.7f) 
    l->ycenter_target -= l->ys * 2;
  
  l->ycenter = GLW_LP(16, l->ycenter, l->ycenter_target);


  d = c->glw_parent_x - l->xcenter_target;
  if(d > 0.7f)
    l->xcenter_target += l->xs * 2;
  if(d < -0.7f) 
    l->xcenter_target -= l->xs * 2;
  
  l->xcenter = GLW_LP(16, l->xcenter, l->xcenter_target);
}




static void
glw_list_render(glw_t *w, glw_rctx_t *rc)
{
  glw_list_t *l = (glw_list_t *)w;
  glw_rctx_t rc0 = *rc;
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01)
    return;

  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link) {

    rc0.rc_alpha  = alpha * c->glw_parent_alpha;
    if(l->w.glw_flags & GLW_EXPAND_CHILDS)
      rc0.rc_zoom = c->glw_parent_zoom;

    rc0.rc_focused = rc->rc_focused && c == w->glw_focused;

    glw_render_TS(c, &rc0, rc);
  }
}



static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_list_t *h = (void *)w;
  glw_t *c = w->glw_focused, *n;
  event_t *e;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_list_layout(w, rc);
    return 0;

  case GLW_SIGNAL_RENDER:
    glw_list_render(w, rc);
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    if(w->glw_focused == NULL) {
      c = w->glw_focused = extra;
      glw_signal0(c, GLW_SIGNAL_FOCUSED_UPDATE_ADVISORY, NULL);
    }
    /* FALLTHRU */
  case GLW_SIGNAL_CHILD_DESTROYED:
  case GLW_SIGNAL_CHILD_VISIBLE:
  case GLW_SIGNAL_CHILD_HIDDEN:
    h->reposition_needed = 1;
    return 0;

  case GLW_SIGNAL_FOCUS:
    w->glw_focused = c;
    return 0;

  case GLW_SIGNAL_EVENT:
    if(w->glw_alpha < 0.01)
      return 0; /* If we're not visible, don't consume events */

    if(c == NULL)
      return 0;

    e = extra;
    n = NULL;

    if(glw_signal0(c, GLW_SIGNAL_EVENT, e))
      return 1;

    switch(e->e_type) {
    default:
      break;

    case EVENT_LEFT:
      if(h->orientation == GLW_ORIENTATION_HORIZONTAL)
	n = glw_get_prev_n(c, 1);
      break;

    case EVENT_RIGHT:
      if(h->orientation == GLW_ORIENTATION_HORIZONTAL)
	n = glw_get_next_n(c, 1);
      break;

    case EVENT_UP:
      if(h->orientation == GLW_ORIENTATION_VERTICAL)
	n = glw_get_prev_n(c, 1);
      break;

    case EVENT_DOWN:
      if(h->orientation == GLW_ORIENTATION_VERTICAL)
	n = glw_get_next_n(c, 1);
      break;

    case EVENT_INCR:
      n = glw_get_next_n(c, 1);
      if(n == NULL)
	n = TAILQ_FIRST(&w->glw_childs);
      break;

    case EVENT_DECR:
      n = glw_get_prev_n(c, 1);
      break;
    }

    if(n != NULL) {
      if(n != c) {
	w->glw_focused = n;
	glw_signal0(n, GLW_SIGNAL_FOCUSED_UPDATE, NULL);
      }
      return 1;
    }

    return glw_navigate(w, extra);
  }
  return 0;
}



void 
glw_list_ctor(glw_t *w, int init, va_list ap)
{
  glw_list_t *h = (void *)w;
  glw_attribute_t attrib;

  if(init) {
    glw_signal_handler_int(w, glw_list_callback);
    w->glw_flags |= GLW_FOCUSABLE;
    h->visible = 5;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_SLICES:
      h->reposition_needed = 1;
      h->visible = va_arg(ap, int);
      break;
    case GLW_ATTRIB_ORIENTATION:
      h->fixed_orientation = va_arg(ap, int);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
