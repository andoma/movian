/*
 *  GL Widgets, Slideshow
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

#ifndef GLW_SLIDESHOW_H_
#define GLW_SLIDESHOW_H_

typedef struct glw_slideshow {
  glw_t w;

  int timer;

  int displaytime;

} glw_slideshow_t;



void glw_slideshow_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_SLIDESHOW_H_ */
