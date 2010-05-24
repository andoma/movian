/*
 *  GL Widgets, View loader
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


typedef struct glw_view_loader {
  glw_t w;
  
  struct prop *prop;
  struct prop *prop_parent;

  float delta;
  float time;

  glw_transition_type_t efx_conf;

} glw_view_loader_t;


#define glw_parent_vl_cur glw_parent_misc[0]
#define glw_parent_vl_tgt glw_parent_misc[1]

/**
 *
 */
static int
glw_view_loader_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c, *n;
  glw_view_loader_t *a = (void *)w;
  glw_rctx_t *rc = extra;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    a->delta = 1 / (a->time * (1000000 / w->glw_root->gr_frameduration));

    for(c = TAILQ_FIRST(&w->glw_childs); c != NULL; c = n) {
      n = TAILQ_NEXT(c, glw_parent_link);

      c->glw_parent_vl_cur = 
	GLW_MIN(c->glw_parent_vl_cur + a->delta, c->glw_parent_vl_tgt);
      
      if(c->glw_parent_vl_cur == 1) {
	glw_destroy(c);

	if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
	  glw_copy_constraints(w, c);
	}
      } else {
	glw_rctx_t rc0 = *rc;
	rc0.rc_final = c->glw_parent_vl_cur > 0;

	glw_layout0(c, &rc0);
      }
    }
    return 0;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    if(TAILQ_FIRST(&w->glw_childs) == c &&
       TAILQ_NEXT(c, glw_parent_link) == NULL &&
       w->glw_flags & GLW_NO_INITIAL_TRANS) {
      c->glw_parent_vl_cur = 0;
    } else {
      c->glw_parent_vl_cur = -1;
    }

    c->glw_parent_vl_tgt = 0;
    
    glw_focus_open_path_close_all_other(c);

    TAILQ_FOREACH(n, &w->glw_childs, glw_parent_link) {
      if(c == n)
	continue;
      n->glw_parent_vl_tgt = 1;
    }

    if(c == TAILQ_FIRST(&w->glw_childs)) {
      glw_copy_constraints(w, c);
    }

    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    c = extra;
    if(c == TAILQ_FIRST(&w->glw_childs))
      glw_copy_constraints(w, c);
    return 1;
  }
  return 0;
}


/**
 *
 */
static void
glw_view_loader_render(glw_t *w, glw_rctx_t *rc)
{
  float alpha = rc->rc_alpha * w->glw_alpha;
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    
    rc0 = *rc;
    if(c->glw_parent_vl_cur == 0) {
      rc0.rc_alpha = alpha;
      glw_render0(c, &rc0);
      continue;
    }
    
    glw_PushMatrix(&rc0, rc);
    glw_transition_render(a->efx_conf, c->glw_parent_vl_cur, alpha, &rc0);
    glw_render0(c, &rc0);
    glw_PopMatrix();
  }
}


/**
 *
 */
static void
glw_view_loader_detach(glw_t *w, glw_t *c)
{
  glw_destroy_subscriptions(c);
  c->glw_parent_vl_tgt = 1;
}


/**
 *
 */
static void 
glw_view_loader_set(glw_t *w, int init, va_list ap)
{
  glw_view_loader_t *a = (void *)w;

  glw_attribute_t attrib;
  const char *filename = NULL;
  glw_t *c;

  if(init)
    a->time = 1.0;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TRANSITION_EFFECT:
      a->efx_conf = va_arg(ap, int);
      break;

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_TIME:
      a->time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_PROPROOTS:
      a->prop = va_arg(ap, void *);
      a->prop_parent = va_arg(ap, void *);
      /* REFcount ?? */
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL) {
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      glw_destroy_subscriptions(c);
    if(*filename) {
      glw_view_create(w->glw_root, filename, w,
		      a->prop, a->prop_parent, 1);
    } else {
      /* Fade out all */
      TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
	c->glw_parent_vl_tgt = 1;
    }
  }
}


/**
 *
 */
static glw_class_t glw_view_loader = {
  .gc_name = "loader",
  .gc_flags = GLW_EXPEDITE_SUBSCRIPTIONS,
  .gc_instance_size = sizeof(glw_view_loader_t),
  .gc_set = glw_view_loader_set,
  .gc_render = glw_view_loader_render,
  .gc_detach = glw_view_loader_detach,
  .gc_signal_handler = glw_view_loader_callback,
};

GLW_REGISTER_CLASS(glw_view_loader);
