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

/**
 * Image flags
 */
#define GLW_IMAGE_MIRROR_X      0x1
#define GLW_IMAGE_MIRROR_Y      0x2
#define GLW_IMAGE_BORDER_LEFT   0x4
#define GLW_IMAGE_BORDER_RIGHT  0x8
#define GLW_IMAGE_BORDER_TOP    0x10
#define GLW_IMAGE_BORDER_BOTTOM 0x20
#define GLW_IMAGE_NOFILL_X      0x40
#define GLW_IMAGE_NOFILL_Y      0x80
#define GLW_IMAGE_INFRONT       0x100

#include "glw_texture.h"

typedef struct glw_image {
  glw_t w;

  float gi_alpha_self;

  float gi_angle;

  glw_loadable_texture_t *gi_current;
  glw_loadable_texture_t *gi_pending;

  int16_t gi_border_left;
  int16_t gi_border_right;
  int16_t gi_border_top;
  int16_t gi_border_bottom;

  int16_t gi_padding_left;
  int16_t gi_padding_right;
  int16_t gi_padding_top;
  int16_t gi_padding_bottom;
 
  int gi_bitmap_flags;

  uint8_t gi_border_scaling;
  uint8_t gi_render_initialized;
  uint8_t gi_update;
  uint8_t gi_explicit_padding;

  uint8_t gi_frozen;

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
