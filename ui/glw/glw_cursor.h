/*
 *  GL Widgets, Cursors
 *  Copyright (C) 2008 Andreas Öman
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

#ifndef GLW_CURSOR_H
#define GLW_CURSOR_H



/**
 *
 */
typedef struct glw_cursor_painter {

  float gcp_m[16];
  float gcp_m_prim[16];

  float gcp_alpha;
  float gcp_alpha_prim;

  float gcp_aspect;
  float gcp_aspect_prim;

} glw_cursor_painter_t;


/**
 *
 */
typedef struct glw_cursor {
  struct glw w;

  int render_cycle;

  glw_cursor_painter_t gcp;

} glw_cursor_t;


void glw_cursor_layout_frame(glw_root_t *gr);

void glw_cursor_ctor(glw_t *w, int init, va_list ap);

int glw_navigate(glw_t *w, event_t *e);

#endif /* GLW_CURSOR_H */
