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
#include "glw_video.h"
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
  glDisable(GL_CULL_FACE);
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
  glw_video_global_init(gr, rectmode);

  return 0;
}


/**
 *
 */
void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;
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


/**
 * XXX: Replace with glProject or something more clever
 */
void
glw_widget_project(float *m, float *x1, float *x2, float *y1, float *y2)
{
  *x1 = m[12] - m[0];
  *x2 = m[12] + m[0];
  *y1 = m[13] - m[5];
  *y2 = m[13] + m[5];
}


/**
 *
 */
void
glw_rtt_init(glw_root_t *gr, glw_rtt_t *grtt, int width, int height)
{
  int m = gr->gr_be.gbr_primary_texture_mode;

  grtt->grtt_width  = width;
  grtt->grtt_height = height;

  glGenTextures(1, &grtt->grtt_texture);
    
  glBindTexture(m, grtt->grtt_texture);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(m, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
  
  glGenFramebuffersEXT(1, &grtt->grtt_framebuffer);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, grtt->grtt_framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
			    GL_COLOR_ATTACHMENT0_EXT,
			    m, grtt->grtt_framebuffer, 0);

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


  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_alpha = 1;
  rc->rc_size_x = grtt->grtt_width;
  rc->rc_size_y = grtt->grtt_height;
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
