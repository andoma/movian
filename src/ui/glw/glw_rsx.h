/*
 *  GL Widgets, common stuff
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

#pragma once

#include <rsx/gcm.h>
#include <rsx/commands.h>

struct glw_rgb;
struct glw_rctx;
struct glw_root;
struct glw_backend_root;
struct glw_renderer;
struct glw_backend_texture;

typedef float Mtx[16];

typedef struct glw_backend_root {
  gcmContextData *be_ctx;

  struct rsx_vp *be_vp_current;
  struct rsx_fp *be_fp_current;

  
  struct rsx_vp *be_vp_1;
  struct rsx_fp *be_fp_tex;
  struct rsx_fp *be_fp_flat;

  struct extent_pool *be_mempool;
  char *be_rsx_address;
  hts_mutex_t be_mempool_lock;



} glw_backend_root_t;

void glw_renderer_draw(struct glw_renderer *gr, struct glw_root *root,
		       struct glw_rctx *rc,
		       struct glw_backend_texture *be_tex,
		       const struct glw_rgb *rgb, float alpha,
		       int flags);



/**
 *
 */
typedef struct glw_backend_texture {
  realityTexture tex;
  uint32_t size;
  char type;
#define GLW_TEXTURE_TYPE_NORMAL   0
#define GLW_TEXTURE_TYPE_NO_ALPHA 1
} glw_backend_texture_t;



#define glw_can_tnpo2(gr) 1

#define glw_is_tex_inited(n) ((n)->size != 0)

int glw_rsx_init_context(struct glw_root *gr);


/**
 *
 */
#define glw_blendmode(m)

/**
 *
 */
#define glw_frontface(x)




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
int rsx_alloc(struct glw_root *gr, int size, int alignment);

void rsx_free(struct glw_root *gr, int pos, int size);

#define rsx_to_ppu(gr, pos) ((void *)((gr)->gr_be.be_rsx_address + (pos)))
