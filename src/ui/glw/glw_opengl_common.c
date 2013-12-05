/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
glw_frontface(struct glw_root *gr, int how)
{
  if(how == gr->gr_be.gbr_frontface)
    return;
  gr->gr_be.gbr_frontface = how;

  if(gr->gr_be.gbr_delayed_rendering)
    return;

  glFrontFace(how == GLW_CW ? GL_CW : GL_CCW);
}



/**
 *
 */
void
glw_blendmode(struct glw_root *gr, int mode)
{
  if(mode == gr->gr_be.gbr_blendmode)
    return;
  gr->gr_be.gbr_blendmode = mode;

  if(gr->gr_be.gbr_delayed_rendering)
    return;

  switch(mode) {
  case GLW_BLEND_NORMAL:
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_ONE, GL_ONE);
    break;

  case GLW_BLEND_ADDITIVE:
    glBlendFuncSeparate(GL_SRC_COLOR, GL_ONE, GL_ONE, GL_ONE);
    break;
  }
}
