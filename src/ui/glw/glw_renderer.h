/*
 *  GLW rendering
 *  Copyright (C) 2010, 2011 Andreas Öman
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

/**
 * Renderer cache
 */
typedef struct glw_renderer_cache {
  union {
    Mtx grc_mtx; // ModelView matrix
    float grc_rgba[4];
  };
  int grc_active_clippers;
  float grc_clip[NUM_CLIPPLANES][4];

  float *grc_array;
  int grc_size;     // In triangles
  int grc_capacity; // In triangles
} glw_renderer_cache_t;

/**
 * Renderer
 */


typedef struct glw_renderer {
  uint16_t gr_vertices;
  uint16_t gr_triangles;
  char gr_static_indices;
  char gr_dirty;
  char gr_blended_attributes;
  char gr_color_attributes;
  unsigned char gr_framecmp;
  unsigned char gr_cacheptr;

  float *gr_array;
  uint16_t *gr_indices;

#define GLW_RENDERER_CACHES 4

  glw_renderer_cache_t *gr_cache[GLW_RENDERER_CACHES];
  
} glw_renderer_t;



/**
 * Public render interface abstraction
 */
void glw_renderer_init(glw_renderer_t *gr, int vertices, int triangles,
		       uint16_t *indices);

void glw_renderer_init_quad(glw_renderer_t *gr);

void glw_renderer_triangle(glw_renderer_t *gr, int element, 
			   uint16_t a, uint16_t b, uint16_t c);

int glw_renderer_initialized(glw_renderer_t *gr);

void glw_renderer_free(glw_renderer_t *gr);

void glw_renderer_vtx_pos(glw_renderer_t *gr, int vertex,
			  float x, float y, float z);

void glw_renderer_vtx_st(glw_renderer_t *gr, int vertex,
			 float s, float t);

void glw_renderer_vtx_col(glw_renderer_t *gr, int vertex,
			  float r, float g, float b, float a);


/**
 * Private
 */
int glw_renderer_get_cache_id(glw_root_t *root, glw_renderer_t *gr);

int glw_renderer_clippers_cmp(glw_renderer_cache_t *grc, glw_root_t *root);

void glw_renderer_clip_tesselate(glw_renderer_t *gr, glw_root_t *root,
				 glw_rctx_t *rc, int cache);
