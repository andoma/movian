/*
 *  GL Widgets, GLW_BAR widget
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
#include "glw_renderer.h"

typedef struct glw_bar {
  glw_t w;

  float gb_col1[3];
  float gb_col2[3];

  glw_renderer_t gb_gr;

  float gb_fill;

  int gb_update;

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
  if(a > 0.01) {
    glw_renderer_draw(&gb->gb_gr, w->glw_root, rc,
		      NULL, NULL, NULL, a, 0, NULL);
  }
}


/**
 *
 */
static void 
glw_bar_layout(glw_t *W, glw_rctx_t *rc)
{
  glw_bar_t *gb = (void *)W;
  float r, g, b, x;

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

  }
}


/**
 *
 */
static int
glw_bar_callback(glw_t *w, void *opaque, glw_signal_t signal,
		      void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_bar_layout(w, extra);
    break;
  }
  return 0;
}

/**
 *
 */
static void 
glw_bar_set(glw_t *w, va_list ap)
{
  glw_bar_t *gb = (glw_bar_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_FILL:
      gb->gb_fill = va_arg(ap, double);
      if(gb->gb_fill > 1)
	gb->gb_fill = 1;
      gb->gb_update = 1;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}


/**
 *
 */
static void
set_color1(glw_t *w, const float *rgb)
{
  glw_bar_t *gb = (glw_bar_t *)w;
  gb->gb_col1[0] = GLW_CLAMP(rgb[0], 0, 1);
  gb->gb_col1[1] = GLW_CLAMP(rgb[1], 0, 1);
  gb->gb_col1[2] = GLW_CLAMP(rgb[2], 0, 1);
  gb->gb_update = 1;
}


/**
 *
 */
static void
set_color2(glw_t *w, const float *rgb)
{
  glw_bar_t *gb = (glw_bar_t *)w;
  gb->gb_col2[0] = GLW_CLAMP(rgb[0], 0, 1);
  gb->gb_col2[1] = GLW_CLAMP(rgb[1], 0, 1);
  gb->gb_col2[2] = GLW_CLAMP(rgb[2], 0, 1);
  gb->gb_update = 1;
}



/**
 *
 */
static glw_class_t glw_bar = {
  .gc_name = "bar",
  .gc_instance_size = sizeof(glw_bar_t),
  .gc_render = glw_bar_render,
  .gc_set = glw_bar_set,
  .gc_dtor = glw_bar_dtor,
  .gc_signal_handler = glw_bar_callback,
  .gc_set_color1 = set_color1,
  .gc_set_color2 = set_color2,
};

GLW_REGISTER_CLASS(glw_bar);

