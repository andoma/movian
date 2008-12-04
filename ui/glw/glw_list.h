/*
 *  GL Widgets, GLW_LIST widget
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

#ifndef GLW_LIST_H
#define GLW_LIST_H

typedef struct glw_list {
  glw_t w;

  int visible;

  int reposition_needed;

  float xs, ys; /* scale for childs */
  float xcenter, xcenter_target;
  float ycenter, ycenter_target;

  float expansion_factor; /* factor */

  glw_t *focused_child;

} glw_list_t;

void glw_list_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_LIST_H */
