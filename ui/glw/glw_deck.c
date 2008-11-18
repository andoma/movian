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
#include <GL/gl.h>

#include "glw.h"
#include "glw_i.h"
#include "glw_deck.h"
#include "glw_transitions.h"

#include <stdlib.h>
#include <assert.h>

/**
 *
 */
static int
glw_deck_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_rctx_t *rc = extra;
  
  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    if(w->glw_alpha < 0.01 || w->glw_selected == NULL)
      break;
    glw_layout(w->glw_selected, rc);
    break;
    
  case GLW_SIGNAL_RENDER:
    if(w->glw_alpha < 0.01 || w->glw_selected == NULL)
      break;
    glw_render0(w->glw_selected, rc);
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL)
      return glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra);
    break;

  case GLW_SIGNAL_SELECT:
    w->glw_selected = extra;
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    w->glw_selected = extra;
    break;
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

