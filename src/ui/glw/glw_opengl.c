/*
 *  libglw, OpenGL interface
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

#include <string.h>

#include "glw.h"
#include "glw_video_opengl.h"
#include "glw_cursor.h"

#include "fileaccess/fileaccess.h"

// #define DEBUG_SHADERS

static const float identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};
  
/**
 * return 1 if the extension is found, otherwise 0
 */
static int
check_gl_ext(const uint8_t *s, const char *func)
{
  int l = strlen(func);
  char *v;
  int x;

  v = strstr((const char *)s, func);
  x = v != NULL && v[l] < 33;

  TRACE(TRACE_DEBUG, "OpenGL", "Feature \"%s\" %savailable",
	func, x ? "" : "not ");
  return x;
}


/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;
  const	GLubyte	*s;
  int x = 0;
  int rectmode;
  GLuint vs, fs;
  /* Check OpenGL extensions we would like to have */

  s = glGetString(GL_EXTENSIONS);

  x |= check_gl_ext(s, "GL_ARB_pixel_buffer_object") ?
    GLW_OPENGL_PBO : 0;

  x |= check_gl_ext(s, "GL_ARB_fragment_program") ?
    GLW_OPENGL_FRAG_PROG : 0;

  gbr->gbr_sysfeatures = x;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if(check_gl_ext(s, "GL_ARB_texture_non_power_of_two")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_NPOT;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0;

#ifdef GL_TEXTURE_RECTANGLE_ARB
  } else if(check_gl_ext(s, "GL_ARB_texture_rectangle")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_RECTANGLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_RECTANGLE_ARB;
    rectmode = 1;
#endif

  } else {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_SIMPLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0; // WRONG
    
  }

  glEnable(gbr->gbr_primary_texture_mode);

  vs = glw_compile_shader("bundle://src/ui/glw/glsl/v1.glsl",
			  GL_VERTEX_SHADER);

  fs = glw_compile_shader("bundle://src/ui/glw/glsl/f_tex.glsl",
			  GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_tex = glw_make_program(gbr, "Texture", vs, fs);
  glDeleteShader(fs);

  fs = glw_compile_shader("bundle://src/ui/glw/glsl/f_alpha_tex.glsl",
			  GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_alpha_tex = glw_make_program(gbr, "Alpha texture", vs, fs);
  glDeleteShader(fs);

  fs = glw_compile_shader("bundle://src/ui/glw/glsl/f_flat.glsl",
			  GL_FRAGMENT_SHADER);
  gbr->gbr_renderer_flat = glw_make_program(gbr, "Flat", vs, fs);
  glDeleteShader(fs);

  glDeleteShader(vs);

#if CONFIG_GLW_BACKEND_OPENGL
  glw_video_opengl_init(gr, rectmode);
#endif
  return 0;
}


/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_width  = width;
  rc->rc_height = height;
  rc->rc_alpha = 1.0f;

  memset(&rc->rc_be.gbr_mtx, 0, sizeof(float) * 16);
  
  rc->rc_be.gbr_mtx[0]  = 1;
  rc->rc_be.gbr_mtx[5]  = 1;
  rc->rc_be.gbr_mtx[10] = 1;
  rc->rc_be.gbr_mtx[15] = 1;

  glw_Translatef(rc, 0, 0, -1 / tan(45 * M_PI / 360));
}


/**
 *
 */
void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;
  if(rc->rc_inhibit_matrix_store)
    return;

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(float) * 16);
  
  memcpy(w->glw_matrix, rc->rc_be.gbr_mtx, sizeof(float) * 16);

  if(glw_is_focused(w) && gcp != NULL) {
    gcp->gcp_alpha  = rc->rc_alpha;
    memcpy(gcp->gcp_m, w->glw_matrix, 16 * sizeof(float));
  }
}


static void
vec_addmul(float *dst, const float *a, const float *b, float s)
{
  dst[0] = a[0] + b[0] * s;
  dst[1] = a[1] + b[1] * s;
  dst[2] = a[2] + b[2] * s;
}

static void
vec_sub(float *dst, const float *a, const float *b)
{
  dst[0] = a[0] - b[0];
  dst[1] = a[1] - b[1];
  dst[2] = a[2] - b[2];
}


