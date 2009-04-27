/*
 *  GL Widgets, Slider
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

#ifndef GLW_SLIDER_H_
#define GLW_SLIDER_H_

typedef struct {
  glw_t w;

  float knob_pos;
  float knob_size;
  float value;

  float min, max, step, step_i;

  int fixed_knob_size;

  prop_sub_t *sub;
  prop_t *p;
  float grab_delta;

  glw_t *bound_widget;

} glw_slider_t;

void glw_slider_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_SLIDER_H_ */
