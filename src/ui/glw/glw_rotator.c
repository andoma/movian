/*
 *  GL Widgets, Fixed speed rotating widget
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

/**
 *
 */
static int
glw_rotator_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;
  glw_rctx_t *rc;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    rc = extra;
    w->glw_extra -= 5;
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL)
      glw_layout0(c, rc);
    break;
  }
  return 0;
}

/**
 *
 */
static void
glw_rotator_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  rc0 = *rc;

  glw_PushMatrix(&rc0, rc);
  glw_Scalef(&rc0, 0.8, 0.8, 0.8);
  glw_rescale(&rc0, 1.0f);

  glw_Rotatef(&rc0, w->glw_extra, 0.0, 0.0f, 1.0f);

  glw_render0(c, &rc0);
  glw_PopMatrix();
}


/**
 *
 */
static glw_class_t glw_rotator = {
  .gc_name = "rotator",
  .gc_instance_size = sizeof(glw_t),
  .gc_render = glw_rotator_render,
  .gc_signal_handler = glw_rotator_callback,
};

GLW_REGISTER_CLASS(glw_rotator);
