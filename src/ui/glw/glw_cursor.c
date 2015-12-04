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
  glw_t w;

  int gc_initialized;
  Mtx gc_mtx;
  glw_rctx_t gc_cursor_rctx;

  glw_rctx_t gc_hover_rctx;
  int gc_hover_set;

} glw_cursor_t;


/**
 *
 */
static void
glw_cursor_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c != NULL)
    glw_layout0(c, rc);
}


/**
 *
 */
static void
render_focus_widget(glw_t *w, glw_cursor_t *gc, const Mtx *saved,
                    glw_rctx_t *rc0, const glw_rctx_t *rc,
                    int *zmax)
{
  glw_root_t *gr = w->glw_root;

  if(!(gc->w.glw_flags & GLW_IN_FOCUS_PATH))
    return;

  glw_t *f = gr->gr_current_focus;

  if(f->glw_matrix != NULL) {

    Mtx a_inv;
    glw_mtx_invert(&a_inv, saved);
    Mtx *b = f->glw_matrix;

    if(0) {
      glw_rect_t focus_rect;
      glw_project_matrix(&focus_rect, b, gr);
      printf("Current focus: %d,%d - %d,%d\n",
             focus_rect.x1, focus_rect.y1,
             focus_rect.x2, focus_rect.y2);
    }

    Mtx x;
    glw_mtx_mul(&x, &a_inv, b);

    if (!gc->gc_initialized) {
      gc->gc_mtx = x;
      gc->gc_initialized = 1;
    } else {
      for(int r = 0; r < 4; r++) {
        for(int c = 0; c < 4; c++) {
          glw_lp(&gc->gc_mtx.r[r][c], gr, x.r[r][c], 0.75);
          /* printf("%2.3f%c", gc->gc_mtx[i], (i+1) & 3 ? '\t' : '\n'); */
        }
      }
    }
    glw_mtx_mul(&gc->gc_cursor_rctx.rc_mtx, saved, &gc->gc_mtx);
  }

  glw_rect_t cursor_rect;

  glw_project(&cursor_rect, &gc->gc_cursor_rctx, gr);

  gc->gc_cursor_rctx.rc_width  = cursor_rect.x2 - cursor_rect.x1;
  gc->gc_cursor_rctx.rc_height = cursor_rect.y2 - cursor_rect.y1;

  if(gc->gc_cursor_rctx.rc_width <= 0)
    return;

  if(gc->gc_cursor_rctx.rc_height <= 0)
    return;

  gc->gc_cursor_rctx.rc_alpha = 1.0f;
  gc->gc_cursor_rctx.rc_sharpness = 1.0f;

  glw_layout0(w, &gc->gc_cursor_rctx);

  rc0->rc_zindex = MAX(*zmax, rc->rc_zindex);
  gc->gc_cursor_rctx.rc_zmax = rc0->rc_zmax;
  gc->gc_cursor_rctx.rc_zindex = rc0->rc_zindex;
  glw_render0(w, &gc->gc_cursor_rctx);
}



/**
 *
 */
static void
render_hover_widget(glw_t *w, glw_cursor_t *gc,
                    glw_rctx_t *rc0, const glw_rctx_t *rc,
                    int *zmax)
{
  //  glw_root_t *gr = w->glw_root;

  if(!(gc->w.glw_flags & GLW_IN_HOVER_PATH))
    return;

  gc->gc_hover_rctx.rc_alpha = 1.0f;
  gc->gc_hover_rctx.rc_sharpness = 1.0f;

  glw_layout0(w, &gc->gc_hover_rctx);

  rc0->rc_zindex = MAX(*zmax, rc->rc_zindex);
  gc->gc_hover_rctx.rc_zmax = rc0->rc_zmax;
  gc->gc_hover_rctx.rc_zindex = rc0->rc_zindex;
  glw_render0(w, &gc->gc_hover_rctx);
}


/**
 *
 */
static void
glw_cursor_focus_tracker(glw_t *w, const glw_rctx_t *rc, glw_t *cursor)
{
  glw_cursor_t *gc = (glw_cursor_t *)cursor;

  if(!(w->glw_flags & GLW_IN_HOVER_PATH))
    return;

  gc->gc_hover_rctx = *rc;
  gc->gc_hover_set = 1;
}

/**
 *
 */
static void
glw_cursor_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_root_t *gr = w->glw_root;
  glw_cursor_t *gc = (glw_cursor_t *)w;
  glw_rctx_t rc0;
  int zmax = 0;
  glw_t *c;
  Mtx saved;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return;

  saved = rc->rc_mtx;
  
  rc0 = *rc;
  rc0.rc_zmax = &zmax;

  gc->gc_hover_set = 0;

  glw_t *saved_cursor = gr->gr_current_cursor;
  void (*saved_focus_tracker)(struct glw *w, const struct glw_rctx *rc,
                              struct glw *cursor) =
    gr->gr_cursor_focus_tracker;

  gr->gr_cursor_focus_tracker = glw_cursor_focus_tracker;
  gr->gr_current_cursor = w;

  glw_render0(c, &rc0);

  gr->gr_cursor_focus_tracker = saved_focus_tracker;
  gr->gr_current_cursor = saved_cursor;

  glw_zinc(&rc0);

  c = TAILQ_NEXT(c, glw_parent_link);
  if(c != NULL) {
    render_focus_widget(c, gc, &saved, &rc0, rc, &zmax);

    if(!gr->gr_keyboard_mode) {
      c = TAILQ_NEXT(c, glw_parent_link);
      if(c != NULL) {
        render_hover_widget(c, gc, &rc0, rc, &zmax);
      }
    }
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
 
