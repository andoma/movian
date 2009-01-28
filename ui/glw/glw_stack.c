/*
 *  GL Widgets, GLW_STACK -widgets
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

#include "glw.h"
#include "glw_stack.h"

#define glw_parent_caspect glw_parent_misc[0]

static void 
glw_stack_x_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float pa  = rc->rc_size_x / rc->rc_size_y;
  float ipa = rc->rc_size_y / rc->rc_size_x;
  float ta = 0;
  float n;
  float a;
  float ys;
  float d = -1;
  glw_rctx_t rc0;
  int z = 0;
  float tw = 0;

  glw_flush_render_list(w);

  if(w->glw_alpha < 0.01)
    return;

  rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);
  rc0 = *rc;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_aspect > 0) {
      c->glw_parent_caspect = c->glw_aspect;
      ta += c->glw_parent_caspect;
    } else {
      tw += c->glw_conf_weight;
      z++;
    }
  }

  if(ta > pa) {
    n = pa / ta;
    ipa *= n;
    ys = n;

  } else if(z) {

    n = pa - ta;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(c->glw_aspect == 0)
	c->glw_parent_caspect = n * c->glw_conf_weight / tw;
    
    ys = 1;

  } else {
    n = ta / pa;

    switch(w->glw_alignment) {
    default:
      break;

    case GLW_ALIGN_CENTER:
      d += 1 - n;
      break;

    case GLW_ALIGN_RIGHT:
      d += (1 - n) * 2;
      break;
    }
    ys = 1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    a = c->glw_parent_caspect * ipa;

    c->glw_norm_weight = a;

    c->glw_parent_pos.x = d + a + w->glw_displacement.x;
    c->glw_parent_pos.y = 0.0   + w->glw_displacement.y;
    c->glw_parent_pos.z = 0.0   + w->glw_displacement.z;

    c->glw_parent_scale.x = a;
    c->glw_parent_scale.y = ys;
    c->glw_parent_scale.z = a;
      
    rc0.rc_size_x = rc->rc_size_x * a;
    rc0.rc_size_y = rc->rc_size_y * ys;

    glw_layout0(c, &rc0);
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, rc0.rc_exp_req);

    if(a > 0.01)
      glw_link_render_list(w, c);

    d += a * 2;
  }
}


static void 
glw_stack_y_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float pa  = rc->rc_size_y / rc->rc_size_x;
  float ipa = rc->rc_size_x / rc->rc_size_y;
  float ta = 0;
  float n;
  float a;
  float xs;
  float d = 1;
  glw_rctx_t rc0;
  int z = 0;
  float tw = 0;

  glw_flush_render_list(w);

  if(w->glw_alpha < 0.01)
    return;

  rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);
  rc0 = *rc;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_aspect > 0) {
      c->glw_parent_caspect = 1 / c->glw_aspect;
      ta += c->glw_parent_caspect;
    } else {
      z++;
      tw += c->glw_conf_weight;
    }
  }

  if(ta > pa) {
    n = pa / ta;
    ipa *= n;
    xs = n;

  } else if(z) {
    n = pa - ta;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c->glw_aspect == 0)
	c->glw_parent_caspect = n * c->glw_conf_weight / tw;
    }
    
    xs = 1;

  } else {

    n = ta / pa;

    switch(w->glw_alignment) {
    default:
      break;

    case GLW_ALIGN_CENTER:
      d -= 1 - n;
      break;

    case GLW_ALIGN_BOTTOM:
      d -= (1 - n) * 2;
      break;
    }
    xs = 1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    a = c->glw_parent_caspect * ipa;

    c->glw_norm_weight = a;

    c->glw_parent_pos.x = 0     + w->glw_displacement.x;
    c->glw_parent_pos.y = d - a + w->glw_displacement.y;
    c->glw_parent_pos.z = 0.0   + w->glw_displacement.z;

    c->glw_parent_scale.x = xs;
    c->glw_parent_scale.y = a;
    c->glw_parent_scale.z = a;
      
    rc0.rc_size_x = rc->rc_size_x * xs;
    rc0.rc_size_y = rc->rc_size_y * a;

    glw_layout0(c, &rc0);
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, rc0.rc_exp_req);

    if(a > 0.01)
      glw_link_render_list(w, c);

    d -= a * 2;
  }
}



static void
glw_stack_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  glw_rctx_t rc0 = *rc;

  if(alpha < 0.01)
    return;

  rc0.rc_alpha = alpha;
  
  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  TAILQ_FOREACH(c, &w->glw_render_list, glw_render_link)
    glw_render_TS(c, &rc0, rc);
}


static int
glw_stack_callback(glw_t *w, void *opaque, glw_signal_t signal,
		       void *extra)
{
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    if(w->glw_class == GLW_STACK_X)
      glw_stack_x_layout(w, extra);
    else
      glw_stack_y_layout(w, extra);

  case GLW_SIGNAL_RENDER:
    glw_stack_render(w, extra);
    break;

  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;

  default:
    break;
  }
  return 0;
}


void 
glw_stack_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_stack_callback);
}
