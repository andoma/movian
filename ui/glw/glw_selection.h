/*
 *  GL Widgets, GLW_SELECTION widget
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

#ifndef GLW_SELECTION_H
#define GLW_SELECTION_H

typedef struct glw_selection {
  glw_t w;

  int reposition_needed;

  float ycenter, ycenter_target;

  float expandfactor;

  int active;

  float active_prim;

} glw_selection_t;

void glw_selection_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_LIST_H */
