/*
 *  GL Widgets, Animator
 *  Copyright (C) 2008 Andreas Öman
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

#ifndef GLW_ANIMATOR_H
#define GLW_ANIMATOR_H

typedef struct glw_animator {
  glw_t w;
  
  struct prop *prop;
  struct prop *prop_parent;

  float delta;
  float time;

  glw_transition_type_t efx_conf;

} glw_animator_t;

void glw_animator_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_ANIMATOR_H */