static void
vec_cross(float *dst, const float *a, const float *b)
{
  dst[0] = (a[1] * b[2]) - (a[2] * b[1]);
  dst[1] = (a[2] * b[0]) - (a[0] * b[2]);
  dst[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

static float
vec_dot(const float *a, const float *b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}


static inline void
mtx_mul_vec(float *dst, const float *mt, float x, float y, float z)
{
  dst[0] = mt[0] * x + mt[4] * y + mt[ 8] * z + mt[12];
  dst[1] = mt[1] * x + mt[5] * y + mt[ 9] * z + mt[13];
  dst[2] = mt[2] * x + mt[6] * y + mt[10] * z + mt[14];
}

static inline void
mtx_trans_mul_vec4(float *dst, const float *mt,
		   float x, float y, float z, float w)
{
  dst[0] = mt[ 0] * x + mt[ 1] * y + mt[ 2] * z + mt[ 3] * w;
  dst[1] = mt[ 4] * x + mt[ 5] * y + mt[ 6] * z + mt[ 7] * w;
  dst[2] = mt[ 8] * x + mt[ 9] * y + mt[10] * z + mt[11] * w;
  dst[3] = mt[12] * x + mt[13] * y + mt[14] * z + mt[15] * w;
}


static inline int
mtx_invert(float *dst, const float *src)
{
  float det = 
    src[0]*src[5]*src[10] + 
    src[4]*src[9]*src[2] + 
    src[8]*src[1]*src[6] -
    src[2]*src[5]*src[8] - 
    src[1]*src[4]*src[10] - 
    src[0]*src[6]*src[9];

  if(det == 0)
    return 0;

  det = 1.0f / det;

  dst[0]  =  (src[5]*src[10] - src[6]*src[9]) * det;
  dst[4]  = -(src[4]*src[10] - src[6]*src[8]) * det;
  dst[8]  =  (src[4]*src[9]  - src[5]*src[8]) * det;

  dst[1]  = -(src[1]*src[10] - src[2]*src[9]) * det;
  dst[5]  =  (src[0]*src[10] - src[2]*src[8]) * det;
  dst[9]  = -(src[0]*src[9]  - src[1]*src[8]) * det;

  dst[2]  =  (src[1]*src[6]  - src[2]*src[5]) * det;
  dst[6]  = -(src[0]*src[6]  - src[2]*src[4]) * det;
  dst[10] =  (src[0]*src[5]  - src[1]*src[4]) * det;

  dst[12] = -dst[0]*src[12] - dst[4]*src[13] - dst[8]*src[14];
  dst[13] = -dst[1]*src[12] - dst[5]*src[13] - dst[9]*src[14];
  dst[14] = -dst[2]*src[12] - dst[6]*src[13] - dst[10]*src[14];

  dst[3]  = 0;
  dst[7]  = 0;
  dst[11] = 0;
  dst[15] = 1;
  return 1;
}

typedef float Vector[3];
/**
 * m   Model matrix
 * x   Return x in model space
 * y   Return y in model space
 * p   Mouse pointer at camera z plane
 * dir Mouse pointer direction vector
 */
int
glw_widget_unproject(const float *m, float *xp, float *yp, 
		     const float *p, const float *dir)
{
  Vector u, v, n, w0, T0, T1, T2, out, I;
  float b, inv[16];

  mtx_mul_vec(T0, m, -1, -1, 0);
  mtx_mul_vec(T1, m,  1, -1, 0);
  mtx_mul_vec(T2, m,  1,  1, 0);

  vec_sub(u, T1, T0);
  vec_sub(v, T2, T0);
  vec_cross(n, u, v);
  
  vec_sub(w0, p, T0);
  b = vec_dot(n, dir);
  if(fabs(b) < 0.000001)
    return 0;

  vec_addmul(I, p, dir, -vec_dot(n, w0) / b);

  if(!mtx_invert(inv, m))
    return 0;
  mtx_mul_vec(out, inv, I[0], I[1], I[2]);

  *xp = out[0];
  *yp = out[1];
  return 1;
}



/**
 *
 */
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{
#if CONFIG_GLW_BACKEND_OPENGL
  glw_backend_root_t *gbr = &gr->gr_be;
  glw_load_program(gbr, gbr->gbr_renderer_flat);
  glw_program_set_modelview(gbr, rc);
  glw_program_set_uniform_color(gbr, 1,1,1,1);
  glDisable(GL_TEXTURE_2D);
  glBegin(GL_LINE_LOOP);
  glColor4f(1,1,1,1);
  glVertex3f(-1.0, -1.0, 0.0);
  glVertex3f( 1.0, -1.0, 0.0);
  glVertex3f( 1.0,  1.0, 0.0);
  glVertex3f(-1.0,  1.0, 0.0);
  glEnd();
  glEnable(GL_TEXTURE_2D);
#endif
}


/**
 *
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{
#if CONFIG_GLW_BACKEND_OPENGL
  glw_backend_root_t *gbr = &gr->gr_be;

  glw_load_program(gbr, gbr->gbr_renderer_flat);
  glw_program_set_modelview(gbr, rc);
  glw_program_set_uniform_color(gbr, 1,1,1,1);

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0,  1.0,  1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0, -1.0,  1.0);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f( 1.0,  1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0, -1.0,  1.0);
  glVertex3f( 1.0, -1.0, -1.0);
  glVertex3f(-1.0, -1.0, -1.0);
  glVertex3f(-1.0, -1.0,  1.0);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0,  1.0,  1.0);
  glVertex3f( 1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0, -1.0);
  glVertex3f(-1.0,  1.0,  1.0);
  glEnd();
#endif
}



/**
 * 
 */
void
glw_renderer_init(glw_renderer_t *gr, int vertices, int triangles,
		  uint16_t *indices)
{
  int i;

  gr->gr_array = malloc(sizeof(float) * (3 + 2 + 4) * vertices);
  gr->gr_vertices = vertices;

  if((gr->gr_static_indices = (indices != NULL))) {
    gr->gr_indices = indices;
  } else {
    gr->gr_indices = malloc(sizeof(uint16_t) * triangles * 3);
  }

  gr->gr_triangles = triangles;

  for(i = 0; i < vertices; i++) {
    gr->gr_array[i * 9 + 5] = 1;
    gr->gr_array[i * 9 + 6] = 1;
    gr->gr_array[i * 9 + 7] = 1;
    gr->gr_array[i * 9 + 8] = 1;
  }
  gr->gr_dirty = 1;
  gr->gr_blended_attribute = 0;
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
  free(gr->gr_array);
  gr->gr_array = NULL;

  if(!gr->gr_static_indices) {
    free(gr->gr_indices);
    gr->gr_indices = NULL;
  }
  for(i = 0; i < GLW_RENDERER_CACHES; i++) {
    if(gr->gr_tc[i] != NULL) {
      free(gr->gr_tc[i]->grt_array);
      free(gr->gr_tc[i]);
      gr->gr_tc[i] = NULL;
    }
  }
}


/**
 *
 */
int
glw_renderer_initialized(glw_renderer_t *gr)
{
  return !!gr->gr_array;
}

/**
 * 
 */
void
glw_renderer_vtx_pos(glw_renderer_t *gr, int vertex,
		     float x, float y, float z)
{
  gr->gr_array[vertex * 9 + 0] = x;
  gr->gr_array[vertex * 9 + 1] = y;
  gr->gr_array[vertex * 9 + 2] = z;
  gr->gr_dirty = 1;
}

/**
 * 
 */
void
glw_renderer_vtx_st(glw_renderer_t *gr, int vertex,
		    float s, float t)
{
  gr->gr_array[vertex * 9 + 3] = s;
  gr->gr_array[vertex * 9 + 4] = t;
  gr->gr_dirty = 1;
}

/**
 * 
 */
void
glw_renderer_vtx_col(glw_renderer_t *gr, int vertex,
		     float r, float g, float b, float a)
{
  gr->gr_array[vertex * 9 + 5] = r;
  gr->gr_array[vertex * 9 + 6] = g;
  gr->gr_array[vertex * 9 + 7] = b;
  gr->gr_array[vertex * 9 + 8] = a;
  if(a <= 0.99)
    gr->gr_blended_attribute = 1;
  gr->gr_dirty = 1;
}

/**
 *
 */
static void
clip_draw(glw_renderer_tc_t *grt,
	  const float *V1, const float *V2, const float *V3,
	  const float *C1, const float *C2, const float *C3,
	  const float *T1, const float *T2, const float *T3)
{
  if(grt->grt_size == grt->grt_capacity) {
    grt->grt_capacity++;
    grt->grt_array = realloc(grt->grt_array, 
			     sizeof(float) * 27 * grt->grt_capacity);
  }

  float *f = grt->grt_array + grt->grt_size * 27;
  grt->grt_size++;

  *f++ = V1[0];
  *f++ = V1[1];
  *f++ = V1[2];

  *f++ = T1[0];
  *f++ = T1[1];

  *f++ = C1[0];
  *f++ = C1[1];
  *f++ = C1[2];
  *f++ = C1[3];

  
  *f++ = V2[0];
  *f++ = V2[1];
  *f++ = V2[2];

  *f++ = T2[0];
  *f++ = T2[1];

  *f++ = C2[0];
  *f++ = C2[1];
  *f++ = C2[2];
  *f++ = C2[3];

  *f++ = V3[0];
  *f++ = V3[1];
  *f++ = V3[2];

  *f++ = T3[0];
  *f++ = T3[1];

  *f++ = C3[0];
  *f++ = C3[1];
  *f++ = C3[2];
  *f++ = C3[3];
}

#define LERP2v(dst, s, a, b) do { \
	dst[0] = a[0] + s * (b[0] - a[0]); \
	dst[1] = a[1] + s * (b[1] - a[1]); } while(0)

#define LERP3v(dst, s, a, b) do { \
	dst[0] = a[0] + s * (b[0] - a[0]); \
	dst[1] = a[1] + s * (b[1] - a[1]); \
	dst[2] = a[2] + s * (b[2] - a[2]); } while(0)

