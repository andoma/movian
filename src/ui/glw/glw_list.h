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

  float child_aspect;

  float center_y, center_y_target, center_y_max;
  float center_x, center_x_target, center_x_max;

  glw_t *scroll_to_me;

  glw_slider_metrics_t metrics;

} glw_list_t;

void glw_list_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_LIST_H */
