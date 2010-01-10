/*
 *  GL Widgets, Displacement
 *  Copyright (C) 2010 Andreas Öman
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

#ifndef GLW_DISPLACEMENT_H_
#define GLW_DISPLACEMENT_H_

typedef struct {
  glw_t w;

  int16_t gd_border_left;
  int16_t gd_border_right;
  int16_t gd_border_top;
  int16_t gd_border_bottom;

  float gd_border_xs;
  float gd_border_ys;
  float gd_border_xt;
  float gd_border_yt;
  

} glw_displacement_t;




void glw_displacement_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_DISPLACEMENT_H_ */