#define LERP4v(dst, s, a, b) do { \
	dst[0] = a[0] + s * (b[0] - a[0]); \
	dst[1] = a[1] + s * (b[1] - a[1]); \
	dst[2] = a[2] + s * (b[2] - a[2]); \
	dst[3] = a[3] + s * (b[3] - a[3]); } while(0)
  


/**
 * Clip a triangle in eye space
 */
static void
clipper(glw_renderer_tc_t *grt,
	const float *V1, const float *V2, const float *V3,
	const float *C1, const float *C2, const float *C3,
	const float *T1, const float *T2, const float *T3,
	int plane)
{
  while(1) {
    if(plane == NUM_CLIPPLANES) {
      clip_draw(grt, V1, V2, V3, C1, C2, C3, T1, T2, T3);
      return;
    }
    if(grt->grt_active_clippers & (1 << plane))
      break;
    plane++;
  }

  const float *P = grt->grt_clip[plane];
  plane++;

  float D1 = P[0] * V1[0] + P[1] * V1[1] + P[2] * V1[2] + P[3];
  float D2 = P[0] * V2[0] + P[1] * V2[1] + P[2] * V2[2] + P[3];
  float D3 = P[0] * V3[0] + P[1] * V3[1] + P[2] * V3[2] + P[3];

  float s12;
  float s13;
  float s23;

  float V12[3];
  float V13[3];
  float V23[3];

  float C12[4];
  float C13[4];
  float C23[4];

  float T12[2];
  float T13[2];
  float T23[2];

  if(D1 >= 0) {
    if(D2 >= 0) {
      if(D3 >= 0) {
	clipper(grt, V1, V2, V3, C1, C2, C3, T1, T2, T3, plane);
      } else {
	s13 = D1 / (D1 - D3);
	s23 = D2 / (D2 - D3);
	
	LERP3v(V13, s13, V1, V3);
	LERP3v(V23, s23, V2, V3);

	LERP4v(C13, s13, C1, C3);
	LERP4v(C23, s23, C2, C3);

	LERP2v(T13, s13, T1, T3);
	LERP2v(T23, s23, T2, T3);
	
	clipper(grt, V1,  V2, V23, C1,  C2, C23, T1, T2, T23, plane);
	clipper(grt, V1, V23, V13, C1, C23, C13, T1, T23, T13, plane);
      }

    } else {
      s12 = D1 / (D1 - D2);
      LERP3v(V12, s12, V1, V2);
      LERP4v(C12, s12, C1, C2);
      LERP2v(T12, s12, T1, T2);

      if(D3 >= 0) {
	s23 = D2 / (D2 - D3);
	LERP3v(V23, s23, V2, V3);
	LERP4v(C23, s23, C2, C3);
	LERP2v(T23, s23, T2, T3);

	clipper(grt, V1, V12, V23, C1, C12, C23, T1, T12, T23, plane);
	clipper(grt, V1, V23, V3,  C1, C23, C3,  T1, T23, T3, plane);

      } else {
	s13 = D1 / (D1 - D3);
	LERP3v(V13, s13, V1, V3);
	LERP4v(C13, s13, C1, C3);
	LERP2v(T13, s13, T1, T3);

	clipper(grt, V1, V12, V13, C1, C12, C13, T1, T12, T13, plane);
      }

    }
  } else {
    if(D2 >= 0) {
      s12 = D1 / (D1 - D2);
      LERP3v(V12, s12, V1, V2);
      LERP4v(C12, s12, C1, C2);
      LERP2v(T12, s12, T1, T2);
      
      if(D3 >= 0) {
	s13 = D1 / (D1 - D3);
	LERP3v(V13, s13, V1, V3);
	LERP4v(C13, s13, C1, C3);
	LERP2v(T13, s13, T1, T3);

	clipper(grt, V12, V2, V3,  C12, C2, C3,  T12, T2, T3, plane);
	clipper(grt, V12, V3, V13, C12, C3, C13, T12, T3, T13, plane);

      } else {
	s23 = D2 / (D2 - D3);
	LERP3v(V23, s23, V2, V3);
	LERP4v(C23, s23, C2, C3);
	LERP2v(T23, s23, T2, T3);

	clipper(grt, V12, V2, V23, C12, C2, C23, T12, T2, T23, plane);

      }
    } else {
      if(D3 >= 0) {
	s13 = D1 / (D1 - D3);
	s23 = D2 / (D2 - D3);
	
	LERP3v(V13, s13, V1, V3);
	LERP3v(V23, s23, V2, V3);

	LERP4v(C13, s13, C1, C3);
	LERP4v(C23, s23, C2, C3);

	LERP2v(T13, s13, T1, T3);
	LERP2v(T23, s23, T2, T3);

	clipper(grt, V13, V23, V3, C13, C23, C3, T13, T23, T3, plane);
      }
    }
  }
}


