/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include "glw.h"
#include "glw_navigation.h"

/**
 *
 */
typedef struct {
  glw_t w;

  glw_t *scroll_to_me;

  float pos;
  float pos_target;

  glw_t *rstart;

  float xs;

} glw_coverflow_t;


typedef struct glw_coverflow_item {
  float pos;
} glw_coverflow_item_t;


/**
 *
 */
static void
glw_coverflow_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_coverflow_t *gc = (glw_coverflow_t *)w;
  float n = 0, nv;
  glw_t *c, *rstart = NULL;
  glw_rctx_t rc0;
  gc->xs = (float)rc->rc_height / rc->rc_width;

  rc0 = *rc;
  rc0.rc_width = rc->rc_height;

  glw_lp(&gc->pos, gc->w.glw_root, gc->pos_target, 0.2);

  TAILQ_FOREACH(c, &gc->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    glw_coverflow_item_t *cd = glw_parent_data(c, glw_coverflow_item_t);

    cd->pos = n - gc->pos;

    if(rstart == NULL ||
       fabs(glw_parent_data(rstart, glw_coverflow_item_t)->pos) > fabs(cd->pos))
      rstart = c;

    nv = cd->pos * gc->xs;
    if(nv > -2 && nv < 2)
      glw_layout0(c, &rc0);

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
  float v, r;
  glw_rctx_t rc0;

  if(c->glw_flags & GLW_HIDDEN)
    return;
 
  glw_coverflow_item_t *cd = glw_parent_data(c, glw_coverflow_item_t);
  v = cd->pos;

  if(v < -1.5 || v > 1.5)
    return;


  //  nv = v * gc->xs;

  rc0 = *rc;

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
  rc0.rc_zindex = -fabs(v);

  //  rc0.rc_alpha *= GLW_CLAMP(1 - fabs(nv), 0, 1);

  glw_render0(c, &rc0);
}


/**
 *
 */
static void
reposition(glw_rctx_t *rc, int left, int top, int right, int bottom)
{
  float sx =         (right - left) / (float)rc->rc_width;
  float tx = -1.0f + (right + left) / (float)rc->rc_width;
  float sy =         (top - bottom) / (float)rc->rc_height;
  float ty = -1.0f + (top + bottom) / (float)rc->rc_height;
  
  glw_Translatef(rc, tx, ty, 0);
  glw_Scalef(rc, sx, sy, 1.0);

  rc->rc_width  = right - left;
  rc->rc_height = top - bottom;
}


/**
 *
 */
static void
glw_coverflow_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c, *p, *n;
  struct glw_queue rqueue;
  glw_coverflow_t *gc = (glw_coverflow_t *)w;
  glw_rctx_t rc0 = *rc;

  if((c = gc->rstart) == NULL)
    return;

  int left  = rc->rc_width / 2 - rc->rc_height / 2;
  int right = left + rc->rc_height;

  reposition(&rc0, left, rc->rc_height, right, 0);

  TAILQ_INIT(&rqueue);
  TAILQ_INSERT_HEAD(&rqueue, c, glw_render_link);

  p = n = c;

  while(1) {
    p = p ? glw_prev_widget(p) : NULL;
    n = n ? glw_next_widget(n) : NULL;

    if(p == NULL && n == NULL)
      break;

    if(p != NULL)
      TAILQ_INSERT_HEAD(&rqueue, p, glw_render_link);
    if(n != NULL)
      TAILQ_INSERT_HEAD(&rqueue, n, glw_render_link);
  }

  TAILQ_FOREACH(c, &rqueue, glw_render_link)
    renderone(&rc0, c, gc);
}


/**
 *
 */
static void
glw_coverflow_ctor(glw_t *w)
{
  w->glw_flags |= GLW_FLOATING_FOCUS;
}


/**
 *
 */
static int
glw_coverflow_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_coverflow_t *gc = (glw_coverflow_t *)w;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE:
    gc->scroll_to_me = extra;
    w->glw_flags &= ~GLW_FLOATING_FOCUS;
    return 0;

  case GLW_SIGNAL_CHILD_DESTROYED:
    if(gc->scroll_to_me == extra)
      gc->scroll_to_me = NULL;
    if(gc->rstart == extra)
      gc->rstart = NULL;
    break;
  }
  return 0;
}


/**
 *
 */
static glw_class_t glw_coverflow = {
  .gc_name = "coverflow",
  .gc_instance_size = sizeof(glw_coverflow_t),
  .gc_parent_data_size = sizeof(glw_coverflow_item_t),
  .gc_ctor = glw_coverflow_ctor,
  .gc_flags = GLW_NAVIGATION_SEARCH_BOUNDARY | GLW_CAN_HIDE_CHILDS,
  .gc_layout = glw_coverflow_layout,
  .gc_render = glw_coverflow_render,
  .gc_signal_handler = glw_coverflow_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_bubble_event = glw_navigate_horizontal,
};

GLW_REGISTER_CLASS(glw_coverflow);
