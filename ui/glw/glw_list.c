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

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(w->glw_class == GLW_LIST_X) {
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
  int issel = l->focused_child == c;
  float alpha = 1.0;
  float v;

  c->glw_parent_pos.x = c->glw_parent_x - l->xcenter - *xdp;
  c->glw_parent_pos.y = c->glw_parent_y - l->ycenter - *ydp;

  if(c->glw_parent_pos.y < -thres || c->glw_parent_pos.y > thres ||
     c->glw_parent_pos.x < -thres || c->glw_parent_pos.x > thres)
    return -1;

  if(l->w.glw_flags & GLW_EXPAND_CHILDS) {
    if(l->w.glw_class == GLW_LIST_X) {
      c->glw_parent_pos.x -= expansion_factor / l->visible;
    } else {
      c->glw_parent_pos.y += expansion_factor / l->visible;
    }
  }

  scale = 1.0f;
  zdisplace = 0.0f;

  xs = ys = scale;

  /**
   * We abuse tpos.z for zoom ramping
   */
  if(l->w.glw_flags & GLW_EXPAND_CHILDS) {
    rc0.rc_zoom = c->glw_parent_zoom;

    if(l->w.glw_class == GLW_LIST_X) {
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
  c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, alpha);

  if(l->w.glw_class == GLW_LIST_X) {
    v = fabs(c->glw_parent_pos.x) - 0.9f;
  } else {
    v = fabs(c->glw_parent_pos.y) - 0.9f;
  }

  v = GLW_LERP(GLW_MIN(GLW_MAX(v * 6., 0), 1), 1.0f, 0.0f);
  c->glw_parent_alpha *= v;

  rc0.rc_exp_req = 1;
  glw_layout0(c, &rc0);
  c->glw_parent_zoom = GLW_LP(8, c->glw_parent_zoom, 
			      issel ? rc0.rc_exp_req - 1.0 : 0);

  if(c->glw_parent_alpha < 0.02)
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
  float expansion_factor;
  
  if(w->glw_alpha < 0.01)
    return;

  glw_flush_render_list(w);

  if(l->reposition_needed) {

    if(w->glw_class == GLW_LIST_X) {
      l->xs = 1.0f / (float)l->visible;
      l->ys = 1.0f;
      l->ycenter = l->ycenter_target = 0;
    } else {
      l->xs = 1.0f;
      l->ys = 1.0f / (float)l->visible;
      l->xcenter = l->xcenter_target = 0;
    }
    l->reposition_needed = 0;
    glw_list_reposition_childs(l);
  }

  c = glw_get_indirectly_focused_child(w);

  if(c != NULL) 
    l->focused_child = c;
  else
    c = l->focused_child;

  if(c == NULL) {
    c = TAILQ_FIRST(&w->glw_childs);
    if(c == NULL)
      return;
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
    glw_render_TS(c, &rc0, rc);
  }
}



static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_list_t *l = (void *)w;

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
  case GLW_SIGNAL_CHILD_DESTROYED:
    if(l->focused_child == extra)
      l->focused_child = NULL;

    l->reposition_needed = 1;
    return 0;

  case GLW_SIGNAL_SELECT:
    w->glw_selected = extra;
    return 0;
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
    h->visible = 5;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_SLICES:
      h->reposition_needed = 1;
      h->visible = va_arg(ap, int);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
