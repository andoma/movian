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

#include <assert.h>

#include "glw.h"
#include "glw_transitions.h"


/**
 *
 */
typedef struct {
  glw_t w;
  char fsmode;

  float speed;

} glw_playfield_t;

#define glw_parent_detached    glw_parent_val[0].ptr
#define glw_parent_amount      glw_parent_val[1].f

/**
 *
 */
static void
clear_constraints(glw_t *w)
{
  glw_set_constraints(w, 0, 0, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

  glw_signal0(w, GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED, NULL);
}



/**
 *
 */
static void
glw_playfield_update_constraints(glw_playfield_t *p)
{
  glw_t *c = p->w.glw_selected;
  glw_copy_constraints(&p->w, c);
}



/**
 *
 */
static glw_t *
find_by_prop(glw_t *w, prop_t *p)
{
  glw_t *c, *r;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_originating_prop != NULL &&
       prop_compare(c->glw_originating_prop, p))
      return c;
    if((r = find_by_prop(c, p)) != NULL)
      return r;
  }
  return NULL;
}


/**
 *
 */
static glw_t *
find_detachable(glw_t *w)
{
  glw_t *c, *r;
  if(w->glw_class->gc_detach_control != NULL)
    return w;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if((r = find_detachable(c)) != NULL)
      return r;
  }
  return NULL;
}


/**
 *
 */
static int
destroy_detached_callback(glw_t *w, void *opaque,
			  glw_signal_t signal, void *extra)
{
  glw_t *s = opaque;
  if(signal == GLW_SIGNAL_DESTROY) {
    assert(s->glw_parent_detached == w);
    s->glw_parent_detached = NULL;
  }
  return 0;
}


/**
 *
 */
static void
detach(glw_t *s, glw_t *d)
{
  if(s->glw_parent_detached != NULL) {
    glw_t *p = s->glw_parent_detached;
    p->glw_class->gc_detach_control(p, 0);
    glw_signal_handler_unregister(p, destroy_detached_callback, s);
  }

  s->glw_parent_detached = d;
  if(d != NULL) {
    d->glw_class->gc_detach_control(d, 1);
    glw_signal_handler_register(d, destroy_detached_callback, s, 1000);
  }
}


/**
 *
 */
static void
playfield_select_child(glw_t *w, glw_t *c, prop_t *origin)
{
  glw_playfield_t *p = (glw_playfield_t *)w;
  float speed = 0.1;

  if(origin && w->glw_selected != NULL &&
     TAILQ_NEXT(w->glw_selected, glw_parent_link) == c) {
    glw_t *x = find_by_prop(w->glw_selected, origin);
    glw_t *d = x ? find_detachable(x) : NULL;
    if(d != NULL) {
      detach(w->glw_selected, d);
      speed = 0.025;
    }
  }
  
  if(c != NULL) {
    if(w->glw_selected == NULL && w->glw_flags2 & GLW2_NO_INITIAL_TRANS) 
      c->glw_parent_amount = 1;

    if(c->glw_parent_detached)
      speed = 0.025;
  }

  p->speed = speed;
  w->glw_selected = c;

  if(c != NULL) {
    glw_focus_open_path_close_all_other(c);
    glw_playfield_update_constraints(p);
  } else {
    clear_constraints(w);
  }

}


/**
 *
 */
static int
glw_playfield_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_playfield_t *p = (glw_playfield_t *)w;
  glw_rctx_t *rc = extra;
  glw_t *c;
  int v = 0;
  float s;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    if(w->glw_alpha < 0.01)
      break;

    TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {

      if(w->glw_selected == c) 
	v = 1;
      else if(v == 1)
	v = 2;

      s = p->speed;

      if(c->glw_parent_amount < v) {
	c->glw_parent_amount = GLW_MIN(v, c->glw_parent_amount + s);
      } else if(c->glw_parent_amount > v) {
	c->glw_parent_amount = GLW_MAX(v, c->glw_parent_amount - s);
      }

      if((c->glw_parent_amount > 0 && c->glw_parent_amount < 2) ||
	 !w->glw_root->gr_reduce_cpu)
	glw_layout0(c, rc);

      if(c->glw_parent_amount <= 1 && c->glw_parent_detached)
	detach(c, NULL);

    }
    break;

  case GLW_SIGNAL_EVENT:
    if(w->glw_selected != NULL)
      return glw_signal0(w->glw_selected, GLW_SIGNAL_EVENT, extra);
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_selected == extra)
      glw_playfield_update_constraints(p);
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
    c = extra;
    if(w->glw_selected == extra)
      clear_constraints(w);
    break;
  }

  return 0;
}


/**
 *
 */
static void 
glw_playfield_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c, *d;
  glw_rctx_t rc0, rc1;

  if(w->glw_alpha < 0.01)
    return;

  rc0 = *rc;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_parent_amount > 0 && c->glw_parent_amount < 2) {
      rc1 = rc0;
      rc1.rc_alpha *= 1 - fabs(c->glw_parent_amount - 1);
      glw_render0(c, &rc1);
    }

    if((d = c->glw_parent_detached) != NULL) {
      glw_t *dd;
      glw_rctx_t rc2, rc3;
      float a = GLW_MIN(c->glw_parent_amount, 1);
      if(a > 0) {
	float v = GLW_MAX(c->glw_parent_amount - 1, 0);
      
	v = GLW_S(v);
      
	glw_LerpMatrix(rc0.rc_mtx, v, *d->glw_matrix, rc0.rc_mtx);
      
	if((dd = TAILQ_FIRST(&d->glw_childs)) != NULL) {

	  glw_Rotatef(&rc0, v * 180, 1, 0, 0);
	  rc3 = rc0;
	  d->glw_class->gc_get_rctx(d, &rc2);
	  rc3.rc_width = rc2.rc_width;
	  rc3.rc_height = rc2.rc_height;
	  rc3.rc_alpha *= a;
	  glw_render0(dd, &rc3);
	  glw_Rotatef(&rc0, 180, 1, 0, 0);
	}
      }
    }
  }
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
  .gc_ctor = clear_constraints,
  .gc_signal_handler = glw_playfield_callback,
  .gc_select_child = playfield_select_child,
};

GLW_REGISTER_CLASS(glw_playfield);
