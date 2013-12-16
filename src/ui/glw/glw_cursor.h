/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#ifndef GLW_CURSOR_H
#define GLW_CURSOR_H

#include "glw_texture.h"

/**
 *
 */
typedef struct glw_cursor_painter {

  float gcp_m[16];
  float gcp_m_prim[16];

  float gcp_alpha;
  float gcp_alpha_prim;

  glw_renderer_t gcp_renderer;
  int            gcp_renderer_inited;

} glw_cursor_painter_t;


/**
 *
 */
typedef struct glw_cursor {
  struct glw w;

  int render_cycle;

  glw_cursor_painter_t gcp;

  glw_loadable_texture_t *tex;

} glw_cursor_t;


void glw_cursor_layout_frame(glw_root_t *gr);

void glw_cursor_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_CURSOR_H */
