/*
 *  GL Widgets, Deck
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

#ifndef GLW_DECK_H_
#define GLW_DECK_H_

#define GLW_DECK_WIDGETS 3 /* Max number of widget to display 
			      at the same time */

typedef struct {
  glw_t w;

  float prev, tgt, k, inc;
  glw_transition_type_t efx_conf;

} glw_deck_t;




void glw_deck_ctor(glw_t *w, int init, va_list ap);

#endif /* GLW_DECK_H_ */
