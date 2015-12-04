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

/**
 *
 */
static void
layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0 = *rc;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

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
}


/**
 *
 */
static void
render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;

  glw_rctx_t rc0;

  if(alpha < GLW_ALPHA_EPSILON)
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
  if(w->glw_flags2 & GLW2_DEBUG)
    glw_wirebox(w->glw_root, &rc0);
}


static glw_class_t glw_underscan = {
  .gc_name = "underscan",
  .gc_instance_size = sizeof(glw_t),
  .gc_layout = layout,
  .gc_render = render,
};

GLW_REGISTER_CLASS(glw_underscan);
