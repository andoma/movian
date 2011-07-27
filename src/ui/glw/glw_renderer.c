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

#include "glw.h"
#include "glw_renderer.h"

static const glw_rgb_t white = {.r = 1,.g = 1,.b = 1};

/**
 * 
 */
void
glw_renderer_init(glw_renderer_t *gr, int num_vertices, int num_triangles,
		  uint16_t *indices)
{
  int i;

  gr->gr_vertices = malloc(sizeof(float) * VERTEX_SIZE * num_vertices);
  gr->gr_num_vertices = num_vertices;

  if((gr->gr_static_indices = (indices != NULL))) {
    gr->gr_indices = indices;
  } else {
    gr->gr_indices = malloc(sizeof(uint16_t) * num_triangles * 3);
  }

  gr->gr_num_triangles = num_triangles;

  for(i = 0; i < num_vertices; i++) {
    gr->gr_vertices[i * VERTEX_SIZE + 5] = 1;
    gr->gr_vertices[i * VERTEX_SIZE + 6] = 1;
    gr->gr_vertices[i * VERTEX_SIZE + 7] = 1;
    gr->gr_vertices[i * VERTEX_SIZE + 8] = 1;
  }
  gr->gr_dirty = 1;
  gr->gr_blended_attributes = 0;
  gr->gr_color_attributes = 0;
}


/**
 *
 */
void
glw_renderer_triangle(glw_renderer_t *gr, int element, 
		      uint16_t a, uint16_t b, uint16_t c)
{
  gr->gr_indices[element * 3 + 0] = a;
  gr->gr_indices[element * 3 + 1] = b;
  gr->gr_indices[element * 3 + 2] = c;
  gr->gr_dirty = 1;
}


/**
 *
 */
void
glw_renderer_init_quad(glw_renderer_t *gr)
{
  static uint16_t quad[6] = {
    0, 1, 2,
    0, 2, 3,
  };

  glw_renderer_init(gr, 4, 2, quad);
}


/**
 * 
 */
void
glw_renderer_free(glw_renderer_t *gr)
{
  int i;
  free(gr->gr_vertices);
  gr->gr_vertices = NULL;

  if(!gr->gr_static_indices) {
    free(gr->gr_indices);
    gr->gr_indices = NULL;
  }
  for(i = 0; i < GLW_RENDERER_CACHES; i++) {
    if(gr->gr_cache[i] != NULL) {
      free(gr->gr_cache[i]->grc_vertices);
      free(gr->gr_cache[i]);
      gr->gr_cache[i] = NULL;
    }
  }
}


/**
 *
 */
int
glw_renderer_initialized(glw_renderer_t *gr)
{
  return !!gr->gr_vertices;
}

/**
 * 
 */
void
glw_renderer_vtx_pos(glw_renderer_t *gr, int vertex,
		     float x, float y, float z)
{
  gr->gr_vertices[vertex * 9 + 0] = x;
  gr->gr_vertices[vertex * 9 + 1] = y;
  gr->gr_vertices[vertex * 9 + 2] = z;
  gr->gr_dirty = 1;
}

/**
 * 
 */
void
glw_renderer_vtx_st(glw_renderer_t *gr, int vertex,
		    float s, float t)
{
  gr->gr_vertices[vertex * 9 + 3] = s;
  gr->gr_vertices[vertex * 9 + 4] = t;
  gr->gr_dirty = 1;
}

/**
 * 
 */
void
glw_renderer_vtx_col(glw_renderer_t *gr, int vertex,
		     float r, float g, float b, float a)
{
  gr->gr_vertices[vertex * 9 + 5] = r;
  gr->gr_vertices[vertex * 9 + 6] = g;
  gr->gr_vertices[vertex * 9 + 7] = b;
  gr->gr_vertices[vertex * 9 + 8] = a;
  if(a <= 0.99)
    gr->gr_blended_attributes = 1;
  gr->gr_dirty = 1;
  gr->gr_color_attributes = 1;

}


/**
 *
 */