/**
 *
 */
static void
clip_tesselate(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc,
	       int cache)
{
  int i;
  uint16_t *ip = gr->gr_indices;
  const float *a = gr->gr_array;

  if(gr->gr_tc[cache] == NULL) {
    gr->gr_tc[cache] = calloc(1, sizeof(glw_renderer_tc_t));
    gr->gr_tc[cache]->grt_capacity = gr->gr_triangles;
    gr->gr_tc[cache]->grt_array = malloc(sizeof(float) * 27 *
					 gr->gr_tc[cache]->grt_capacity);
  }

  glw_renderer_tc_t *grt = gr->gr_tc[cache];
  grt->grt_size = 0;

  memcpy(grt->grt_mtx, rc->rc_be.gbr_mtx, sizeof(float) * 16);

  grt->grt_active_clippers = root->gr_be.gbr_active_clippers;

  for(i = 0; i < NUM_CLIPPLANES; i++)
    if((1 << i) & root->gr_be.gbr_active_clippers)
      memcpy(grt->grt_clip[i], root->gr_be.gbr_clip[i], 
	     sizeof(float) * 4);

  for(i = 0; i < gr->gr_triangles; i++) {
    int v1 = *ip++;
    int v2 = *ip++;
    int v3 = *ip++;

    float V1[3];
    float V2[3];
    float V3[3];
    
    mtx_mul_vec(V1, rc->rc_be.gbr_mtx, a[v1*9+0], a[v1*9+1], a[v1*9+2]);
    mtx_mul_vec(V2, rc->rc_be.gbr_mtx, a[v2*9+0], a[v2*9+1], a[v2*9+2]);
    mtx_mul_vec(V3, rc->rc_be.gbr_mtx, a[v3*9+0], a[v3*9+1], a[v3*9+2]);

    clipper(grt, V1, V2, V3,
	    &gr->gr_array[v1 * 9 + 5],
	    &gr->gr_array[v2 * 9 + 5],
	    &gr->gr_array[v3 * 9 + 5],
	    &gr->gr_array[v1 * 9 + 3],
	    &gr->gr_array[v2 * 9 + 3],
	    &gr->gr_array[v3 * 9 + 3],
	    0);
  }
}


