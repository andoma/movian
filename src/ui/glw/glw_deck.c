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
#include "glw_transitions.h"

/**
 *
 */
typedef struct {
  glw_t w;

  float prev, tgt, k, inc;
  glw_transition_type_t efx_conf;

} glw_deck_t;




/**
 *
 */
static void
clear_constraints(glw_t *w)
{
  glw_set_constraints(w, 0, 0, 0, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  glw_signal0(w, GLW_SIGNAL_FULLSCREEN_CONSTRAINT_CHANGED, NULL);
}



/**
 *
 */
static void
glw_deck_update_constraints(glw_t *w)
{
  glw_t *c = w->glw_selected;

  int was_fullscreen = w->glw_flags & GLW_CONSTRAINT_F;

  glw_copy_constraints(w, c);

  if((w->glw_flags & GLW_CONSTRAINT_F) == was_fullscreen)
    return;

  glw_signal0(w, GLW_SIGNAL_FULLSCREEN_CONSTRAINT_CHANGED, NULL);
}

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

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL) {
      if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra))
	return 1;
    }

    if((c = w->glw_selected) == NULL)
      return 0;
    
    /* Respond to some events ourselfs */
    e = extra;

    if(event_is_action(e, ACTION_INCR)) {

      n = glw_get_next_n(c, 1);

    } else if(event_is_action(e, ACTION_DECR)) {

      n = glw_get_prev_n(c, 1);
      
    } else {

      break;

    }

    if(n != c && n != NULL)
      glw_select(w, n);
    return 1;

  case GLW_SIGNAL_SELECT:
    w->glw_selected = extra;
    if(w->glw_selected != NULL) {
      glw_focus_open_path_close_all_other(w->glw_selected);
      glw_deck_update_constraints(w);
    } else {
      clear_constraints(w);
    }
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_selected == extra)
      glw_deck_update_constraints(w);
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = extra;
    if(w->glw_selected == extra)
      clear_constraints(w);
    return 0;
  }

  return 0;
}

    
/**
 *
 */
static void 
glw_deck_render(glw_t *w, glw_rctx_t *rc)
{
  if(w->glw_alpha < 0.01 || w->glw_selected == NULL)
    return;
  w->glw_selected->glw_class->gc_render(w->glw_selected, rc);
}



/**
 *
 */
static void 
glw_deck_set(glw_t *w, int init, va_list ap)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  glw_attribute_t attrib;

  if(init)
    clear_constraints(w);

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


/**
 *
 */
static glw_class_t glw_deck = {
  .gc_name = "deck",
  .gc_instance_size = sizeof(glw_deck_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_SELECTED,
  .gc_render = glw_deck_render,
  .gc_set = glw_deck_set,
  .gc_signal_handler = glw_deck_callback,
};

GLW_REGISTER_CLASS(glw_deck);
