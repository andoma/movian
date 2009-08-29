/*
 *  GL Widgets, Freefloat
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef GLW_FREEFLOAT_H_
#define GLW_FREEFLOAT_H_

#define GLW_FREEFLOAT_MAX_VISIBLE 3

typedef struct glw_freefloat {
  glw_t w;

  int xpos;

  int num_visible;

  glw_t *pick;

  glw_t *visible[GLW_FREEFLOAT_MAX_VISIBLE];

  

} glw_freefloat_t;



void glw_freefloat_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_FREEFLOAT_H_ */