/**
 *
 */
static int
grc_clippers_cmp(glw_renderer_tc_t *grt, glw_root_t *root)
{
  int i;

  if(grt->grt_active_clippers != root->gr_be.gbr_active_clippers)
    return 1;

  for(i = 0; i < NUM_CLIPPLANES; i++)
    if((1 << i) & root->gr_be.gbr_active_clippers)
      if(memcmp(grt->grt_clip[i], root->gr_be.gbr_clip[i], 
		sizeof(float) * 4))
	return 1;
  return 0;
}


/**
 * 
 */
void
glw_renderer_draw(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
		  glw_backend_texture_t *be_tex,
		  const glw_rgb_t *rgb, float alpha)
{
  glw_backend_root_t *gbr = &root->gr_be;
  glw_program_t *gp;
  int reenable_blend = 0;

  if(be_tex == NULL) {
    gp = gbr->gbr_renderer_flat;
  } else {

    switch(be_tex->type) {
    case GLW_TEXTURE_TYPE_ALPHA:
      gp = gbr->gbr_renderer_alpha_tex;
      break;

    case GLW_TEXTURE_TYPE_NORMAL:
      gp = gbr->gbr_renderer_tex;
      break;

    case GLW_TEXTURE_TYPE_NO_ALPHA:
      gp = gbr->gbr_renderer_tex;
      if(alpha > 0.99 && !gr->gr_blended_attribute) {
	glDisable(GL_BLEND);
	reenable_blend = 1;
      }
      break;

    default:
      return;
    }
    glBindTexture(gbr->gbr_primary_texture_mode, be_tex->tex);
  }

  if(gp == NULL)
    return;

  glw_load_program(gbr, gp);
  if(rgb != NULL)
    glw_program_set_uniform_color(gbr, rgb->r, rgb->g, rgb->b, alpha);
  else
    glw_program_set_uniform_color(gbr, 1, 1, 1, alpha);

  if(gbr->gbr_active_clippers) {
    float *A;

    if((root->gr_frames & 0xff ) != gr->gr_framecmp) {
      gr->gr_cacheptr = 0;
      gr->gr_framecmp = root->gr_frames & 0xff;
    } else {
      gr->gr_cacheptr = (gr->gr_cacheptr + 1) & (GLW_RENDERER_CACHES - 1);
    }

    int cacheid = gr->gr_cacheptr;

    if(gr->gr_dirty || gr->gr_tc[cacheid] == NULL ||
       memcmp(gr->gr_tc[cacheid]->grt_mtx,
	      rc->rc_be.gbr_mtx, sizeof(float) * 16) ||
       grc_clippers_cmp(gr->gr_tc[cacheid], root)) {

      clip_tesselate(gr, root, rc, cacheid);
    }
    
    glw_program_set_modelview(gbr, NULL);

    A = gr->gr_tc[cacheid]->grt_array;

    glVertexAttribPointer(gp->gp_attribute_position,
			  3, GL_FLOAT, 0, sizeof(float) * 9, A);
    glVertexAttribPointer(gp->gp_attribute_color,
			  4, GL_FLOAT, 0, sizeof(float) * 9, A + 5);

    if(gp->gp_attribute_texcoord != -1)
      glVertexAttribPointer(gp->gp_attribute_texcoord,
			    2, GL_FLOAT, 0, sizeof(float) * 9, A + 3);

    glDrawArrays(GL_TRIANGLES, 0, 3 * gr->gr_tc[cacheid]->grt_size);

  } else {

    glw_program_set_modelview(gbr, rc);

    glVertexAttribPointer(gp->gp_attribute_position,
			  3, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array);
    glVertexAttribPointer(gp->gp_attribute_color,
			  4, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array + 5);
    if(gp->gp_attribute_texcoord != -1)
      glVertexAttribPointer(gp->gp_attribute_texcoord,
			    2, GL_FLOAT, 0, sizeof(float) * 9, gr->gr_array+3);

    glDrawElements(GL_TRIANGLES, 3 * gr->gr_triangles, GL_UNSIGNED_SHORT,
		   gr->gr_indices);
  }
  gr->gr_dirty = 0;

  if(reenable_blend)
    glEnable(GL_BLEND);
}


