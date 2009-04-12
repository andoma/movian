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
#include "glw_cursor.h"

/**
 * return 1 if the extension is found, otherwise 0
 */
static int
check_gl_ext(const uint8_t *s, const char *func)
{
  int l = strlen(func);
  char *v;

  v = strstr((const char *)s, func);
  return v != NULL && v[l] < 33;
}

/**
 *
 */
void
glw_check_system_features(glw_root_t *gr)
{
  const	GLubyte	*s;
  int x = 0;

  /* Check OpenGL extensions we would like to have */

  s = glGetString(GL_EXTENSIONS);
  
  x |= check_gl_ext(s, "GL_ARB_pixel_buffer_object") ?
    GLW_OPENGL_PBO : 0;

  x |= check_gl_ext(s, "GL_ARB_vertex_buffer_object") ?
    GLW_OPENGL_VBO : 0;

  x |= check_gl_ext(s, "GL_ARB_fragment_program") ?
    GLW_OPENGL_FRAG_PROG : 0;

  x |= check_gl_ext(s, "GL_ARB_texture_non_power_of_two") ? 
    GLW_OPENGL_TNPO2 : 0;

  gr->gr_be.gbr_sysfeatures = x;
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


