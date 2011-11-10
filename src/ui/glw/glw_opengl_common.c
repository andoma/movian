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
glw_frontface(struct glw_root *gr, int how)
{
  glFrontFace(how == GLW_CW ? GL_CW : GL_CCW);
}



/**
 *
 */
void
glw_blendmode(struct glw_root *gr, int mode)
{
  if(mode == gr->gr_be.be_blendmode)
    return;
  gr->gr_be.be_blendmode = mode;

  switch(mode) {
  case GLW_BLEND_NORMAL:
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    break;

  case GLW_BLEND_ADDITIVE:
    glBlendFunc(GL_SRC_COLOR, GL_ONE);
    break;
  }
}
