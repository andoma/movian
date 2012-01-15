/*
 *  OpenGL GLX specifics
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

  glGenTextures(1, &grtt->grtt_texture.tex);
    
  glBindTexture(m, grtt->grtt_texture.tex);
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
			    m, grtt->grtt_texture.tex, 0);

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

  glClear(GL_COLOR_BUFFER_BIT);

  glw_rctx_init(rc, grtt->grtt_width, grtt->grtt_height, 0);
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
  glDeleteTextures(1, &grtt->grtt_texture.tex);
  glDeleteFramebuffersEXT(1, &grtt->grtt_framebuffer);
}

