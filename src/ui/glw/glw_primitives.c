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

typedef struct glw_quad {
  glw_t w;
  
  glw_rgb_t color;
  glw_renderer_t r;
} glw_quad_t;




static void
glw_quad_render(glw_t *w, glw_rctx_t *rc)
{
  glw_quad_t *q = (void *)w;

  if(!glw_renderer_initialized(&q->r)) {
    glw_renderer_init_quad(&q->r);
    glw_renderer_vtx_pos(&q->r, 0, -1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 1,  1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 2,  1,  1, 0);
    glw_renderer_vtx_pos(&q->r, 3, -1,  1, 0);
  }

  glw_renderer_draw(&q->r, w->glw_root, rc, NULL,
		    q->color.r, q->color.g, q->color.b, rc->rc_alpha);
}


/**
 *
 */
static int
glw_quad_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  return 0;
}

static void 
glw_quad_set(glw_t *w, int init, va_list ap)
{
  glw_quad_t *q = (void *)w;
  glw_attribute_t attrib;

  if(init) {
    q->color.r = 1.0;
    q->color.g = 1.0;
    q->color.b = 1.0;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_RGB:
      q->color.r = va_arg(ap, double);
      q->color.g = va_arg(ap, double);
      q->color.b = va_arg(ap, double);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}



static glw_class_t glw_quad = {
  .gc_name = "quad",
  .gc_instance_size = sizeof(glw_quad_t),
  .gc_render = glw_quad_render,
  .gc_signal_handler = glw_quad_callback,
  .gc_set = glw_quad_set,
};



GLW_REGISTER_CLASS(glw_quad);
