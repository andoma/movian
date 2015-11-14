/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <assert.h>

#include "glw.h"
#include "glw_transitions.h"


/**
 *
 */
typedef struct glw_playfield {
  glw_t w;
  char fsmode;

  float speed;

} glw_playfield_t;



typedef struct glw_playfield_item {
  glw_t *detached;
  float amount;
} glw_playfield_item_t;


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

    glw_playfield_item_t *cd = glw_parent_data(s, glw_playfield_item_t);
    assert(cd->detached == w);
    cd->detached = NULL;
  }
  return 0;
}


/**
 *
 */
static void
detach(glw_t *s, glw_t *d)
{
  glw_playfield_item_t *cd = glw_parent_data(s, glw_playfield_item_t);

  if(cd->detached != NULL) {
    glw_t *p = cd->detached;
    p->glw_class->gc_detach_control(p, 0);
    glw_signal_handler_unregister(p, destroy_detached_callback, s);
  }

  cd->detached = d;
  if(d != NULL) {
    d->glw_class->gc_detach_control(d, 1);
    glw_signal_handler_register(d, destroy_detached_callback, s);
  }
}


/**
 *
 */
static int
playfield_select_child(glw_t *w, glw_t *c, prop_t *origin)
{
  glw_playfield_t *p = (glw_playfield_t *)w;
  float speed = 0.5;

  glw_need_refresh(w->glw_root, 0);

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

    glw_playfield_item_t *cd = glw_parent_data(c, glw_playfield_item_t);

    if(w->glw_selected == NULL && w->glw_flags2 & GLW2_NO_INITIAL_TRANS) 
      cd->amount = 1;

    if(cd->detached)
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
  return 1;
}


/**
 *
 */
static void
glw_playfield_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_playfield_t *p = (glw_playfield_t *)w;
  glw_t *c;
  int v = 0;
  float s;

  if(w->glw_alpha < 0.01)
    return;

  TAILQ_FOREACH_REVERSE(c, &w->glw_childs, glw_queue, glw_parent_link) {

    if(w->glw_selected == c)
      v = 1;
    else if(v == 1)
      v = 2;

    s = p->speed;

    glw_playfield_item_t *cd = glw_parent_data(c, glw_playfield_item_t);

    float n = cd->amount;

    if(cd->amount < v) {
      n = GLW_MIN(v, cd->amount + s);
    } else if(cd->amount > v) {
      n = GLW_MAX(v, cd->amount - s);
    }

    if(cd->amount != n) {
      cd->amount = n;
      glw_need_refresh(w->glw_root, 0);
    }

    if(cd->amount > 0 && cd->amount < 2)
      glw_layout0(c, rc);

    if(cd->amount <= 1 && cd->detached)
      detach(c, NULL);
  }
}


/**
 *
 */
static int
glw_playfield_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_playfield_t *p = (glw_playfield_t *)w;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    if(w->glw_selected == extra)
      glw_playfield_update_constraints(p);
    return 1;

  case GLW_SIGNAL_CHILD_DESTROYED:
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
  int zmax = 0;
  glw_t *c, *d;
  glw_rctx_t rc0, rc1;

  if(w->glw_alpha < 0.01)
    return;

  rc0 = *rc;
  rc0.rc_alpha *= w->glw_alpha;
  rc0.rc_zmax = &zmax;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    glw_playfield_item_t *cd = glw_parent_data(c, glw_playfield_item_t);

    rc0.rc_zindex = MAX(zmax, rc->rc_zindex);

    if(cd->amount > 0 && cd->amount < 2) {
      rc1 = rc0;
      rc1.rc_alpha *= 1 - fabs(cd->amount - 1);
      glw_render0(c, &rc1);
    }

    if((d = cd->detached) != NULL) {
      glw_t *dd;
      glw_rctx_t rc2, rc3;
      float a = GLW_MIN(cd->amount, 1);
      if(a > 0) {
	float v = GLW_MAX(cd->amount - 1, 0);
      
	v = GLW_S(v);
      
	glw_LerpMatrix(&rc0.rc_mtx, v, d->glw_matrix, &rc0.rc_mtx);
      
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
    zmax = MAX(zmax, rc0.rc_zindex + 1);
  }
  *rc->rc_zmax = MAX(*rc->rc_zmax, zmax);
}


/**
 *
 */
static glw_class_t glw_playfield = {
  .gc_name = "playfield",
  .gc_instance_size = sizeof(glw_playfield_t),
  .gc_parent_data_size = sizeof(glw_playfield_item_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_layout = glw_playfield_layout,
  .gc_render = glw_playfield_render,
  .gc_ctor = clear_constraints,
  .gc_signal_handler = glw_playfield_callback,
  .gc_select_child = playfield_select_child,
};

GLW_REGISTER_CLASS(glw_playfield);