static void
clip_emit_triangle(glw_root_t *gr,
		   const Vec3 V1, const Vec3 V2, const Vec3 V3,
		   const Vec4 C1, const Vec4 C2, const Vec4 C3,
		   const Vec2 T1, const Vec2 T2, const Vec2 T3)
{
  if(gr->gr_vtmp_size + 3 > gr->gr_vtmp_capacity) {
    gr->gr_vtmp_capacity += 3;
    gr->gr_vtmp_buffer = realloc(gr->gr_vtmp_buffer, sizeof(float) *
				 VERTEX_SIZE * gr->gr_vtmp_capacity);
  }

  float *f = gr->gr_vtmp_buffer + gr->gr_vtmp_size * VERTEX_SIZE;
  gr->gr_vtmp_size += 3;

  glw_vec3_store(f,   V1);
  glw_vec2_store(f+3, T1);
  glw_vec4_store(f+5, C1);

  glw_vec3_store(f+9,   V2);
  glw_vec2_store(f+9+3, T2);
  glw_vec4_store(f+9+5, C2);

  glw_vec3_store(f+9+9,   V3);
  glw_vec2_store(f+9+9+3, T3);
  glw_vec4_store(f+9+9+5, C3);
}


/**
 * Clip a triangle in eye space
 */
static void
clipper(glw_root_t *gr, glw_renderer_cache_t *grc,
	const Vec3 V1, const Vec3 V2, const Vec3 V3,
	const Vec4 C1, const Vec4 C2, const Vec4 C3,
	const Vec2 T1, const Vec2 T2, const Vec2 T3,
	int plane)
{
  while(1) {
    if(plane == NUM_CLIPPLANES) {
      clip_emit_triangle(gr, V1, V2, V3, C1, C2, C3, T1, T2, T3);
      return;
    }
    if(grc->grc_active_clippers & (1 << plane))
      break;
    plane++;
  }

  const float D1 = glw_vec34_dot(V1, grc->grc_clip[plane]);
  const float D2 = glw_vec34_dot(V2, grc->grc_clip[plane]);
  const float D3 = glw_vec34_dot(V3, grc->grc_clip[plane]);

  plane++;

  float s12;
  float s13;
  float s23;

  Vec3 V12, V13, V23;
  Vec4 C12, C13, C23;
  Vec2 T12, T13, T23;

  if(D1 >= 0) {
    if(D2 >= 0) {
      if(D3 >= 0) {
	clipper(gr, grc, V1, V2, V3, C1, C2, C3, T1, T2, T3, plane);
      } else {
	s13 = D1 / (D1 - D3);
	s23 = D2 / (D2 - D3);
	
	glw_vec3_lerp(V13, s13, V1, V3);
	glw_vec3_lerp(V23, s23, V2, V3);

	glw_vec4_lerp(C13, s13, C1, C3);
	glw_vec4_lerp(C23, s23, C2, C3);

	glw_vec2_lerp(T13, s13, T1, T3);
	glw_vec2_lerp(T23, s23, T2, T3);
	
	clipper(gr, grc, V1,  V2, V23, C1,  C2, C23, T1, T2, T23, plane);
	clipper(gr, grc, V1, V23, V13, C1, C23, C13, T1, T23, T13, plane);
      }

    } else {
      s12 = D1 / (D1 - D2);
      glw_vec3_lerp(V12, s12, V1, V2);
      glw_vec4_lerp(C12, s12, C1, C2);
      glw_vec2_lerp(T12, s12, T1, T2);

      if(D3 >= 0) {
	s23 = D2 / (D2 - D3);
	glw_vec3_lerp(V23, s23, V2, V3);
	glw_vec4_lerp(C23, s23, C2, C3);
	glw_vec2_lerp(T23, s23, T2, T3);

	clipper(gr, grc, V1, V12, V23, C1, C12, C23, T1, T12, T23, plane);
	clipper(gr, grc, V1, V23, V3,  C1, C23, C3,  T1, T23, T3, plane);

      } else {
	s13 = D1 / (D1 - D3);
	glw_vec3_lerp(V13, s13, V1, V3);
	glw_vec4_lerp(C13, s13, C1, C3);
	glw_vec2_lerp(T13, s13, T1, T3);

	clipper(gr, grc, V1, V12, V13, C1, C12, C13, T1, T12, T13, plane);
      }

    }
  } else {
    if(D2 >= 0) {
      s12 = D1 / (D1 - D2);
      glw_vec3_lerp(V12, s12, V1, V2);
      glw_vec4_lerp(C12, s12, C1, C2);
      glw_vec2_lerp(T12, s12, T1, T2);
      
      if(D3 >= 0) {
	s13 = D1 / (D1 - D3);
	glw_vec3_lerp(V13, s13, V1, V3);
	glw_vec4_lerp(C13, s13, C1, C3);
	glw_vec2_lerp(T13, s13, T1, T3);

	clipper(gr, grc, V12, V2, V3,  C12, C2, C3,  T12, T2, T3, plane);
	clipper(gr, grc, V12, V3, V13, C12, C3, C13, T12, T3, T13, plane);

      } else {
	s23 = D2 / (D2 - D3);
	glw_vec3_lerp(V23, s23, V2, V3);
	glw_vec4_lerp(C23, s23, C2, C3);
	glw_vec2_lerp(T23, s23, T2, T3);

	clipper(gr, grc, V12, V2, V23, C12, C2, C23, T12, T2, T23, plane);

      }
    } else {
      if(D3 >= 0) {
	s13 = D1 / (D1 - D3);
	s23 = D2 / (D2 - D3);
	
	glw_vec3_lerp(V13, s13, V1, V3);
	glw_vec3_lerp(V23, s23, V2, V3);

	glw_vec4_lerp(C13, s13, C1, C3);
	glw_vec4_lerp(C23, s23, C2, C3);

	glw_vec2_lerp(T13, s13, T1, T3);
	glw_vec2_lerp(T23, s23, T2, T3);

	clipper(gr, grc, V13, V23, V3, C13, C23, C3, T13, T23, T3, plane);
      }
    }
  }
}


