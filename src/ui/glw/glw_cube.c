/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

typedef struct glw_cube {
  glw_t w;
  float theta;
} glw_cube_t;


/*
 *
 */
static int
glw_cube_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_cube_t *gc = (glw_cube_t *)w;
  glw_t *c;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    gc->theta -= 1;
    c = TAILQ_FIRST(&gc->w.glw_childs);
    if(c != NULL)
      glw_layout0(c, extra);
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_cube_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_cube_t *gc = (glw_cube_t *)w;
  glw_rctx_t rc0, rc1;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  rc0 = *rc;

  glw_scale_to_aspect(&rc0, 1.0f);

  glw_Translatef(&rc0, 0, 0, -2.0);

  glw_Rotatef(&rc0, gc->theta, 1.1, 0.5f, 1.0f);

  rc1 = rc0;

  glw_Rotatef(&rc1, 0, 0, 1, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);

  rc1 = rc0;
  glw_Rotatef(&rc1, 90, 0, 1, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);

  rc1 = rc0;
  glw_Rotatef(&rc1, 180, 0, 1, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);

  rc1 = rc0;
  glw_Rotatef(&rc1, 270, 0, 1, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);

  rc1 = rc0;
  glw_Rotatef(&rc1, 90, 1, 0, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);

  rc1 = rc0;
  glw_Rotatef(&rc1, 270, 1, 0, 0);
  glw_Translatef(&rc1, 0, 0, 1.0);
  glw_render0(c, &rc1);
}



/**
 *
 */
static glw_class_t glw_cube = {
  .gc_name = "cube",
  .gc_instance_size = sizeof(glw_cube_t),
  .gc_render = glw_cube_render,
  .gc_signal_handler = glw_cube_callback,
};

GLW_REGISTER_CLASS(glw_cube);
