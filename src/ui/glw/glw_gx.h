/*
 *  GL Widgets, GX specifics
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

#ifndef GLW_GX_H__
#define GLW_GX_H__

#include <gccore.h>

/**
 *
 */
typedef struct glw_backend_root {

} glw_backend_root_t;


/**
 *
 */
typedef struct glw_backend_rctx {
  Mtx gbr_model_matrix;

} glw_backend_rctx_t;


typedef struct {
  GXTexObj obj;
  void *mem;  // Use obj.userdata ?
  int initialized;
} glw_backend_texture_t;

/**
 * Renderer
 */
typedef struct glw_renderer {
  int gr_vertices;
  float *gr_buffer;
  int gr_stride;
} glw_renderer_t;

#define GLW_RENDER_MODE_QUADS     GX_QUADS
#define GLW_RENDER_MODE_LINESTRIP GX_LINESTRIP

#define glw_render_set_pre(gr)

#define glw_render_set_post(gr)

#define glw_can_tnpo2(gr) (1)

#define glw_is_tex_inited(n) ((n)->mem != NULL)


#endif /* GLW_GX_H__ */
