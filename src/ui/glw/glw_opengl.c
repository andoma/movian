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

  } else if(check_gl_ext(s, "GL_ARB_texture_rectangle")) {
    gr->gr_be.gbr_texmode = GLW_OPENGL_TEXTURE_RECTANGLE;
    gr->gr_be.gbr_primary_texture_mode = GL_TEXTURE_RECTANGLE_ARB;
    rectmode = 1;

  } else {
    gr->gr_be.gbr_texmode = GLW_OPENGL_TEXTURE_SIMPLE;
    gr->gr_be.gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    rectmode = 0; // WRONG
    
  }

  glEnable(gr->gr_be.gbr_primary_texture_mode);
  glw_video_opengl_init(gr, rectmode);

  return 0;
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
  
  glGetFloatv(GL_MODELVIEW_MATRIX, w->glw_matrix);
  
  if(glw_is_focused(w) && gcp != NULL) {
    gcp->gcp_alpha  = rc->rc_alpha;
    memcpy(gcp->gcp_m, w->glw_matrix, 16 * sizeof(float));
  }
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
glw_clip_enable(glw_rctx_t *rc, glw_clip_boundary_t how)
{
  int i;
  for(i = 0; i < 6; i++)
    if(!(rc->rc_be.gbr_active_clippers & (1 << i)))
      break;

  if(i == 6)
    return -1;

  rc->rc_be.gbr_active_clippers |= (1 << i);

  glClipPlane(GL_CLIP_PLANE0 + i, clip_planes[how]);
  glEnable(GL_CLIP_PLANE0 + i);
  return i;
}


/**
 *
 */
void
glw_clip_disable(glw_rctx_t *rc, int which)
{
  if(which == -1)
    return;

  rc->rc_be.gbr_active_clippers &= ~(1 << which);
  glDisable(GL_CLIP_PLANE0 + which);
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
glw_rtt_init(glw_root_t *gr, glw_rtt_t *grtt, int width, int height,
	     int alpha)
{
  int m = gr->gr_be.gbr_primary_texture_mode;
  int mode;

  grtt->grtt_width  = width;
  grtt->grtt_height = height;

  glGenTextures(1, &grtt->grtt_texture);
    
  glBindTexture(m, grtt->grtt_texture);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  mode = alpha ? GL_RGBA : GL_RGB;

  glTexImage2D(m, 0, mode, width, height, 0, mode, GL_UNSIGNED_BYTE, NULL);
  glGenFramebuffersEXT(1, &grtt->grtt_framebuffer);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, grtt->grtt_framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
			    GL_COLOR_ATTACHMENT0_EXT,
			    m, grtt->grtt_texture, 0);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}


/**
 *
 */
void
glw_rtt_enter(glw_root_t *gr, glw_rtt_t *grtt, glw_rctx_t *rc)
{
  int m = gr->gr_be.gbr_primary_texture_mode;

  /* Save viewport */
  glGetIntegerv(GL_VIEWPORT, grtt->grtt_viewport);

  glBindTexture(m, 0);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, grtt->grtt_framebuffer);
  
  glViewport(0, 0, grtt->grtt_width, grtt->grtt_height);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  gluLookAt(0, 0, 1 / tan(45 * M_PI / 360),
	    0, 0, 1,
	    0, 1, 0);

  glClear(GL_COLOR_BUFFER_BIT);

  glw_rctx_init(rc, grtt->grtt_width, grtt->grtt_height);
}


/**
 *
 */
void
glw_rtt_restore(glw_root_t *gr, glw_rtt_t *grtt)
{
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  /* Restore viewport */
  glViewport(grtt->grtt_viewport[0],
	     grtt->grtt_viewport[1],
	     grtt->grtt_viewport[2],
	     grtt->grtt_viewport[3]);
}


/**
 *
 */
void
glw_rtt_destroy(glw_root_t *gr, glw_rtt_t *grtt)
{
  glDeleteTextures(1, &grtt->grtt_texture);
  glDeleteFramebuffersEXT(1, &grtt->grtt_framebuffer);
}


