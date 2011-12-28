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
  struct prop *prop_clone;
  struct prop *prop_parent_override;
  struct prop *args;

  float delta;
  float time;

  glw_transition_type_t efx_conf;
  rstr_t *url;

} glw_view_loader_t;


#define glw_parent_vl_cur glw_parent_val[0].f
#define glw_parent_vl_tgt glw_parent_val[1].f

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
	glw_layout0(c, rc);
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

  case GLW_SIGNAL_DESTROY:
    prop_destroy(a->args);
    if(a->prop_parent_override)
      prop_ref_dec(a->prop_parent_override);
    break;

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
  float blur  = rc->rc_blur  * w->glw_blur;
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    
    rc0 = *rc;
    if(c->glw_parent_vl_cur == 0) {
      rc0.rc_alpha = alpha;
      rc0.rc_blur = blur;
      glw_render0(c, &rc0);
      continue;
    }
    
    glw_transition_render(a->efx_conf, c->glw_parent_vl_cur, alpha, &rc0);
    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
glw_view_loader_retire_child(glw_t *w, glw_t *c)
{
  glw_suspend_subscriptions(c);
  c->glw_parent_vl_tgt = 1;
}


/**
 *
 */
static void 
glw_view_loader_ctor(glw_t *w)
{
  glw_view_loader_t *a = (void *)w;
  a->time = 1.0;
  a->args = prop_create_root("args");
}


/**
 *
 */
static void 
glw_view_loader_dtor(glw_t *w)
{
  glw_view_loader_t *a = (void *)w;
  rstr_release(a->url);
}


/**
 *
 */
static void
set_source(glw_t *w, rstr_t *url)
{
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  glw_t *c;
  
  if(w->glw_flags & GLW_DEBUG)
    TRACE(TRACE_DEBUG, "GLW", "Loader loading %s", 
	  rstr_get(url) ?: "(void)");

  if(!strcmp(rstr_get(url) ?: "", rstr_get(a->url) ?: ""))
    return;

  rstr_release(a->url);
  a->url = rstr_dup(url);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_suspend_subscriptions(c);

  if(url && rstr_get(url)[0]) {
    glw_view_create(w->glw_root, url, w, a->prop, 
		    a->prop_parent_override ?: a->prop_parent, a->args, 
		    a->prop_clone, 1);
  } else {
    /* Fade out all */
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      c->glw_parent_vl_tgt = 1;
  }
}


/**
 *
 */
static void 
glw_view_loader_set(glw_t *w, va_list ap)
{
  glw_view_loader_t *a = (void *)w;

  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_TRANSITION_EFFECT:
      a->efx_conf = va_arg(ap, int);
      break;

    case GLW_ATTRIB_TIME:
      a->time = va_arg(ap, double);
      break;

    case GLW_ATTRIB_PROPROOTS3:
      a->prop = va_arg(ap, void *);
      a->prop_parent = va_arg(ap, void *);
      a->prop_clone = va_arg(ap, void *);
      /* REFcount ?? */
      break;

    case GLW_ATTRIB_ARGS:
      prop_link_ex(va_arg(ap, prop_t *), a->args, NULL,
		   PROP_LINK_XREFED_IF_ORPHANED);
      break;

    case GLW_ATTRIB_PROP_PARENT:
      if(a->prop_parent_override)
	prop_ref_dec(a->prop_parent_override);

      a->prop_parent_override = prop_ref_inc(va_arg(ap, prop_t *));
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
static glw_class_t glw_view_loader = {
  .gc_name = "loader",
  .gc_flags = GLW_EXPEDITE_SUBSCRIPTIONS,
  .gc_instance_size = sizeof(glw_view_loader_t),
  .gc_ctor = glw_view_loader_ctor,
  .gc_dtor = glw_view_loader_dtor,
  .gc_set = glw_view_loader_set,
  .gc_render = glw_view_loader_render,
  .gc_retire_child = glw_view_loader_retire_child,
  .gc_signal_handler = glw_view_loader_callback,
  .gc_set_source = set_source,
};

GLW_REGISTER_CLASS(glw_view_loader);
