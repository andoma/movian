/*
 *  GL Widgets, GLW_MAP
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

#ifndef GLW_MAP_H
#define GLW_MAP_H

typedef struct glw_map {
  glw_t w;

  glw_t *childs[GLW_POS_num];

} glw_map_t;

void glw_map_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_MAP_H */
