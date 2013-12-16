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

#ifndef GLW_GX_H__
#define GLW_GX_H__

#include <gccore.h>

struct glw_root;
struct glw_rctx;

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


/**
 * Render to texture support
 */
typedef struct {
  glw_backend_texture_t grtt_texture;
  
  int grtt_width;
  int grtt_height;

  int grtt_size;

} glw_rtt_t;

void glw_rtt_init(struct glw_root *gr, glw_rtt_t *grtt, int width, int height,
		  int alpha);

void glw_rtt_enter(struct glw_root *gr, glw_rtt_t *grtt, struct glw_rctx *rc);

void glw_rtt_restore(struct glw_root *gr, glw_rtt_t *grtt);

void glw_rtt_destroy(struct glw_root *gr, glw_rtt_t *grtt);

#define glw_rtt_texture(grtt) ((grtt)->grtt_texture)

#define GLW_BLEND_NORMAL   0
#define GLW_BLEND_ADDITIVE 1

void glw_blendmode(int mode);

void *gx_convert_argb(const uint8_t *src, int linesize, 
		      unsigned int w, unsigned int h);

#define GLW_CW   GX_CULL_FRONT
#define GLW_CCW  GX_CULL_BACK

#define glw_frontface(x) GX_SetCullMode(x)



#endif /* GLW_GX_H__ */
