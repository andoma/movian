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

typedef struct glw_bar {
  glw_t w;

  float gb_col1[3];
  float gb_col2[3];

  int gb_gr_initialized;
  glw_renderer_t gb_gr;

  float gb_fill;

  int gb_update;

} glw_bar_t;



static void
glw_bar_dtor(glw_t *w)
{
  glw_bar_t *gb = (void *)w;

  if(gb->gb_gr_initialized)
    glw_render_free(&gb->gb_gr);
}


/**
 *
 */
static void 
glw_bar_render(glw_t *w, glw_rctx_t *rc)
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
    glw_render(&gb->gb_gr, w->glw_root, rc, 
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_COLOR,
	       NULL, 1, 1, 1, a);
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

  if(!gb->gb_gr_initialized) {
    glw_render_init(&gb->gb_gr, 4, GLW_RENDER_ATTRIBS_TEX_COLOR);
    gb->gb_gr_initialized = 1;
    gb->gb_update = 1;
  }

  if(gb->gb_update) {
    gb->gb_update = 0;

    r = GLW_LERP(gb->gb_fill, gb->gb_col1[0], gb->gb_col2[0]);
    g = GLW_LERP(gb->gb_fill, gb->gb_col1[1], gb->gb_col2[1]);
    b = GLW_LERP(gb->gb_fill, gb->gb_col1[2], gb->gb_col2[2]);
    x = GLW_LERP(gb->gb_fill, -1, 1);

    glw_render_vtx_pos(&gb->gb_gr, 0, -1.0, -1.0, 0.0);
    glw_render_vtx_col(&gb->gb_gr, 0,
		       gb->gb_col1[0],
		       gb->gb_col1[1],
		       gb->gb_col1[2],
		       1.0);

    glw_render_vtx_pos(&gb->gb_gr, 1,  x, -1.0, 0.0);
    glw_render_vtx_col(&gb->gb_gr, 1, r, g, b, 1.0);


    glw_render_vtx_pos(&gb->gb_gr, 2,  x,  1.0, 0.0);
    glw_render_vtx_col(&gb->gb_gr, 2, r, g, b, 1.0);


    glw_render_vtx_pos(&gb->gb_gr, 3, -1.0,  1.0, 0.0);
    glw_render_vtx_col(&gb->gb_gr, 3,
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
glw_bar_set(glw_t *w, int init, va_list ap)
{
  glw_bar_t *gb = (glw_bar_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_COLOR1:
      gb->gb_col1[0] = va_arg(ap, double);
      gb->gb_col1[1] = va_arg(ap, double);
      gb->gb_col1[2] = va_arg(ap, double);
      gb->gb_col1[0] = GLW_CLAMP(gb->gb_col1[0], 0, 1);
      gb->gb_col1[1] = GLW_CLAMP(gb->gb_col1[1], 0, 1);
      gb->gb_col1[2] = GLW_CLAMP(gb->gb_col1[2], 0, 1);
      gb->gb_update = 1;
      break;

    case GLW_ATTRIB_COLOR2:
      gb->gb_col2[0] = va_arg(ap, double);
      gb->gb_col2[1] = va_arg(ap, double);
      gb->gb_col2[2] = va_arg(ap, double);
      gb->gb_col2[0] = GLW_CLAMP(gb->gb_col2[0], 0, 1);
      gb->gb_col2[1] = GLW_CLAMP(gb->gb_col2[1], 0, 1);
      gb->gb_col2[2] = GLW_CLAMP(gb->gb_col2[2], 0, 1);
      gb->gb_update = 1;
      break;
      
    case GLW_ATTRIB_FILL:
      gb->gb_fill = va_arg(ap, double);
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
static glw_class_t glw_bar = {
  .gc_name = "bar",
  .gc_instance_size = sizeof(glw_bar_t),
  .gc_render = glw_bar_render,
  .gc_set = glw_bar_set,
  .gc_dtor = glw_bar_dtor,
  .gc_signal_handler = glw_bar_callback,
};

GLW_REGISTER_CLASS(glw_bar);

