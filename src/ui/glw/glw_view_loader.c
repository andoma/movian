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
#include "glw.h"
#include "glw_transitions.h"


typedef struct glw_view_loader {
  glw_t w;

  glw_scope_t *scope;

  rstr_t *url;
  rstr_t *alt_url;

  float delta;
  float time;

  glw_transition_type_t efx_conf;
  char loaded;

} glw_view_loader_t;


typedef struct glw_loader_item {
  float vl_cur;
  float vl_tgt;
} glw_loader_item_t;

#define itemdata(w) glw_parent_data(w, glw_loader_item_t)


/**
 *
 */
static void
glw_loader_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_view_loader_t *a = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_t *c, *n;

  a->delta = 1 / (a->time * (1000000 / w->glw_root->gr_frameduration));

  for(c = TAILQ_FIRST(&w->glw_childs); c != NULL; c = n) {
    n = TAILQ_NEXT(c, glw_parent_link);

    float n =
      GLW_MIN(itemdata(c)->vl_cur + a->delta, itemdata(c)->vl_tgt);

    if(n != itemdata(c)->vl_cur)
      glw_need_refresh(gr, 0);

    itemdata(c)->vl_cur = n;

    if(itemdata(c)->vl_cur == 1) {
      glw_destroy(c);

      if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
        glw_copy_constraints(w, c);
      }
    } else {
      glw_layout0(c, rc);
    }
  }
}


/**
 *
 */
