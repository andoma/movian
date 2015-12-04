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


typedef struct glw_popup {
  glw_t w;

  int16_t width;
  int16_t height;

} glw_popup_t;

/**
 *
 */
static void
popup_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  glw_popup_t *p = (glw_popup_t *)w;

  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL || c->glw_flags & GLW_HIDDEN)
    return;

  int f = glw_filter_constraints(c);
  if(f & GLW_CONSTRAINT_X)
    p->width = MIN(c->glw_req_size_x, rc->rc_width);
  else
    p->width = rc->rc_width / 2;

  if(f & GLW_CONSTRAINT_Y)
    p->height = MIN(c->glw_req_size_y, rc->rc_height);
  else
    p->height = rc->rc_height / 2;

  rc0 = *rc;
  rc0.rc_width  = p->width;
  rc0.rc_height = p->height;

  glw_layout0(c, &rc0);
}


/**
 *
 */
static void
popup_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  glw_popup_t *p = (glw_popup_t *)w;

  glw_store_matrix(w, rc);

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;

  if(rc0.rc_alpha < GLW_ALPHA_EPSILON)
    return;

  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL || c->glw_flags & GLW_HIDDEN)
    return;

  int x1 = rc->rc_width / 2 - p->width / 2;
  int x2 = rc->rc_width / 2 + p->width / 2;

  int y1 = rc->rc_height / 2 - p->height / 2;
  int y2 = rc->rc_height / 2 + p->height / 2;

  glw_reposition(&rc0, x1, y2, x2, y1);

  glw_render0(c, &rc0);
}


static glw_class_t glw_popup = {
  .gc_name = "popup",
  .gc_instance_size = sizeof(glw_popup_t),
  .gc_layout = popup_layout,
  .gc_render = popup_render,
};

GLW_REGISTER_CLASS(glw_popup);


