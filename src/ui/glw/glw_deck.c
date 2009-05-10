/*
 *  GL Widgets, deck, transition between childs objects
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

#include "glw.h"
#include "glw_deck.h"
#include "glw_transitions.h"

/**
 *
 */
static int
glw_deck_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  glw_t *c, *n;
  event_t *e;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    if(w->glw_alpha < 0.01 || w->glw_selected == NULL)
      break;
    glw_layout0(w->glw_selected, rc);
    break;
    
  case GLW_SIGNAL_RENDER:
    if(w->glw_alpha < 0.01 || w->glw_selected == NULL)
      break;
    glw_render0(w->glw_selected, rc);
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL) {
      if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra))
	return 1;
    }

    if((c = w->glw_selected) == NULL)
      return 0;
    
    /* Respond to some events ourselfs */
    e = extra;

    switch(e->e_type) {
    case EVENT_INCR:
      n = glw_get_next_n(c, 1);
      break;
    case EVENT_DECR:
      n = glw_get_prev_n(c, 1);
      break;
    default:
      n = NULL;
      break;
    }

    if(n != NULL) {
      if(n != c)
	glw_select(w, n);
      return 1;
    }
    break;

  case GLW_SIGNAL_SELECT:
    w->glw_selected = extra;
    if(w->glw_selected != NULL) {
      glw_focus_open_path(w, w->glw_selected);
      glw_copy_constraints(w, w->glw_selected);
    } else {
      glw_clear_constraints(w);
    }
    break;

#if 0
  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    if(w->glw_selected == NULL) {
      w->glw_selected = c;
      glw_focus_open_path(w, c);
    }
    break;
#endif
  }

  return 0;
}

void 
glw_deck_ctor(glw_t *w, int init, va_list ap)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  glw_attribute_t attrib;

  if(init)
    glw_signal_handler_int(w, glw_deck_callback);

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TRANSITION_EFFECT:
      gd->efx_conf = va_arg(ap, int);
      break;
    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

 }