/**
 *
 */
static void
glw_renderer_clip_tesselate(glw_renderer_t *gr, glw_root_t *root,
			    glw_rctx_t *rc, glw_renderer_cache_t *grc)
{
  int i;
  uint16_t *ip = gr->gr_indices;
  const float *a = gr->gr_vertices;
  PMtx pmtx;
  
  root->gr_vtmp_size = 0;

  memcpy(grc->grc_mtx, rc->rc_mtx, sizeof(Mtx));

  grc->grc_active_clippers = root->gr_active_clippers;

  for(i = 0; i < NUM_CLIPPLANES; i++)
    if((1 << i) & root->gr_active_clippers)
      memcpy(&grc->grc_clip[i], &root->gr_clip[i], sizeof(Vec4));

  glw_pmtx_mul_prepare(pmtx, rc->rc_mtx);

  for(i = 0; i < gr->gr_num_triangles; i++) {
    int v1 = *ip++;
    int v2 = *ip++;
    int v3 = *ip++;

    Vec3 V1, V2, V3;

    glw_pmtx_mul_vec3(V1, pmtx, glw_vec3_get(a + v1*9));
    glw_pmtx_mul_vec3(V2, pmtx, glw_vec3_get(a + v2*9));
    glw_pmtx_mul_vec3(V3, pmtx, glw_vec3_get(a + v3*9));

    clipper(root, grc,
	    V1, V2, V3,
	    glw_vec4_get(a + v1 * 9 + 5),
	    glw_vec4_get(a + v2 * 9 + 5),
	    glw_vec4_get(a + v3 * 9 + 5),
	    glw_vec2_get(a + v1 * 9 + 3),
	    glw_vec2_get(a + v2 * 9 + 3),
	    glw_vec2_get(a + v3 * 9 + 3),
	    0);
  }

  int size = root->gr_vtmp_size * sizeof(float) * VERTEX_SIZE;

  if(root->gr_vtmp_size != grc->grc_num_vertices) {
    grc->grc_num_vertices = root->gr_vtmp_size;
    free(grc->grc_vertices);
    grc->grc_vertices = size ? malloc(size) : NULL;
  }

  if(size)
    memcpy(grc->grc_vertices, root->gr_vtmp_buffer, size);
}


/**
 *
 */
static int
glw_renderer_clippers_cmp(glw_renderer_cache_t *grc, glw_root_t *root)
{
  int i;

  if(grc->grc_active_clippers != root->gr_active_clippers)
    return 1;

  for(i = 0; i < NUM_CLIPPLANES; i++)
    if((1 << i) & root->gr_active_clippers)
      if(memcmp(&grc->grc_clip[i], &root->gr_clip[i], sizeof(Vec4)))
	return 1;
  return 0;
}


