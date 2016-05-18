/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#pragma once
#include <rsx/gcm.h>
#include <rsx/commands.h>

struct glw_rgb;
struct glw_rctx;
struct glw_root;
struct glw_backend_root;
struct glw_renderer;
struct glw_backend_texture;


#define GLW_DRAW_TRIANGLES REALITY_TRIANGLES
#define GLW_DRAW_LINE_LOOP REALITY_LINE_LOOP
#define GLW_DRAW_LINES     REALITY_LINES


/**
 *
 */
typedef struct rsx_vp {
  realityVertexProgram *rvp_binary;

  int rvp_u_modelview;
  int rvp_u_color;
  int rvp_u_color_offset;
  int rvp_u_blur;

  int rvp_a_position;
  int rvp_a_color;
  int rvp_a_texcoord;

} rsx_vp_t;


/**
 *
 */
typedef struct rsx_fp {
  realityFragmentProgram *rfp_binary;

  int rfp_rsx_location;  // location in RSX memory

  int rfp_u_color;
  int rfp_u_color_matrix;

  int rfp_u_blend;

  int rfp_texunit[6];

} rsx_fp_t;


/**
 *
 */
struct glw_program {

  rsx_vp_t *gp_vertex_program;
  rsx_fp_t *gp_fragment_program;

};

/**
 *
 */
typedef struct glw_backend_root {
  gcmContextData *be_ctx;

  struct rsx_vp *be_vp_current;
  struct rsx_fp *be_fp_current;

  struct rsx_vp *be_vp_1;
  struct rsx_fp *be_fp_tex;
  struct rsx_fp *be_fp_flat;
  struct rsx_fp *be_fp_tex_blur;

  struct glw_program be_yuv2rgb_1f;
  struct glw_program be_yuv2rgb_2f;

  struct rsx_fp *be_fp_tex_stencil;
  struct rsx_fp *be_fp_flat_stencil;
  struct rsx_fp *be_fp_tex_stencil_blur;

} glw_backend_root_t;


/**
 *
 */
typedef struct glw_backend_texture {
  realityTexture tex;
  uint32_t size;
} glw_backend_texture_t;

#define glw_tex_width(gbt) ((gbt)->tex.width)
#define glw_tex_height(gbt) ((gbt)->tex.height)


#define glw_can_tnpo2(gr) 1

#define glw_is_tex_inited(n) ((n)->size != 0)

int glw_rsx_init_context(struct glw_root *gr);


/**
 * Render to texture support
 */
typedef struct {

  glw_backend_texture_t grtt_texture;

  int grtt_width;
  int grtt_height;

} glw_rtt_t;

void glw_rtt_init(struct glw_root *gr, glw_rtt_t *grtt, int width, int height,
		  int alpha);

void glw_rtt_enter(struct glw_root *gr, glw_rtt_t *grtt, struct glw_rctx *rc0);

void glw_rtt_restore(struct glw_root *gr, glw_rtt_t *grtt);

void glw_rtt_destroy(struct glw_root *gr, glw_rtt_t *grtt);

#define glw_rtt_texture(grtt) ((grtt)->grtt_texture)

/**
 *
 */
int rsx_alloc(int size, int alignment);

void rsx_free(int pos, int size);

extern char *rsx_address;

#define rsx_to_ppu(pos) ((void *)(rsx_address + (pos)))
