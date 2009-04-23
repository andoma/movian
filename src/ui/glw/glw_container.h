/*
 *  GL Widgets, GLW_CONTAINER -widgets
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef GLW_CONTAINER_H
#define GLW_CONTAINER_H

typedef struct glw_container {
  glw_t w;
  
  int x_sum;
  int y_sum;
  float weight_sum;
  float aspect_sum;

} glw_container_t;



void glw_container_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_CONTAINER_H */
