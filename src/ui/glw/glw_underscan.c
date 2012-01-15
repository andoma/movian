/*
 *  Underscanning widget
 *  Copyright (C) 2011 Andreas Öman
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
layout(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0 = *rc;

  if(w->glw_alpha < 0.01f)
    return 0;

  if(rc0.rc_overscanning) {
    rc0.rc_width  = rc->rc_width  - 2 * (w->glw_root->gr_underscan_h);
    rc0.rc_height = rc->rc_height - 2 * (w->glw_root->gr_underscan_v);
    rc0.rc_overscanning = 0;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, &rc0);
  }
  return 0;
}

/**
 *
 */
static void
render(glw_t *w, glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;

  glw_rctx_t rc0;

  if(alpha < 0.01f)
    return;
  rc0 = *rc;
  if(rc0.rc_overscanning) {
    int ho = w->glw_root->gr_underscan_h;
    int vo = w->glw_root->gr_underscan_v;
    glw_reposition(&rc0, ho, rc->rc_height - vo, rc->rc_width - ho, vo);
    rc0.rc_overscanning = 0;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_render0(c, &rc0);
  }
  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, &rc0);
}


static int
signal_handler(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    return layout(w, extra);
  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;
  default:
    break;
  }
  return 0;
}

static glw_class_t glw_underscan = {
  .gc_name = "underscan",
  .gc_instance_size = sizeof(glw_t),
  .gc_render = render,
  .gc_signal_handler = signal_handler,
};

GLW_REGISTER_CLASS(glw_underscan);
