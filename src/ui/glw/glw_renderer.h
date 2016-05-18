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


#define GLW_RENDER_BLUR_ATTRIBUTE   0x1 /* set if pos.w != 1 (sharpness)
					 * ie, the triangle should be blurred
					 */

#define GLW_RENDER_OPAQUE           0x2 // Can be rendered without blending

#define GLW_RENDER_DEBUG            0x4 // Turn on various debug


#define VERTEX_SIZE 12 // Number of floats per vertex

/**
 * Vertex layout
 *
 * [0] = x
 * [1] = y
 * [2] = z
 * [3] = s ( sharpness )
 * [4] = r
 * [5] = g
 * [6] = b
 * [7] = a
 * [8] = s0  Texture0
 * [9] = t0
 * [10] = s1 Texture1
 * [11] = t1
 */


/**
 * Renderer cache
 */
typedef struct glw_renderer_cache {
  Mtx grc_mtx; // ModelView matrix
  uint16_t grc_active_clippers;
  uint16_t grc_active_faders;
  Vec4 grc_clip[NUM_CLIPPLANES];

  Vec4 grc_stencil[2];
  int16_t grc_stencil_width;
  int16_t grc_stencil_height;
  int16_t grc_stencil_border[4];
  float grc_stencil_edge[4];

  Vec4 grc_fader[NUM_FADERS];
  float grc_fader_alpha[NUM_FADERS];
  float grc_fader_blur[NUM_FADERS];

  char grc_blurred;
  uint16_t grc_num_vertices;

  float *grc_vertices;
} glw_renderer_cache_t;

/**
 * Renderer
 */
typedef struct glw_renderer {
  float *gr_vertices;
  uint16_t *gr_indices;
#define GLW_RENDERER_CACHES 2
  glw_renderer_cache_t *gr_cache[GLW_RENDERER_CACHES];


  uint16_t gr_num_vertices;
  uint16_t gr_num_triangles;

  unsigned char gr_framecmp;
  unsigned char gr_cacheptr;
  char gr_static_indices : 1;
  char gr_dirty : 1;
  char gr_color_attributes : 1;

} glw_renderer_t;


struct glw_program_args {
  glw_program_t *gpa_prog;
  void *gpa_aux;
  void (*gpa_load_uniforms)(glw_root_t *gr, glw_program_t *prog, void *aux,
                            const struct glw_render_job *rj);
  void (*gpa_load_texture)(glw_root_t *gr, glw_program_t *prog, void *aux,
                           const struct glw_backend_texture *t, int num);
};


/**
 * Render job
 */
typedef struct glw_render_job {
  Mtx m;
  const struct glw_backend_texture *t0;
  const struct glw_backend_texture *t1;
  struct glw_program_args *gpa;
  struct glw_rgb rgb_mul;
  struct glw_rgb rgb_off;
  float alpha;
  float blur;
  int vertex_offset;
  int index_offset;
  int16_t num_vertices;
  int16_t num_indices;
  int16_t width;
  int16_t height;
  int16_t primitive_type;
  char blendmode;
  char flags;
  char eyespace : 1;
  char frontface : 1;
  char opaque ;
} glw_render_job_t;


typedef struct glw_render_order {
  glw_render_job_t *job;
  int16_t zindex;

} glw_render_order_t;

/**
 * Public render interface abstraction
 */
void glw_renderer_init(glw_renderer_t *gr, int vertices, int triangles,
		       uint16_t *indices);

void glw_renderer_init_quad(glw_renderer_t *gr);

void glw_renderer_init_triangle(glw_renderer_t *gr);

void glw_renderer_triangle(glw_renderer_t *gr, int element, 
			   uint16_t a, uint16_t b, uint16_t c);

int glw_renderer_initialized(glw_renderer_t *gr);

void glw_renderer_free(glw_renderer_t *gr);

void glw_renderer_vtx_pos(glw_renderer_t *gr, int vertex,
			  float x, float y, float z);

void glw_renderer_vtx_st(glw_renderer_t *gr, int vertex,
			 float s, float t);

void glw_renderer_vtx_st2(glw_renderer_t *gr, int vertex,
			  float s, float t);

void glw_renderer_vtx_col(glw_renderer_t *gr, int vertex,
			  float r, float g, float b, float a);

void glw_renderer_vtx_col_reset(glw_renderer_t *gr);

void glw_renderer_draw(glw_renderer_t *gr, glw_root_t *root,
		       const glw_rctx_t *rc,
		       const struct glw_backend_texture *tex,
		       const struct glw_backend_texture *tex2,
		       const struct glw_rgb *rgb_mul,
		       const struct glw_rgb *rgb_off,
		       float alpha, float blur,
		       glw_program_args_t *gpa);

void glw_vtmp_resize(glw_root_t *gr, int num_float);
