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
#include <GL/gl.h>

#include "glw.h"
#include "glw_cursor.h"

/**
 * Render a widget with prior translation and scaling
 */
void
glw_render_TS(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  rc->rc_scale_x = prevrc->rc_scale_x * c->glw_parent_scale.x;
  rc->rc_scale_y = prevrc->rc_scale_y * c->glw_parent_scale.y;

  glPushMatrix();
  glTranslatef(c->glw_parent_pos.x,
	       c->glw_parent_pos.y,
	       c->glw_parent_pos.z);

  glScalef(c->glw_parent_scale.x,
	   c->glw_parent_scale.y,
	   c->glw_parent_scale.z);

  glw_signal0(c, GLW_SIGNAL_RENDER, rc);
  glPopMatrix();
}


/**
 * Render a widget with prior translation
 */
void
glw_render_T(glw_t *c, glw_rctx_t *rc, glw_rctx_t *prevrc)
{
  glPushMatrix();
  glTranslatef(c->glw_parent_pos.x,
	       c->glw_parent_pos.y,
	       c->glw_parent_pos.z);
  glw_signal0(c, GLW_SIGNAL_RENDER, rc);
  glPopMatrix();
}



/**
 *
 */
void
glw_rescale(float s_aspect, float t_aspect)
{
  float a = s_aspect / t_aspect;

  if(a > 1.0f) {
    glScalef(1.0f / a, 1.0f, 1.0f);
  } else {
    glScalef(1.0f, a, 1.0f);
  }
}


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
    gcp->gcp_scale_x = rc->rc_scale_x;
    gcp->gcp_scale_y = rc->rc_scale_y;
    memcpy(gcp->gcp_m, w->glw_matrix, 16 * sizeof(float));
  }
}