/**
 *
 */
void
glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z)
{
  float s = sinf(GLW_DEG2RAD(a));
  float c = cosf(GLW_DEG2RAD(a));
  float t = 1.0 - c;
  float n = 1 / sqrtf(x*x + y*y + z*z);
  float m[16];
  float *o = rc->rc_be.gbr_mtx;
  float p[16];

  x *= n;
  y *= n;
  z *= n;
  
  m[ 0] = t * x * x + c;
  m[ 4] = t * x * y - s * z;
  m[ 8] = t * x * z + s * y;
  m[12] = 0;

  m[ 1] = t * y * x + s * z;
  m[ 5] = t * y * y + c;
  m[ 9] = t * y * z - s * x;
  m[13] = 0;

  m[ 2] = t * z * x - s * y;
  m[ 6] = t * z * y + s * x;
  m[10] = t * z * z + c;
  m[14] = 0;

  p[0]  = o[0]*m[0]  + o[4]*m[1]  + o[8]*m[2];
  p[4]  = o[0]*m[4]  + o[4]*m[5]  + o[8]*m[6];
  p[8]  = o[0]*m[8]  + o[4]*m[9]  + o[8]*m[10];
  p[12] = o[0]*m[12] + o[4]*m[13] + o[8]*m[14] + o[12];
 
  p[1]  = o[1]*m[0]  + o[5]*m[1]  + o[9]*m[2];
  p[5]  = o[1]*m[4]  + o[5]*m[5]  + o[9]*m[6];
  p[9]  = o[1]*m[8]  + o[5]*m[9]  + o[9]*m[10];
  p[13] = o[1]*m[12] + o[5]*m[13] + o[9]*m[14] + o[13];
  
  p[2]  = o[2]*m[0]  + o[6]*m[1]  + o[10]*m[2];
  p[6]  = o[2]*m[4]  + o[6]*m[5]  + o[10]*m[6];
  p[10] = o[2]*m[8]  + o[6]*m[9]  + o[10]*m[10];
  p[14] = o[2]*m[12] + o[6]*m[13] + o[10]*m[14] + o[14];

  p[ 3] = 0;
  p[ 7] = 0;
  p[11] = 0;
  p[15] = 1;

  memcpy(o, p, sizeof(float) * 16);
}




