/*
 *  GL Widgets, GLW_BITMAP widget and texture stuff
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

#ifndef GLW_BITMAP_H
#define GLW_BITMAP_H

#include "glw_texture.h"

typedef struct glw_bitmap {
  glw_t gb_head;

  float gb_alpha_self;

  float gb_angle;
  float gb_angle0;

  glw_texture_t *gb_tex;

  int gb_border_scaling;

  float gb_tex_left;
  float gb_tex_right;
  float gb_tex_top;
  float gb_tex_bottom;
 
  int gb_mirror;

  int gb_render_initialized;
  int gb_render_init;

  glw_renderer_t gb_gr;

  float gb_saved_scale_x;
  float gb_saved_scale_y;

  float gb_child_xs;
  float gb_child_ys;

} glw_bitmap_t;

void glw_bitmap_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_BITMAP_H */
