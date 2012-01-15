/*
 *  glw OpenGL interface
 *  Copyright (C) 2008-2011 Andreas Öman
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
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{
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
}


/**
 *
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{
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
}




/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  GLint tu = 0;
  glw_backend_root_t *gbr = &gr->gr_be;
  const	GLubyte	*s;
  /* Check OpenGL extensions we would like to have */

  s = glGetString(GL_EXTENSIONS);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  gbr->gbr_culling = 1;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // We should try to fix this

  if(check_gl_ext(s, "GL_ARB_texture_non_power_of_two")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_NPOT;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;

#ifdef GL_TEXTURE_RECTANGLE_ARB
  } else if(check_gl_ext(s, "GL_ARB_texture_rectangle")) {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_RECTANGLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_RECTANGLE_ARB;
#endif

  } else {
    gbr->gbr_texmode = GLW_OPENGL_TEXTURE_SIMPLE;
    gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
    gr->gr_normalized_texture_coords = 1;
    
  }

  glEnable(gbr->gbr_primary_texture_mode);

  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &tu);
  if(tu < 6) {
    TRACE(TRACE_ERROR, "GLW", 
	  "Insufficient number of texture image units %d < 6 "
	  "for GLW video rendering widget.",
	  tu);
    return -1;
  }
  TRACE(TRACE_DEBUG, "GLW", "%d texture image units available", tu);

  const char *vendor   = (const char *)glGetString(GL_VENDOR);
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  TRACE(TRACE_INFO, "GLW", "OpenGL Renderer: '%s' by '%s'", renderer, vendor);

  int use_shaders = 1;

  if(strstr(renderer, "Mesa"))
      use_shaders = 0;

  if(use_shaders) {
    return glw_opengl_shaders_init(gr);
  } else {
    return glw_opengl_ff_init(gr);
  }
}