/**
 *
 */
static const float clip_planes[4][4] = {
  [GLW_CLIP_TOP]    = { 0.0, -1.0, 0.0, 1.0},
  [GLW_CLIP_BOTTOM] = { 0.0,  1.0, 0.0, 1.0},
  [GLW_CLIP_LEFT]   = {-1.0,  0.0, 0.0, 1.0},
  [GLW_CLIP_RIGHT]  = { 1.0,  0.0, 0.0, 1.0},
};


/**
 *
 */
int
glw_clip_enable(glw_root_t *gr, glw_rctx_t *rc, glw_clip_boundary_t how)
{
  int i;
  float inv[16];
  for(i = 0; i < NUM_CLIPPLANES; i++)
    if(!(gr->gr_be.gbr_active_clippers & (1 << i)))
      break;

  if(i == NUM_CLIPPLANES)
    return -1;

  if(!mtx_invert(inv, rc->rc_be.gbr_mtx))
    return -1;

  gr->gr_be.gbr_active_clippers |= (1 << i);
  
  mtx_trans_mul_vec4(gr->gr_be.gbr_clip[i], inv, 
		     clip_planes[how][0],
		     clip_planes[how][1],
		     clip_planes[how][2],
		     clip_planes[how][3]);
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

  gr->gr_be.gbr_active_clippers &= ~(1 << which);
}


/**
 *
 */
GLuint
glw_compile_shader(const char *url, int type)
{
  char *src;
  struct fa_stat st;
  GLint v, len;
  GLuint s;
  char log[4096];
  
  if((src = fa_quickload(url, &st, NULL, log, sizeof(log))) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, log);
    return 0;
  }
  
  s = glCreateShader(type);
  glShaderSource(s, 1, (const char **)&src, NULL);
  
  glCompileShader(s);
  glGetShaderInfoLog(s, sizeof(log), &len, log); 
  glGetShaderiv(s, GL_COMPILE_STATUS, &v);
    
  free(src);

  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to compile shader %s", url);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return 0;
  }
  return s;
}


