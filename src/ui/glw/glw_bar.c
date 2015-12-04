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
#include "glw_renderer.h"

typedef struct glw_bar {
  glw_t w;

  float gb_col1[3];
  float gb_col2[3];

  glw_renderer_t gb_gr;

  float gb_fill;

  char gb_update;
  char gb_is_active;

} glw_bar_t;



static void
glw_bar_dtor(glw_t *w)
{
  glw_bar_t *gb = (void *)w;

  glw_renderer_free(&gb->gb_gr);
}


/**
 *
 */
static void 
glw_bar_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_bar_t *gb = (void *)w;
  float a = rc->rc_alpha * w->glw_alpha;

  if(gb->gb_col1[0] < 0.001 &&
     gb->gb_col1[1] < 0.001 &&
     gb->gb_col1[2] < 0.001 &&
     gb->gb_col2[0] < 0.001 &&
     gb->gb_col2[1] < 0.001 &&
     gb->gb_col2[2] < 0.001) {
    return;
  }
  if(a > GLW_ALPHA_EPSILON) {
    glw_renderer_draw(&gb->gb_gr, w->glw_root, rc,
		      NULL, NULL, NULL, NULL, a, 0, NULL);
  }
}


/**
 *
 */
static void
glw_bar_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_bar_t *gb = (void *)w;
  float r, g, b, x;

  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  if(!glw_renderer_initialized(&gb->gb_gr)) {
    glw_renderer_init_quad(&gb->gb_gr);
    gb->gb_update = 1;
  }

  if(gb->gb_update) {
    gb->gb_update = 0;

    r = GLW_LERP(gb->gb_fill, gb->gb_col1[0], gb->gb_col2[0]);
    g = GLW_LERP(gb->gb_fill, gb->gb_col1[1], gb->gb_col2[1]);
    b = GLW_LERP(gb->gb_fill, gb->gb_col1[2], gb->gb_col2[2]);
    x = GLW_LERP(gb->gb_fill, -1, 1);

    glw_renderer_vtx_pos(&gb->gb_gr, 0, -1.0, -1.0, 0.0);
    glw_renderer_vtx_col(&gb->gb_gr, 0,
			 gb->gb_col1[0],
			 gb->gb_col1[1],
			 gb->gb_col1[2],
			 1.0);

    glw_renderer_vtx_pos(&gb->gb_gr, 1,  x, -1.0, 0.0);
    glw_renderer_vtx_col(&gb->gb_gr, 1, r, g, b, 1.0);


    glw_renderer_vtx_pos(&gb->gb_gr, 2,  x,  1.0, 0.0);
    glw_renderer_vtx_col(&gb->gb_gr, 2, r, g, b, 1.0);


    glw_renderer_vtx_pos(&gb->gb_gr, 3, -1.0,  1.0, 0.0);
    glw_renderer_vtx_col(&gb->gb_gr, 3,
			 gb->gb_col1[0],
			 gb->gb_col1[1],
			 gb->gb_col1[2],
			 1.0);

    glw_need_refresh(w->glw_root, 0);
  }
}


/**
 *
 */
static int
glw_bar_set_float(glw_t *w, glw_attribute_t attrib, float value,
                  glw_style_t *gs)
{
  glw_bar_t *gb = (glw_bar_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_FILL:
    value = MIN(value, 1.0f);
    if(gb->gb_fill == value)
      return 0;

    gb->gb_fill = value;
    gb->gb_update = 1;
    return w->glw_flags & GLW_ACTIVE ? GLW_REFRESH_LAYOUT_ONLY : 0;

  default:
    return -1;
  }
  return 1;
}

/**
 *
 */
static int
glw_bar_set_float3(glw_t *w, glw_attribute_t attrib, const float *vector,
                   glw_style_t *gs)
{
  glw_bar_t *gb = (glw_bar_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_COLOR1:
    if(!glw_attrib_set_float3_clamped(gb->gb_col1, vector))
      return 0;
    break;

  case GLW_ATTRIB_COLOR2:
    if(!glw_attrib_set_float3_clamped(gb->gb_col2, vector))
      return 0;
    break;
  default:
    return -1;
  }
  gb->gb_update = 1;
  return 1;
}


/**
 *
 */
static glw_class_t glw_bar = {
  .gc_name = "bar",
  .gc_instance_size = sizeof(glw_bar_t),
  .gc_render = glw_bar_render,
  .gc_set_float = glw_bar_set_float,
  .gc_dtor = glw_bar_dtor,
  .gc_set_float3 = glw_bar_set_float3,
  .gc_layout = glw_bar_layout,
};

GLW_REGISTER_CLASS(glw_bar);