/**
 *
 */
static glw_renderer_cache_t *
glw_renderer_get_cache(glw_root_t *root, glw_renderer_t *gr)
{
  int idx;
  if((root->gr_frames & 0xff ) != gr->gr_framecmp) {
    gr->gr_cacheptr = 0;
    gr->gr_framecmp = root->gr_frames & 0xff;
  } else {
    gr->gr_cacheptr = (gr->gr_cacheptr + 1) & (GLW_RENDERER_CACHES - 1);
  }
  idx = gr->gr_cacheptr;

  if(gr->gr_cache[idx] == NULL)
    gr->gr_cache[idx] = calloc(1, sizeof(glw_renderer_cache_t));
  return gr->gr_cache[idx];
}


/**
 * This is the entry point of the rendering pipeline
 */
void
glw_renderer_draw(glw_renderer_t *gr, glw_root_t *root,
		  glw_rctx_t *rc, struct glw_backend_texture *tex,
		  const struct glw_rgb *rgb_mul,
		  const struct glw_rgb *rgb_off,
		  float alpha, float blur)
{
  rgb_mul = rgb_mul ?: &white;

  int flags = 
    gr->gr_color_attributes ? GLW_RENDER_COLOR_ATTRIBUTES : 0;

  if(root->gr_need_sw_clip) {
    glw_renderer_cache_t *grc = glw_renderer_get_cache(root, gr);

    if(gr->gr_dirty || 
       memcmp(grc->grc_mtx, rc->rc_mtx, sizeof(Mtx)) ||
       glw_renderer_clippers_cmp(grc, root)) {
      glw_renderer_clip_tesselate(gr, root, rc, grc);
    }

    root->gr_render(root, NULL, tex, rgb_mul, rgb_off, alpha, blur,
		    grc->grc_vertices, grc->grc_num_vertices,
		    NULL, 0, flags);
  } else {
    root->gr_render(root, rc->rc_mtx, tex, rgb_mul, rgb_off, alpha, blur,
		    gr->gr_vertices, gr->gr_num_vertices,
		    gr->gr_indices,  gr->gr_num_triangles,
		    flags);
  }
  gr->gr_dirty = 0;
}




/**
 *
 */
static const float clip_planes[4][3] = {
  [GLW_CLIP_TOP]    = { 0.0, -1.0, 0.0},
  [GLW_CLIP_BOTTOM] = { 0.0,  1.0, 0.0},
  [GLW_CLIP_LEFT]   = { 1.0,  0.0, 0.0},
  [GLW_CLIP_RIGHT]  = {-1.0,  0.0, 0.0},
};


/**
 *
 */
int
glw_clip_enable(glw_root_t *gr, glw_rctx_t *rc, glw_clip_boundary_t how,
		float distance)
{
  int i;
  Vec4 v4;
  for(i = 0; i < NUM_CLIPPLANES; i++)
    if(!(gr->gr_active_clippers & (1 << i)))
      break;

  if(i == NUM_CLIPPLANES)
    return -1;

  glw_vec4_copy(v4, glw_vec4_make(clip_planes[how][0],
				  clip_planes[how][1],
				  clip_planes[how][2],
				  1 - (distance * 2)));

  if(gr->gr_set_hw_clipper != NULL) {
    gr->gr_set_hw_clipper(rc, i, v4);

  } else {
    Mtx inv;

    if(!glw_mtx_invert(inv, rc->rc_mtx))
      return -1;

    glw_mtx_trans_mul_vec4(gr->gr_clip[i], inv, v4);
    gr->gr_need_sw_clip = 1;
  }

  gr->gr_active_clippers |= (1 << i);
  return i;
}


/**
 *
 */
void
glw_clip_disable(glw_root_t *gr, glw_rctx_t *rc, int which)
{
  if(which == -1)
    return;

  gr->gr_active_clippers &= ~(1 << which);

  if(gr->gr_clr_hw_clipper != NULL)
    gr->gr_clr_hw_clipper(rc, which);
  else
    gr->gr_need_sw_clip = gr->gr_active_clippers;
}
