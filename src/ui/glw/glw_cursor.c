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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "glw.h"



typedef struct glw_cursor {
  glw_t h;
  glw_rctx_t gc_cursor_rctx;
  float alpha;
} glw_cursor_t;


/**
 *
 */
static void
glw_cursor_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_cursor_t *gc = (glw_cursor_t *)w;

  if(w->glw_alpha < 0.01)
    return;

  c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return;

  glw_layout0(c, rc);

  c = TAILQ_NEXT(c, glw_parent_link);
  if(c == NULL)
    return;

  glw_root_t *gr = w->glw_root;

  glw_t *f = gr->gr_current_focus;

  float cursor_alpha = 0;

  if(f == NULL)
    return;

  if(f->glw_flags & GLW_IN_FOCUS_PATH &&
     w->glw_flags & GLW_IN_FOCUS_PATH) {

    if(f->glw_matrix != NULL) {
      Mtx *x = f->glw_matrix;

      for(int i = 0; i < 16; i++) {
        glw_lp(&gc->gc_cursor_rctx.rc_mtx[i], gr, (*x)[i], 0.5);
        //        printf("%2.3f%c", gc->gc_matrix[i], (i+1) & 3 ? '\t' : '\n');
      }
    }

    if(f->glw_flags2 & GLW2_CURSOR)
      cursor_alpha = 1;
  }

  glw_rect_t cursor_rect;

  glw_project(&cursor_rect, &gc->gc_cursor_rctx, gr);

  gc->gc_cursor_rctx.rc_width  = cursor_rect.x2 - cursor_rect.x1;
  gc->gc_cursor_rctx.rc_height = cursor_rect.y2 - cursor_rect.y1;

  if(gc->gc_cursor_rctx.rc_width <= 0)
    return;

  if(gc->gc_cursor_rctx.rc_height <= 0)
    return;

  glw_lp(&gc->alpha, w->glw_root, cursor_alpha, 0.5);

  gc->gc_cursor_rctx.rc_alpha = rc->rc_alpha * w->glw_alpha * gc->alpha;
  gc->gc_cursor_rctx.rc_sharpness = 1.0f;


  glw_layout0(c, &gc->gc_cursor_rctx);
}


/**
 *
 */
static void
glw_cursor_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_cursor_t *gc = (glw_cursor_t *)w;
  glw_t *c, *d;
  glw_rctx_t rc0;
  int zmax = 0;

  if(w->glw_alpha < 0.01)
    return;

  c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return;

  rc0 = *rc;
  rc0.rc_zmax = &zmax;

  glw_render0(c, &rc0);
  glw_zinc(&rc0);

  d = TAILQ_NEXT(c, glw_parent_link);
  if(d != NULL) {
    rc0.rc_zindex = MAX(zmax, rc->rc_zindex);
    gc->gc_cursor_rctx.rc_zmax = rc0.rc_zmax;
    gc->gc_cursor_rctx.rc_zindex = rc0.rc_zindex;
    glw_render0(d, &gc->gc_cursor_rctx);
  }

  *rc->rc_zmax = MAX(*rc->rc_zmax, zmax);
}


/**
 *
 */
static int
glw_cursor_callback(glw_t *w, void *opaque, glw_signal_t signal,
                    void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    if(extra == TAILQ_FIRST(&w->glw_childs))
       glw_copy_constraints(w, extra);
    return 1;
  default:
    return 0;
  }
}



static glw_class_t glw_cursor = {
  .gc_name = "cursor",
  .gc_instance_size = sizeof(glw_cursor_t),
  .gc_render = glw_cursor_render,
  .gc_signal_handler = glw_cursor_callback,
  .gc_layout = glw_cursor_layout,
};

GLW_REGISTER_CLASS(glw_cursor);
 
