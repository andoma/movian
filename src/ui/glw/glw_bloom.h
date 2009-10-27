/*
 *  GL Widgets -- Bloom filter
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef GLW_BLOOM_H
#define GLW_BLOOM_H

#include "glw_texture.h"

#define BLOOM_COUNT 3

typedef struct glw_bloom {
  glw_t w;

  glw_gf_ctrl_t b_flushctrl;

  float b_glow;

  int b_width;
  int b_height;
  glw_rtt_t b_rtt[BLOOM_COUNT];

  int b_render_initialized;
  glw_renderer_t b_render;

  int b_need_render;

} glw_bloom_t;

void glw_bloom_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_BLOOM_H */
