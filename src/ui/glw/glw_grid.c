/*
 *  GL Widgets, Grid helper widgets
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

TAILQ_HEAD(glw_column_queue, glw_column);

typedef struct glw_column {
  TAILQ_ENTRY(glw_column) gc_link;

  int16_t gc_width;
  int16_t gc_req_width;

} glw_column_t;


/**
 *
 */
typedef struct glw_grid {
  glw_t w;

  struct glw_column_queue gg_columns;

} glw_grid_t;


/**
 *
 */
static void
grid_ctor(glw_t *w)
{
  glw_grid_t *gg = (glw_grid_t *)w;
  TAILQ_INIT(&gg->gg_columns);
}


/**
 *
 */
static void
grid_render(glw_t *w, glw_rctx_t *rc)
{
  glw_grid_t *gg = (glw_grid_t *)w;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    glw_rctx_t rc0 = *rc;
    rc0.rc_grid = gg;
    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static int
grid_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_grid_t *gg = (glw_grid_t *)w;
  glw_rctx_t rc0;
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;

  case GLW_SIGNAL_LAYOUT:
    rc0 = *(glw_rctx_t *)extra;
    rc0.rc_grid = gg;
    if((c = TAILQ_FIRST(&w->glw_childs)) != NULL)
      glw_layout0(c, &rc0);
    break;

  default:
    break;
  }
  return 0;
}


static glw_class_t glw_grid = {
  .gc_name = "grid",
  .gc_instance_size = sizeof(glw_grid_t),
  .gc_render = grid_render,
  .gc_ctor = grid_ctor,
  .gc_signal_handler = grid_callback,
};
GLW_REGISTER_CLASS(glw_grid);


/**
 *
 */
typedef struct glw_row {
  glw_t w;

  

} glw_row_t;


/**
 *
 */
static void
row_render(glw_t *w, glw_rctx_t *rc)
{
  if(rc->rc_grid == NULL)
    return;
  
  //  glw_grid_t *gg = (glw_grid_t *)w;
}


/**
 *
 */
static void
row_layout(glw_row_t *gr, glw_rctx_t *rc)
{
  glw_t *c;

  if(rc->rc_grid == NULL)
    return;

  TAILQ_FOREACH(c, &gr->w.glw_childs, glw_parent_link) {
    

  }
  
}


/**
 *
 */
static int
row_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_row_t *gr = (glw_row_t *)w;

  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:

    return 1;

  case GLW_SIGNAL_LAYOUT:
    row_layout(gr, extra);
    break;

  default:
    break;
  }
  return 0;
}



static glw_class_t glw_row = {
  .gc_name = "row",
  .gc_instance_size = sizeof(glw_row_t),
  .gc_render = row_render,
  .gc_signal_handler = row_callback,
};
GLW_REGISTER_CLASS(glw_row);
