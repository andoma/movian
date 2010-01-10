/*
 *  GL Widgets, Coverflow
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

#ifndef GLW_COVERFLOW_H_
#define GLW_COVERFLOW_H_

typedef struct {
  glw_t w;

  glw_t *scroll_to_me;

  float pos;
  float pos_target;

  glw_t *rstart;

  float xs, ys;

} glw_coverflow_t;


void glw_coverflow_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_COVERFLOW_H_ */