static int
glw_loader_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c, *n;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CREATED:
    c = extra;

    if(TAILQ_FIRST(&w->glw_childs) == c &&
       TAILQ_NEXT(c, glw_parent_link) == NULL &&
       w->glw_flags2 & GLW2_NO_INITIAL_TRANS) {
      itemdata(c)->vl_cur = 0;
    } else {
      itemdata(c)->vl_cur = -1;
    }

    itemdata(c)->vl_tgt = 0;

    glw_focus_open_path_close_all_other(c);

    TAILQ_FOREACH(n, &w->glw_childs, glw_parent_link) {
      if(c == n)
	continue;
      itemdata(n)->vl_tgt = 1;
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
glw_view_loader_render(glw_t *w, const glw_rctx_t *rc)
{
  float alpha = rc->rc_alpha * w->glw_alpha;
  float sharpness  = rc->rc_sharpness  * w->glw_sharpness;
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  glw_t *c;
  glw_rctx_t rc0;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    rc0 = *rc;
    if(itemdata(c)->vl_cur == 0) {
      rc0.rc_alpha = alpha;
      rc0.rc_sharpness = sharpness;
      glw_render0(c, &rc0);
      continue;
    }

    glw_transition_render(a->efx_conf, itemdata(c)->vl_cur, alpha, &rc0);
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
  itemdata(c)->vl_tgt = 1;
}


/**
 *
 */
static void
glw_view_loader_ctor(glw_t *w)
{
  glw_view_loader_t *vl = (glw_view_loader_t *)w;
  vl->time = 0.00001;
  vl->scope = glw_scope_retain(w->glw_scope);
  w->glw_flags2 |= GLW2_EXPEDITE_SUBSCRIPTIONS;
}


/**
 *
 */
static void
glw_view_loader_dtor(glw_t *w)
{
  glw_view_loader_t *vl = (void *)w;
  glw_scope_release(vl->scope);
  rstr_release(vl->url);
  rstr_release(vl->alt_url);
}


/**
 *
 */
static void
update_autohide(glw_view_loader_t *l)
{
  if(!(l->w.glw_flags2 & GLW2_AUTOHIDE))
    return;

  if(l->loaded) {
    glw_unhide(&l->w);
  } else {
    glw_hide(&l->w);

    glw_t *c;

    while((c = TAILQ_FIRST(&l->w.glw_childs)) != NULL)
      glw_destroy(c);
  }
}


/**
 *
 */
static void
set_source(glw_t *w, rstr_t *url, glw_style_t *origin)
{
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  glw_t *c;

  if(w->glw_flags2 & GLW2_DEBUG)
    TRACE(TRACE_DEBUG, "GLW", "Loader loading %s",
	  rstr_get(url) ?: "(void)");

  if(!strcmp(rstr_get(url) ?: "", rstr_get(a->url) ?: ""))
    return;

  rstr_release(a->url);
  a->url = rstr_dup(url);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_suspend_subscriptions(c);

  if(url != NULL && rstr_get(url)[0] == 0)
    url = NULL;

  rstr_t *alt_url = a->alt_url;

  if(alt_url != NULL && rstr_get(alt_url)[0] == 0)
    alt_url = NULL;

  if(url || alt_url) {
    glw_view_create(w->glw_root, url, alt_url, w, a->scope, NULL, 0);
    a->loaded = 1;
    update_autohide(a);
    return;
  }

  a->loaded = 0;
  update_autohide(a);

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    itemdata(c)->vl_tgt = 1;
}


/**
 *
 */
static void
set_alt(glw_t *w, rstr_t *url)
{
  glw_view_loader_t *a = (glw_view_loader_t *)w;
  rstr_set(&a->alt_url, url);
}


/**
 *
 */
static int
glw_view_loader_set_int(glw_t *w, glw_attribute_t attrib, int value,
                        glw_style_t *origin)
{
  glw_view_loader_t *vl = (void *)w;

  switch(attrib) {
  case GLW_ATTRIB_TRANSITION_EFFECT:
    if(vl->efx_conf == value)
      return 0;
    vl->efx_conf = value;
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_view_loader_set_float(glw_t *w, glw_attribute_t attrib, float value,
                          glw_style_t *origin)
{
  glw_view_loader_t *vl = (void *)w;

  switch(attrib) {
  case GLW_ATTRIB_TIME:
    value = GLW_MAX(value, 0.00001);
    if(vl->time == value)
      return 0;
    vl->time = value;
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_view_loader_set_prop(glw_t *w, glw_attribute_t attrib, prop_t *p)
{
  glw_view_loader_t *vl = (void *)w;
  glw_scope_t *scope;

  switch(attrib) {
  case GLW_ATTRIB_ARGS:
    scope = glw_scope_dup(vl->scope, (1 << GLW_ROOT_ARGS));
    scope->gs_roots[GLW_ROOT_ARGS].p = prop_ref_inc(p);
    glw_scope_release(vl->scope);
    vl->scope = scope;
    return 0;

  case GLW_ATTRIB_PROP_SELF:
    scope = glw_scope_dup(vl->scope, (1 << GLW_ROOT_SELF));
    scope->gs_roots[GLW_ROOT_SELF].p = prop_ref_inc(p);
    glw_scope_release(vl->scope);
    vl->scope = scope;
    return 0;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static const char *
get_identity(glw_t *w, char *tmp, size_t tmpsize)
{
  glw_view_loader_t *l = (glw_view_loader_t *)w;
  return rstr_get(l->url) ?: "NULL";
}



/**
 *
 */
static void
mod_flags2(glw_t *w, int set, int clr)
{
  glw_view_loader_t *gvl = (glw_view_loader_t *)w;

  if(set & GLW2_AUTOHIDE && !gvl->loaded)
    glw_hide(w);

  if(clr & GLW2_AUTOHIDE)
    glw_unhide(w);
}


/**
 *
 */
static glw_class_t glw_view_loader = {
  .gc_name = "loader",
  .gc_instance_size = sizeof(glw_view_loader_t),
  .gc_parent_data_size = sizeof(glw_loader_item_t),
  .gc_ctor = glw_view_loader_ctor,
  .gc_dtor = glw_view_loader_dtor,
  .gc_set_int = glw_view_loader_set_int,
  .gc_set_float = glw_view_loader_set_float,
  .gc_set_prop = glw_view_loader_set_prop,
  .gc_layout = glw_loader_layout,
  .gc_render = glw_view_loader_render,
  .gc_retire_child = glw_view_loader_retire_child,
  .gc_signal_handler = glw_loader_callback,
  .gc_set_source = set_source,
  .gc_get_identity = get_identity,
  .gc_set_alt = set_alt,
  .gc_mod_flags2 = mod_flags2,
};

GLW_REGISTER_CLASS(glw_view_loader);
