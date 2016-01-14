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
#include "glw.h"



/**
 *
 */
void
glw_rtt_init(glw_root_t *gr, glw_rtt_t *grtt, int width, int height,
	     int alpha)
{
  const int m = GL_TEXTURE_2D;
  int mode;

  grtt->grtt_width  = width;
  grtt->grtt_height = height;

  glGenTextures(1, grtt->grtt_texture.textures);

  glBindTexture(m, grtt->grtt_texture.textures[0]);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  mode = alpha ? GL_RGBA : GL_RGB;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

  glTexImage2D(m, 0, mode, width, height, 0, mode, GL_UNSIGNED_BYTE, NULL);
  glPixelStorei(GL_UNPACK_ALIGNMENT, PIXMAP_ROW_ALIGN);
  glGenFramebuffersEXT(1, &grtt->grtt_framebuffer);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, grtt->grtt_framebuffer);
  glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
			    GL_COLOR_ATTACHMENT0_EXT,
			    m, grtt->grtt_texture.textures[0], 0);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
}


/**
 *
 */
void
glw_rtt_enter(glw_root_t *gr, glw_rtt_t *grtt, glw_rctx_t *rc)
{
  /* Save viewport */
  glGetIntegerv(GL_VIEWPORT, grtt->grtt_viewport);

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, grtt->grtt_framebuffer);
  
  glViewport(0, 0, grtt->grtt_width, grtt->grtt_height);

  glClear(GL_COLOR_BUFFER_BIT);

  abort();
  glw_rctx_init(rc, grtt->grtt_width, grtt->grtt_height, 0, NULL);
}


/**
 *
 */
void
glw_rtt_restore(glw_root_t *gr, glw_rtt_t *grtt)
{
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

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
  glDeleteTextures(1, grtt->grtt_texture.textures);
  glDeleteFramebuffersEXT(1, &grtt->grtt_framebuffer);
}


/**
 *
 */
static pixmap_t *
opengl_read_pixels(glw_root_t *gr)
{
  pixmap_t *pm = pixmap_create(gr->gr_width, gr->gr_height, PIXMAP_BGR32, 0);

  glReadPixels(0, 0, gr->gr_width, gr->gr_height,
               GL_BGRA, GL_UNSIGNED_BYTE, pm->pm_data);
  return pm;
}


/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  GLint tu = 0;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, PIXMAP_ROW_ALIGN);
  glPixelStorei(GL_PACK_ALIGNMENT, PIXMAP_ROW_ALIGN);

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

  gr->gr_br_read_pixels = opengl_read_pixels;

  return glw_opengl_shaders_init(gr);
}


/**
 *
 */
void
glw_opengl_fini_context(glw_root_t *gr)
{
  glw_opengl_shaders_fini(gr);
}
