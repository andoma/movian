/*
 *  GL Widgets, GLW_CONTAINER -widgets
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
glw_clip_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_alpha < 0.01)
    return 0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
  return 0;
}




static void
glw_clip_render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;

  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, rc);

  int l = glw_clip_enable(w->glw_root, rc, GLW_CLIP_LEFT);
  int r = glw_clip_enable(w->glw_root, rc, GLW_CLIP_RIGHT);
  int t = glw_clip_enable(w->glw_root, rc, GLW_CLIP_TOP);
  int b = glw_clip_enable(w->glw_root, rc, GLW_CLIP_BOTTOM);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    c->glw_class->gc_render(c, rc);

  glw_clip_disable(w->glw_root, rc, l);
  glw_clip_disable(w->glw_root, rc, r);
  glw_clip_disable(w->glw_root, rc, t);
  glw_clip_disable(w->glw_root, rc, b);

}

static int
glw_clip_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return glw_clip_layout(w, extra);
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_copy_constraints(w, extra);
    return 1;
  default:
    return 0;
  }
}



static glw_class_t glw_clip = {
  .gc_name = "clip",
  .gc_instance_size = sizeof(glw_t),
  .gc_render = glw_clip_render,
  .gc_signal_handler = glw_clip_callback,
};



GLW_REGISTER_CLASS(glw_clip);
