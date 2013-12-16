#pragma once
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
#pragma once

extern glw_class_t glw_grid;

typedef struct glw_grid {
  glw_t w;

  float child_scale;

  int current_ypos;
  float filtered_ypos;

  int current_xpos;
  float filtered_xpos;

  glw_t *scroll_to_me;

} glw_grid_t;


typedef struct glw_gridrow {
  glw_t w;

  float child_scale;

  glw_t *scroll_to_me;
} glw_gridrow_t;
