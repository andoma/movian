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
typedef struct glw_resizer {
  glw_t w;

} glw_resizer_t;



/**
 *
 */
static int
get_width(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return rc->rc_width;
  int rw = glw_req_width(c);
  return MAX(rw, rc->rc_width);
}


/**
 *
 */
static int
get_height(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c == NULL)
    return rc->rc_height;
  int rh = glw_req_height(c);
  return MAX(rh, rc->rc_height);
}


/**
 *
 */
static void
glw_resizer_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;
  rc0 = *rc;

  rc0.rc_width  = get_width(w,  rc);
  rc0.rc_height = get_height(w, rc);
  glw_layout0(c, &rc0);
}


/**
 *
 */
static void
glw_resizer_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  int width  = get_width(w,  rc);
  int height = get_height(w, rc);

  int exp_x = MAX(width  - rc->rc_width, 0);
  int exp_y = MAX(height - rc->rc_height, 0);

  rc0 = *rc;
  glw_reposition(&rc0, 0, rc->rc_height, rc->rc_width+exp_x, -exp_y);
  glw_render0(c, &rc0);
}


/**
 *
 */
static int
glw_resizer_set_int_unresolved(glw_t *w, const char *a, int value,
                               glw_style_t *gs)
{
  if(!strcmp(a, "fixedWidth")) {
    //    r->fixedWidth = value;
    return GLW_SET_RERENDER_REQUIRED;
  }
  return GLW_SET_NOT_RESPONDING;
}


static glw_class_t glw_resizer = {
  .gc_name = "resizer",
  .gc_instance_size = sizeof(glw_resizer),
  .gc_layout = glw_resizer_layout,
  .gc_render = glw_resizer_render,
  .gc_set_int_unresolved = glw_resizer_set_int_unresolved,
};

GLW_REGISTER_CLASS(glw_resizer);
