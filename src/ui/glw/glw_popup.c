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

  float screen_x;
  float screen_y;
  float aspect;

  int screen_cord_set;

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

  if(p->aspect > 0) {
    p->height = rc->rc_height / 2;
    p->width = p->height * p->aspect;
    p->width = MIN(p->height * p->aspect, rc->rc_width);
  } else {


    int f = glw_filter_constraints(c);
    if(f & GLW_CONSTRAINT_X)
      p->width = MIN(c->glw_req_size_x, rc->rc_width);
    else
      p->width = rc->rc_width / 2;

    if(f & GLW_CONSTRAINT_Y)
      p->height = MIN(c->glw_req_size_y, rc->rc_height);
    else
      p->height = rc->rc_height / 2;
  }

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

  Vec3 point, dir;

  glw_vec3_copy(point, glw_vec3_make(p->screen_x, p->screen_y, -2.41));
  glw_vec3_sub(dir, point, glw_vec3_make(p->screen_x * 42.38,
                                         p->screen_y * 42.38,
                                         -100));
  float x, y;

  glw_widget_unproject(w->glw_matrix, &x, &y, point, dir);

  int x1, y1;

  if(p->screen_cord_set) {
    x1 = (x + 1.0f) * 0.5f * rc->rc_width;
    y1 = (y + 1.0f) * 0.5f * rc->rc_height - p->height;
  } else {
    x1 = rc->rc_width / 2 - p->width / 2;
    y1 = rc->rc_height / 2 - p->height / 2;
  }

  int x2 = x1 + p->width;
  int y2 = y1 + p->height;


  if(x2 > rc->rc_width) {
    int spill = x2 - rc->rc_width;
    x1 -= spill;
    x2 -= spill;
  }

  if(y1 < 0) {
    y2 -= y1;
    y1 -= y1;
  }
  glw_reposition(&rc0, x1, y2, x2, y1);

  glw_render0(c, &rc0);
}



/**
 *
 */
static int
glw_popup_set_float(glw_t *w, glw_attribute_t attrib, float value,
                    glw_style_t *gs)
{
  glw_popup_t *p = (glw_popup_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_ASPECT:
    if(p->aspect == value)
      return 0;
    p->aspect = value;
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_popup_set_float_unresolved(glw_t *w, const char *a, float value,
                              glw_style_t *gs)
{
  glw_popup_t *p = (glw_popup_t *)w;

  if(!strcmp(a, "screenPositionX")) {
    p->screen_x = value;
    p->screen_cord_set = 1;
    return GLW_SET_RERENDER_REQUIRED;
  }

  if(!strcmp(a, "screenPositionY")) {
    p->screen_y = value;
    p->screen_cord_set = 1;
    return GLW_SET_RERENDER_REQUIRED;
  }

  return GLW_SET_NOT_RESPONDING;
}


static glw_class_t glw_popup = {
  .gc_name = "popup",
  .gc_instance_size = sizeof(glw_popup_t),
  .gc_layout = popup_layout,
  .gc_render = popup_render,
  .gc_set_float_unresolved = glw_popup_set_float_unresolved,
  .gc_set_float = glw_popup_set_float,
};

GLW_REGISTER_CLASS(glw_popup);


