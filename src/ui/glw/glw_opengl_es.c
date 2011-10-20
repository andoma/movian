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
 *
 */
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{
  // NOT IMPLEMENTED
}


/**
 *
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{
  // NOT IMPLEMENTED
}




/**
 *
 */
int
glw_opengl_init_context(glw_root_t *gr)
{
  glw_backend_root_t *gbr = &gr->gr_be;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_CULL_FACE);
  gbr->gbr_culling = 1;

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // We should try to fix this

  gbr->gbr_texmode = GLW_OPENGL_TEXTURE_NPOT;
  gbr->gbr_primary_texture_mode = GL_TEXTURE_2D;
  gr->gr_normalized_texture_coords = 1;

  glEnable(gbr->gbr_primary_texture_mode);

  const char *vendor   = (const char *)glGetString(GL_VENDOR);
  const char *renderer = (const char *)glGetString(GL_RENDERER);
  TRACE(TRACE_INFO, "GLW", "OpenGL Renderer: '%s' by '%s'", renderer, vendor);

  return glw_opengl_shaders_init(gr);
}
