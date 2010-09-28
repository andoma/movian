/*
 *  GL Widgets, playfield, transition between childs objects
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
  glw_t *prev; // Widget we are transitioning from

  glw_transition_type_t efx_conf;
  float time;
  float delta;
  
  float v;
  char rev;

  char fsmode;

} glw_playfield_t;




/**
 *
 */
static void
clear_constraints(glw_t *w)
{
  glw_set_constraints(w, 0, 0, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  glw_signal0(w, GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED, NULL);
}



/**
 *
 */
static void
glw_playfield_update_constraints(glw_playfield_t *p)
{
  glw_t *c = p->w.glw_selected;

  int was_fullscreen = !!(p->w.glw_flags & GLW_CONSTRAINT_F);

  glw_copy_constraints(&p->w, c);

  p->fsmode = !!(p->w.glw_flags & GLW_CONSTRAINT_F);
    
  if(p->fsmode == was_fullscreen)
    return;

  glw_signal0(&p->w, GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED, NULL);
}


/**
 *
 */
static void
setprev(glw_playfield_t *gd, glw_t *c)
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
playfield_select(glw_playfield_t *gd, glw_t *c)
{
  setprev(gd, c);
  gd->w.glw_selected = c;
  if(gd->w.glw_selected != NULL) {
    glw_focus_open_path_close_all_other(gd->w.glw_selected);
    glw_playfield_update_constraints(gd);
  } else {
    clear_constraints(&gd->w);
  }

  if(gd->efx_conf != GLW_TRANS_NONE &&
     (gd->prev != NULL || !(gd->w.glw_flags & GLW_NO_INITIAL_TRANS)))
    gd->v = 0;
}


/**
 *
 */
static int
glw_playfield_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_playfield_t *p = (glw_playfield_t *)w;
  glw_rctx_t *rc = extra;
  glw_t *c, *n;
  event_t *e;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    p->delta = 1 / (p->time * (1000000 / w->glw_root->gr_frameduration));

    if(w->glw_alpha < 0.01)
      break;

    p->v = GLW_MIN(p->v + p->delta, 1.0);
    if(p->v == 1)
      p->prev = NULL;

    if(w->glw_selected != NULL)
      glw_layout0(w->glw_selected, rc);
    if(p->prev != NULL)
      glw_layout0(p->prev, rc);
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
    playfield_select(p, extra);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_selected == extra)
      glw_playfield_update_constraints(p);
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = extra;
    if(w->glw_selected == extra)
      clear_constraints(w);

    if(p->prev == extra)
      p->prev = NULL;

    return 0;
  }

  return 0;
}

/**
 *
 */
static void
playfield_render(glw_rctx_t *rc, glw_playfield_t *gd, glw_t *w, float v)
{
  if(gd->efx_conf != GLW_TRANS_NONE) {
    glw_rctx_t rc0 = *rc;
    if(gd->rev)
      v = 1 - (v + 1);
    glw_transition_render(gd->efx_conf, v, 
			  rc->rc_alpha * gd->w.glw_alpha, &rc0);
    w->glw_class->gc_render(w, &rc0);
  } else {
    w->glw_class->gc_render(w, rc);
  }
}


/**
 *
 */
static void 
glw_playfield_render(glw_t *w, glw_rctx_t *rc)
{
  glw_playfield_t *gd = (glw_playfield_t *)w;

  if(w->glw_alpha < 0.01)
    return;

  if(gd->prev != NULL)
    playfield_render(rc, gd, gd->prev, gd->v);

  if(w->glw_selected != NULL)
    playfield_render(rc, gd, w->glw_selected, -1 + gd->v);
}


/**
 *
 */
static void
set_page(glw_playfield_t *gd, int n)
{
  glw_t *c;
  TAILQ_FOREACH(c, &gd->w.glw_childs, glw_parent_link) {
    if(!n--)
      break;
  }
  playfield_select(gd, c);
}




/**
 *
 */
static void 
glw_playfield_set(glw_t *w, int init, va_list ap)
{
  glw_playfield_t *gd = (glw_playfield_t *)w;
  glw_attribute_t attrib;

  if(init) {
    gd->v = 1.0;
    clear_constraints(w);
  }

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
static glw_class_t glw_playfield = {
  .gc_name = "playfield",
  .gc_instance_size = sizeof(glw_playfield_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_nav_descend_mode = GLW_NAV_DESCEND_SELECTED,
  .gc_render = glw_playfield_render,
  .gc_set = glw_playfield_set,
  .gc_signal_handler = glw_playfield_callback,
};

GLW_REGISTER_CLASS(glw_playfield);
