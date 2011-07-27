/*
 *  GLW mirror effect
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

/**
 *
 */
static int
glw_mirror_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    c = TAILQ_FIRST(&w->glw_childs);
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
glw_mirror_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;
  int b;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  b = glw_clip_enable(w->glw_root, rc, GLW_CLIP_BOTTOM, 0);
  glw_render0(c, rc);
  glw_clip_disable(w->glw_root, rc, b);

  rc0 = *rc;

  glw_Translatef(&rc0, 0, -1, 0);
  glw_Scalef(&rc0, 1.0, -1.0, 1.0);
  glw_Translatef(&rc0, 0, 1, 0);

  glw_frontface(w->glw_root, GLW_CW);

  rc0.rc_alpha *= w->glw_alpha;
  rc0.rc_inhibit_matrix_store = 1;

  b = glw_clip_enable(w->glw_root, &rc0, GLW_CLIP_BOTTOM, 0);
  glw_render0(c, &rc0);
  glw_clip_disable(w->glw_root, &rc0, b);

  glw_frontface(w->glw_root, GLW_CCW);
}


/**
 *
 */
static glw_class_t glw_mirror = {
  .gc_name = "mirror",
  .gc_instance_size = sizeof(glw_t),
  .gc_render = glw_mirror_render,
  .gc_signal_handler = glw_mirror_callback,
};

GLW_REGISTER_CLASS(glw_mirror);