/**
 *
 */
void
glw_wirebox(glw_rctx_t *rc)
{
  glDisable(GL_TEXTURE_2D);
  glBegin(GL_LINE_LOOP);
  glColor4f(1,1,1,1);
  glVertex3f(-1.0, -1.0, 0.0);
  glVertex3f( 1.0, -1.0, 0.0);
  glVertex3f( 1.0,  1.0, 0.0);
  glVertex3f(-1.0,  1.0, 0.0);
  glEnd();
  glEnable(GL_TEXTURE_2D);
}


/**
 * 
 */
void
glw_render_init(glw_renderer_t *gr, int vertices, int attribs)
{
  gr->gr_stride = 3 + 
    (attribs >= GLW_RENDER_ATTRIBS_TEX       ? 2 : 0) +
    (attribs >= GLW_RENDER_ATTRIBS_TEX_COLOR ? 4 : 0);
    
  gr->gr_buffer = malloc(sizeof(float) * gr->gr_stride * vertices);
  gr->gr_vertices = vertices;
}


/**
 * 
 */
void
glw_render_set_vertices(glw_renderer_t *gr, int vertices)
{
  gr->gr_vertices = vertices;
}


/**
 * 
 */
void
glw_render_free(glw_renderer_t *gr)
{
  free(gr->gr_buffer);
}


/**
 * 
 */
void
glw_render_vtx_pos(glw_renderer_t *gr, int vertex,
		   float x, float y, float z)
{
  gr->gr_buffer[vertex * gr->gr_stride + 0] = x;
  gr->gr_buffer[vertex * gr->gr_stride + 1] = y;
  gr->gr_buffer[vertex * gr->gr_stride + 2] = z;
}

/**
 * 
 */
void
glw_render_vtx_st(glw_renderer_t *gr, int vertex,
		  float s, float t)
{
  gr->gr_buffer[vertex * gr->gr_stride + 3] = s;
  gr->gr_buffer[vertex * gr->gr_stride + 4] = t;
}

/**
 * 
 */
void
glw_render_vts_col(glw_renderer_t *gr, int vertex,
		   float r, float g, float b, float a)
{
  gr->gr_buffer[vertex * gr->gr_stride + 5] = r;
  gr->gr_buffer[vertex * gr->gr_stride + 6] = g;
  gr->gr_buffer[vertex * gr->gr_stride + 7] = b;
  gr->gr_buffer[vertex * gr->gr_stride + 8] = a;
}



/**
 * 
 */
void
glw_render(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
	   int mode, int attribs,
	   glw_backend_texture_t *be_tex,
	   float r, float g, float b, float a)
{
  int i;
  float *buf = gr->gr_buffer;
  
  switch(attribs) {
  case GLW_RENDER_ATTRIBS_NONE:

    glDisable(root->gr_be.gbr_primary_texture_mode);

    glColor4f(r, g, b, a);
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();

    glEnable(root->gr_be.gbr_primary_texture_mode);
    break;
    
  case GLW_RENDER_ATTRIBS_COLOR:
    glDisable(root->gr_be.gbr_primary_texture_mode);
   
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glColor4f(buf[5], buf[6], buf[7], buf[8] * a);
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();

    glEnable(root->gr_be.gbr_primary_texture_mode);
    break;

  case GLW_RENDER_ATTRIBS_TEX:
    glBindTexture(root->gr_be.gbr_primary_texture_mode, *be_tex);
  
    glColor4f(r, g, b, a);
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glTexCoord2f(buf[3], buf[4]);
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();
    break;

  case GLW_RENDER_ATTRIBS_TEX_COLOR:
    glBindTexture(root->gr_be.gbr_primary_texture_mode, *be_tex);
  
    glBegin(mode);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      glColor4f(buf[5], buf[6], buf[7], buf[8] * a);
      glTexCoord2f(buf[3], buf[4]);
      glVertex3f(buf[0], buf[1], buf[2]);
      buf += gr->gr_stride;
    }
    glEnd();
    break;
  }
}
