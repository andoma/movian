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
#include "event.h"

/**
 *
 */
typedef struct {
  glw_t w;
  glw_t *prev; // Widget we are transitioning from

  glw_transition_type_t efx_conf;
  float time;
  float delta;
  
  float v;
  char rev;

} glw_deck_t;




/**
 *
 */
static void
clear_constraints(glw_t *w)
{
  glw_set_constraints(w, 0, 0, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);
}



/**
 *
 */
static void
glw_deck_update_constraints(glw_t *w)
{
  glw_copy_constraints(w, w->glw_selected);
}


/**
 *
 */
static void
setprev(glw_deck_t *gd, glw_t *c)
{
  glw_t *l = gd->w.glw_selected;
  glw_t *p;
  int rev = 0;

  gd->prev = l;
  if(c == NULL)
    return;

  for(p = TAILQ_NEXT(c, glw_parent_link); p != NULL;
      p = TAILQ_NEXT(p, glw_parent_link)) {
    if(p == l) {
      rev = 1;
      break;
    }
  }
  gd->rev = rev;
}


/**
 *
 */
static void
deck_select_child(glw_t *w, glw_t *c, prop_t *origin)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  if(w->glw_selected == c)
    return;

  setprev(gd, c);
  w->glw_selected = c;
  if(w->glw_selected != NULL) {
    glw_focus_open_path_close_all_other(w->glw_selected);
    glw_deck_update_constraints(w);
  } else {
    clear_constraints(w);
  }

  if(gd->efx_conf != GLW_TRANS_NONE &&
     (gd->prev != NULL || !(w->glw_flags & GLW_NO_INITIAL_TRANS)))
    gd->v = 0;
}


/**
 *
 */
static int
glw_deck_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  glw_rctx_t *rc = extra;
  glw_t *c, *n;
  event_t *e;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    gd->delta = 1 / (gd->time * (1000000 / w->glw_root->gr_frameduration));


    if(w->glw_alpha < 0.01)
      break;

    gd->v = GLW_MIN(gd->v + gd->delta, 1.0);
    if(gd->v == 1)
      gd->prev = NULL;

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c == w->glw_selected || c == gd->prev || 
	 c->glw_flags2 & GLW2_ALWAYS_LAYOUT)
	glw_layout0(c, rc);
    }
    break;

  case GLW_SIGNAL_EVENT:
    /* Respond to some events ourselfs */
    e = extra;
    c = w->glw_selected;

    if(c != NULL && event_is_action(e, ACTION_INCR)) {
      n = glw_get_next_n(c, 1);
    } else if(c != NULL && event_is_action(e, ACTION_DECR)) {
      n = glw_get_prev_n(c, 1);
    } else {
      if(w->glw_selected != NULL) {
	if(glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra))
	  return 1;
      }
      n = NULL;
    }

    if(n != c && n != NULL) {

      if(n->glw_originating_prop) {
	// This will bounce back via .gc_select_child
	prop_select(n->glw_originating_prop);
      } else {
	deck_select_child(&gd->w, n, NULL);
      }
      return 1;
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

    if(gd->prev == extra)
      gd->prev = NULL;

    return 0;
  }

  return 0;
}

/**
 *
 */
static void
deck_render(glw_rctx_t *rc, glw_deck_t *gd, glw_t *w, float v)
{
  if(gd->efx_conf != GLW_TRANS_NONE) {
    glw_rctx_t rc0 = *rc;
    if(gd->rev)
      v = 1 - (v + 1);
    glw_transition_render(gd->efx_conf, v, 
			  rc->rc_alpha * gd->w.glw_alpha, &rc0);
    glw_render0(w, &rc0);
  } else {
    glw_render0(w, rc);
  }
}


/**
 *
 */
static void 
glw_deck_render(glw_t *w, glw_rctx_t *rc)
{
  glw_deck_t *gd = (glw_deck_t *)w;

  if(w->glw_alpha < 0.01)
    return;

  if(gd->prev != NULL)
    deck_render(rc, gd, gd->prev, gd->v);

  if(w->glw_selected != NULL)
    deck_render(rc, gd, w->glw_selected, -1 + gd->v);
}


/**
 *
 */
static void
set_page(glw_deck_t *gd, int n)
{
  glw_t *c;
  TAILQ_FOREACH(c, &gd->w.glw_childs, glw_parent_link) {
    if(!n--)
      break;
  }
  deck_select_child(&gd->w, c, NULL);
}



/**
 *
 */
static void
glw_deck_ctor(glw_t *w)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  gd->v = 1.0;
  clear_constraints(w);
}

/**
 *
 */
static void 
glw_deck_set(glw_t *w, va_list ap)
{
  glw_deck_t *gd = (glw_deck_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TRANSITION_EFFECT:
      gd->efx_conf = va_arg(ap, int);
      break;
    case GLW_ATTRIB_TIME:
      gd->time = va_arg(ap, double);
      break;
    case GLW_ATTRIB_PAGE:
      set_page(gd, va_arg(ap, int));
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
  .gc_ctor = glw_deck_ctor,
  .gc_signal_handler = glw_deck_callback,
  .gc_select_child = deck_select_child,
};

GLW_REGISTER_CLASS(glw_deck);
