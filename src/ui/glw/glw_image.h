/*
 *  GL Widgets, GLW_IMAGE widget and texture stuff
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

#ifndef GLW_IMAGE_H
#define GLW_IMAGE_H

#include "glw_texture.h"

typedef struct glw_image {
  glw_t w;

  float gi_alpha_self;

  float gi_angle;
  float gi_angle0;

  glw_loadable_texture_t *gi_tex;

  int gi_border_scaling;

  float gi_border_left;
  float gi_border_right;
  float gi_border_top;
  float gi_border_bottom;
 
  int gi_mirror;

  int gi_render_initialized;
  int gi_render_init;

  glw_renderer_t gi_gr;

  float gi_saved_size_x;
  float gi_saved_size_y;

  float gi_child_xs;
  float gi_child_ys;
  float gi_child_xt;
  float gi_child_yt;

  glw_rgb_t gi_color;

  float gi_size_scale;
  float gi_size_bias;

} glw_image_t;

void glw_image_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_IMAGE_H */
