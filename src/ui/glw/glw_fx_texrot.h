/*
 *  GL Widgets
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

#ifndef GLW_FX_TEXROT_H
#define GLW_FX_TEXROT_H

#include "glw_texture.h"

struct fxplate {
  float angle;
  float inc;

  float x, y;

  float texalpha;
  float texbeta;

  float tex1;
  float tex2;

  float coldrive;
  float colinc;

};

#define FX_NPLATES 10

typedef struct glw_fx_texrot {
  glw_t w;

  glw_loadable_texture_t *fx_tex;

  unsigned int fx_fb;
  unsigned int fx_fbtex;

  int fx_texsize;

  struct fxplate fx_plates[10];

  glw_gf_ctrl_t fx_flushctrl;

} glw_fx_texrot_t;

void glw_fx_texrot_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_FX_TEXROT_H */