/**
 *
 */
glw_program_t *
glw_make_program(glw_backend_root_t *gbr, const char *title,
		 GLuint vs, GLuint fs)
{
  char log[4096];
  GLsizei len;
  GLint v;
  GLuint p;
  int i;

  p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);

  glGetProgramInfoLog(p, sizeof(log), &len, log); 

  glGetProgramiv(p, GL_LINK_STATUS, &v);
  if(!v) {
    TRACE(TRACE_ERROR, "GLW", "Unable to link shader %s", title);
    TRACE(TRACE_ERROR, "GLW", "%s", log);
    return NULL;
  }

  glw_program_t *gp = calloc(1, sizeof(glw_program_t));

  gp->gp_title = strdup(title);
  gp->gp_program = p;

  glUseProgram(p);
  gbr->gbr_current = gp;

  gp->gp_attribute_position = glGetAttribLocation(p, "a_position");
  gp->gp_attribute_texcoord = glGetAttribLocation(p, "a_texcoord");
  gp->gp_attribute_color    = glGetAttribLocation(p, "a_color");

  gp->gp_uniform_modelview  = glGetUniformLocation(p, "u_modelview");
  gp->gp_uniform_color      = glGetUniformLocation(p, "u_color");
  gp->gp_uniform_colormtx   = glGetUniformLocation(p, "u_colormtx");
  gp->gp_uniform_blend      = glGetUniformLocation(p, "u_blend");
  
#ifdef DEBUG_SHADERS
  printf("Loaded %s\n", title);
  printf("  a_position  = %d\n", gp->gp_attribute_position);
  printf("  a_texcoord  = %d\n", gp->gp_attribute_texcoord);
  printf("  a_color     = %d\n", gp->gp_attribute_color);

  printf("  u_modelview = %d\n", gp->gp_uniform_modelview);
  printf("  u_color     = %d\n", gp->gp_uniform_color);
  printf("  u_colormtx  = %d\n", gp->gp_uniform_colormtx);
  printf("  u_blend     = %d\n", gp->gp_uniform_blend);
#endif

  for(i = 0; i < 6; i++) {
    char name[8];
    snprintf(name, sizeof(name), "u_t%d", i);
    gp->gp_uniform_t[i]         = glGetUniformLocation(p, name);
    if(gp->gp_uniform_t[i] != -1)
      glUniform1i(gp->gp_uniform_t[i], i);
#ifdef DEBUG_SHADERS
    printf("  u_t%d       = %d\n", i, gp->gp_uniform_t[i]);
#endif
  }
  return gp;
}

/**
 *
 */
void
glw_load_program(glw_backend_root_t *gbr, glw_program_t *gp)
{
  if(gbr->gbr_current == gp)
    return;

  if(gbr->gbr_current != NULL) {
    glw_program_t *old = gbr->gbr_current;
    if(old->gp_attribute_position != -1)
      glDisableVertexAttribArray(old->gp_attribute_position);
    if(old->gp_attribute_texcoord != -1)
      glDisableVertexAttribArray(old->gp_attribute_texcoord);
    if(old->gp_attribute_color != -1)
      glDisableVertexAttribArray(old->gp_attribute_color);
  }

  gbr->gbr_current = gp;

  if(gp == NULL) {
    glUseProgram(0);
    return;
  }

  glUseProgram(gp->gp_program);

  if(gp->gp_attribute_position != -1)
      glEnableVertexAttribArray(gp->gp_attribute_position);
  if(gp->gp_attribute_texcoord != -1)
    glEnableVertexAttribArray(gp->gp_attribute_texcoord);
  if(gp->gp_attribute_color != -1)
    glEnableVertexAttribArray(gp->gp_attribute_color);
}



/**
 *
 */
void
glw_program_set_modelview(glw_backend_root_t *gbr, glw_rctx_t *rc)
{
  const float *m = rc ? rc->rc_be.gbr_mtx : identitymtx;
  glUniformMatrix4fv(gbr->gbr_current->gp_uniform_modelview, 1, 0, m);
}

/**
 *
 */
void
glw_program_set_uniform_color(glw_backend_root_t *gbr,
			      float r, float g, float b, float a)
{
  glUniform4f(gbr->gbr_current->gp_uniform_color, r, g, b, a);
}


