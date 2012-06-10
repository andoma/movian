/*
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

typedef struct glw_detachable {
  glw_t w;
  int on;
  int16_t width, height;

} glw_detachable_t;

/**
 *
 */
static int
glw_detachable_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;
  glw_rctx_t *rc;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = extra;
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL)
      glw_layout0(c, rc);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;

  }
  return 0;
}

/**
 *
 */
static void
glw_detachable_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_detachable_t *gd = (glw_detachable_t *)w;
  glw_t *c;

  glw_store_matrix(w, rc);
  gd->width = rc->rc_width;
  gd->height = rc->rc_height;

  if(gd->on)
    return;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  glw_render0(c, rc);
}


/**
 *
 */
static void
glw_detach_control(glw_t *w, int on)
{
  glw_detachable_t *gd = (glw_detachable_t *)w;
  gd->on = on;
}


/**
 *
 */
static void
glw_detach_get_rctx(glw_t *w, struct glw_rctx *rc)
{
  glw_detachable_t *gd = (glw_detachable_t *)w;

  memcpy(rc->rc_mtx, w->glw_matrix, sizeof(Mtx));
  rc->rc_width = gd->width;
  rc->rc_height = gd->height;
}


/**
 *
 */
static glw_class_t glw_detachable = {
  .gc_name = "detachable",
  .gc_instance_size = sizeof(glw_detachable_t),
  .gc_render = glw_detachable_render,
  .gc_signal_handler = glw_detachable_callback,
  .gc_detach_control = glw_detach_control,
  .gc_get_rctx = glw_detach_get_rctx,
};

GLW_REGISTER_CLASS(glw_detachable);
