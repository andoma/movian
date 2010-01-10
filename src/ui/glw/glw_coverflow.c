/*
 *  GL Widgets, Coverflow
 *  Copyright (C) 2010 Andreas Öman
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
#include "glw_coverflow.h"

/**
 *
 */
static void
layout(glw_coverflow_t *gc, glw_rctx_t *rc)
{
  float n = 0;
  glw_t *c, *rstart = NULL;
  glw_rctx_t rc0;
  float aspect = rc->rc_size_x / rc->rc_size_y;

  if(aspect > 1.0) {
    gc->xs = 1.0 / aspect;
    gc->ys = 1;
  } else {
    gc->xs = 1;
    gc->ys = aspect;
  }

  rc0 = *rc;
  rc0.rc_size_x *= gc->xs;
  rc0.rc_size_y *= gc->ys;

  gc->pos = GLW_LP(6, gc->pos, gc->pos_target);

  TAILQ_FOREACH(c, &gc->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    
    c->glw_parent_misc[0] = n - gc->pos;

    if(rstart == NULL ||
       fabs(rstart->glw_parent_misc[0]) > fabs(c->glw_parent_misc[0]))
      rstart = c;

    glw_signal0(c, GLW_SIGNAL_LAYOUT, &rc0);

    if(gc->scroll_to_me == c) {
      gc->pos_target = n;
    }
    n++;
  }
  gc->rstart = rstart;
}

/**
 *
 */
static void
renderone(glw_rctx_t *rc, glw_t *c, glw_coverflow_t *gc)
{
  float v, r, nv;
  glw_rctx_t rc0;

  if(c->glw_flags & GLW_HIDDEN)
    return;
 
  v = c->glw_parent_misc[0];

  nv = v * gc->xs;

  if(nv < -1.5 || nv > 1.5)
    return;

  rc0 = *rc;
  glw_PushMatrix(&rc0, rc);

  rc0.rc_size_x *= gc->xs;
  rc0.rc_size_y *= gc->ys;

  glw_Scalef(&rc0, gc->xs, gc->ys, 1.0);

  r = GLW_MAX(GLW_MIN(v, 1.0), -1.0) * 60;

  if(v < -1) {
    v = v - 1;
  } else if(v > 1) {
    v = v + 1;
  } else {
    v *= 2;
  }

  glw_Translatef(&rc0, v, 0.0, 0.0);
  glw_Rotatef(&rc0, -r, 0, 1.0, 0.0);

  rc0.rc_alpha *= GLW_CLAMP(1 - fabs(nv), 0, 1);

  glw_signal0(c, GLW_SIGNAL_RENDER, &rc0);
  glw_PopMatrix();

}


/**
 *
 */
static void
render(glw_coverflow_t *gc, glw_rctx_t *rc)
{
  glw_t *c, *p, *n;
  struct glw_queue rqueue;

  if((c = gc->rstart) == NULL)
    return;
  TAILQ_INIT(&rqueue);
  TAILQ_INSERT_HEAD(&rqueue, c, glw_render_link);

  p = n = c;

  while(1) {
    p = p ? TAILQ_PREV(p, glw_queue, glw_parent_link) : NULL;
    n = n ? TAILQ_NEXT(n, glw_parent_link) : NULL;

    if(p == NULL && n == NULL)
      break;

    if(p != NULL)
      TAILQ_INSERT_HEAD(&rqueue, p, glw_render_link);
    if(n != NULL)
      TAILQ_INSERT_HEAD(&rqueue, n, glw_render_link);
  }

  TAILQ_FOREACH(c, &rqueue, glw_render_link)
    renderone(rc, c, gc);
}


/**
 *
 */
static int
glw_list_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_coverflow_t *gc = (glw_coverflow_t *)w;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    layout(gc, rc);
    return 0;

  case GLW_SIGNAL_RENDER:
    render(gc, rc);
    return 0;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    gc->scroll_to_me = extra;
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    break;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(gc->scroll_to_me == extra)
      gc->scroll_to_me = NULL;
    break;
  }
  return 0;
}


/**
 *
 */
void 
glw_coverflow_ctor(glw_t *w, int init, va_list ap)
{
  glw_attribute_t attrib;

  if(init)
    glw_signal_handler_int(w, glw_list_callback);

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}
