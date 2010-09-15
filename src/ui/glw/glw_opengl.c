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


static void
perspective( GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar )
{
   GLfloat xmin, xmax, ymin, ymax;

   ymax = zNear * tan(fovy * M_PI / 360.0);
   ymin = -ymax;
   xmin = ymin * aspect;
   xmax = ymax * aspect;

   glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}


/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  const	GLubyte	*s;
  int x = 0;
  int rectmode;
  /* Check OpenGL extensions we would like to have */

  s = glGetString(GL_EXTENSIONS);

  x |= check_gl_ext(s, "GL_ARB_pixel_buffer_object") ?
    GLW_OPENGL_PBO : 0;

  x |= check_gl_ext(s, "GL_ARB_fragment_program") ?
    GLW_OPENGL_FRAG_PROG : 0;

  gr->gr_be.gbr_sysfeatures = x;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  if(check_gl_ext(s, "GL_ARB_texture_non_power_of_two")) {
    gr->gr_be.gbr_texmode = GLW_OPENGL_TEXTURE_NPOT;
    gr->gr_be.gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0;

#ifdef GL_TEXTURE_RECTANGLE_ARB
  } else if(check_gl_ext(s, "GL_ARB_texture_rectangle")) {
    gr->gr_be.gbr_texmode = GLW_OPENGL_TEXTURE_RECTANGLE;
    gr->gr_be.gbr_primary_texture_mode = GL_TEXTURE_RECTANGLE_ARB;
    rectmode = 1;
#endif

  } else {
    gr->gr_be.gbr_texmode = GLW_OPENGL_TEXTURE_SIMPLE;
    gr->gr_be.gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0; // WRONG
    
  }

  glEnable(gr->gr_be.gbr_primary_texture_mode);
#if CONFIG_GLW_BACKEND_OPENGL
  glw_video_opengl_init(gr, rectmode);
#endif
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  perspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  return 0;
}


/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_size_x = width;
  rc->rc_size_y = height;
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
glw_wirebox(glw_rctx_t *rc)
{
#if CONFIG_GLW_BACKEND_OPENGL
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
glw_renderer_init(glw_renderer_t *gr, int vertices, int triangles,
		  uint16_t *indices)
{
  int i;
  float *v;

  gr->gr_array = malloc(sizeof(float) * (3 + 2 + 4 + 4) * vertices);
  v = gr->gr_colors = gr->gr_array + (3 + 2 + 4) * vertices;
  gr->gr_vertices = vertices;

  if((gr->gr_static_indices = (indices != NULL))) {
    gr->gr_indices = indices;
  } else {
    gr->gr_indices = malloc(sizeof(uint16_t) * triangles * 3);
  }

  gr->gr_triangles = triangles;

  for(i = 0; i < vertices * 4; i++)
    *v++ = 1;
  gr->gr_alpha = -1000;
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
  free(gr->gr_array);
  gr->gr_array = NULL;

  if(!gr->gr_static_indices) {
    free(gr->gr_indices);
    gr->gr_indices = NULL;
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
}

/**
 * 
 */
void
glw_renderer_vtx_col(glw_renderer_t *gr, int vertex,
		     float r, float g, float b, float a)
{
  gr->gr_colors[vertex * 4 + 0] = r;
  gr->gr_colors[vertex * 4 + 1] = g;
  gr->gr_colors[vertex * 4 + 2] = b;
  gr->gr_colors[vertex * 4 + 3] = a;
  gr->gr_alpha = -1000;
}


/**
 * 
 */
void
glw_renderer_draw(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
		  glw_backend_texture_t *be_tex,
		  float r, float g, float b, float a)
{
  if(r != gr->gr_red || g != gr->gr_green || b != gr->gr_blue ||
     a != gr->gr_alpha) {

    const float *src = gr->gr_colors;
    int i;
  
    for(i = 0; i < gr->gr_vertices; i++) {
      gr->gr_array[i * 9 + 5] = *src++ * r;
      gr->gr_array[i * 9 + 6] = *src++ * g;
      gr->gr_array[i * 9 + 7] = *src++ * b;
      gr->gr_array[i * 9 + 8] = *src++ * a;
    }

    gr->gr_red   = r;
    gr->gr_green = g;
    gr->gr_blue  = b;
    gr->gr_alpha = a;
  }



  if(be_tex != NULL) {

    glLoadMatrixf(rc->rc_be.gbr_mtx);

    glBindTexture(root->gr_be.gbr_primary_texture_mode, *be_tex);

    glVertexPointer(  3, GL_FLOAT, sizeof(float) * 9, gr->gr_array);
    glTexCoordPointer(2, GL_FLOAT, sizeof(float) * 9, gr->gr_array + 3);
    glColorPointer(   4, GL_FLOAT, sizeof(float) * 9, gr->gr_array + 5);

    glDrawElements(GL_TRIANGLES, 3 * gr->gr_triangles, GL_UNSIGNED_SHORT,
		   gr->gr_indices);

  } else {

    glLoadMatrixf(rc->rc_be.gbr_mtx);

    glDisable(root->gr_be.gbr_primary_texture_mode);

    glVertexPointer(3, GL_FLOAT, sizeof(float) * 9, gr->gr_array);
    glColorPointer( 4, GL_FLOAT, sizeof(float) * 9, gr->gr_array + 5);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glDrawElements(GL_TRIANGLES, 3 * gr->gr_triangles, GL_UNSIGNED_SHORT,
		   gr->gr_indices);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glEnable(root->gr_be.gbr_primary_texture_mode);
  }
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
static const GLdouble clip_planes[4][4] = {
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
  for(i = 0; i < 6; i++)
    if(!(rc->rc_be.gbr_active_clippers & (1 << i)))
      break;

  if(i == 6)
    return -1;

  rc->rc_be.gbr_active_clippers |= (1 << i);

  glLoadMatrixf(rc->rc_be.gbr_mtx);

  glClipPlane(GL_CLIP_PLANE0 + i, clip_planes[how]);
  glEnable(GL_CLIP_PLANE0 + i);
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

  rc->rc_be.gbr_active_clippers &= ~(1 << which);
  glDisable(GL_CLIP_PLANE0 + which);
}

