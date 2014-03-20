/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <unistd.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>

#include "glw.h"
#include "glw_view.h"

typedef struct glw_view {
  glw_t w;
  prop_t *viewprop;

} glw_view_t;


/**
 *
 */
static void
glw_view_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c != NULL)
    return glw_layout0(c, rc);
}


/**
 *
 */
static int
glw_view_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  switch(signal) {

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
glw_view_dtor(glw_t *w)
{
  glw_view_t *v = (glw_view_t *)w;
  prop_destroy(v->viewprop);
}


/**
 *
 */
static void
glw_view_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  if(c != NULL)
    glw_render0(c, rc);
}


/**
 *
 */
static glw_class_t glw_view = {
  .gc_name = "view",
  .gc_instance_size = sizeof(glw_view_t),
  .gc_layout = glw_view_layout,
  .gc_render = glw_view_render,
  .gc_dtor = glw_view_dtor,
  .gc_signal_handler = glw_view_callback,
  .gc_send_event = glw_event_distribute_to_childs,
};


/**
 *
 */
static glw_t *
glw_view_error(glw_root_t *gr, errorinfo_t *ei, glw_t *parent)
{
  char buf[256];
  glw_t *w;

  snprintf(buf, sizeof(buf), "GLW %s:%d", ei->file, ei->line);

  TRACE(TRACE_ERROR, buf, "%s", ei->error);

  snprintf(buf, sizeof(buf), "GLW %s:%d: Error: %s",
	   ei->file, ei->line, ei->error);

  w = glw_create(gr, glw_class_find_by_name("label"), parent, NULL, NULL);
  w->glw_class->gc_set_caption(w, buf, 0);
  return w;
}


/**
 *
 */
typedef struct glw_cached_view {
  LIST_ENTRY(glw_cached_view) gcv_link;
  token_t *gcv_sof;
  rstr_t *gcv_url;
} glw_cached_view_t;

/**
 *
 */
glw_t *
glw_view_create(glw_root_t *gr, rstr_t *url,
		glw_t *parent, prop_t *prop, prop_t *prop_parent, prop_t *args,
		prop_t *prop_clone, int cache, int nofail)
{
  token_t *eof, *l, *t;
  errorinfo_t ei;
  glw_t *r;
  glw_view_eval_context_t ec;
  glw_cached_view_t *gcv;
  glw_view_t *v;

  LIST_FOREACH(gcv, &gr->gr_views, gcv_link) {
    if(!strcmp(rstr_get(gcv->gcv_url), rstr_get(url)))
      break;
  }

  if(gcv == NULL) {
    int nofile = 0;
    token_t *sof = glw_view_token_alloc(gr);
    sof->type = TOKEN_START;
    sof->file = rstr_dup(url);
    if((l = glw_view_load1(gr, url, &ei, sof, &nofile)) == NULL) {
      glw_view_free_chain(gr, sof);
      if(nofile && !nofail)
	return NULL;
      return glw_view_error(gr, &ei, parent);
    }
    eof = glw_view_token_alloc(gr);
    eof->type = TOKEN_END;
    eof->file = rstr_dup(url);
    l->next = eof;
  
    if(glw_view_preproc(gr, sof, &ei) || glw_view_parse(sof, &ei, gr)) {
      glw_view_free_chain(gr, sof);
      return glw_view_error(gr, &ei, parent);
    }

    if(cache) {
      gcv = malloc(sizeof(glw_cached_view_t));
      gcv->gcv_sof = sof;
      gcv->gcv_url = rstr_dup(url);
      LIST_INSERT_HEAD(&gr->gr_views, gcv, gcv_link);
      t = glw_view_clone_chain(gr, gcv->gcv_sof, NULL);
    } else {
      t = sof;
    }
  } else {
    t = glw_view_clone_chain(gr, gcv->gcv_sof, NULL);
  }


  memset(&ec, 0, sizeof(ec));

  r = glw_create(gr, &glw_view, parent, NULL, NULL);
  //  glw_set(r, GLW_ATTRIB_CAPTION, src, 0, NULL);

  v = (glw_view_t *)r;
  ec.gr = gr;
  ec.rc = NULL;
  ec.w = r;
  ec.ei = &ei;
  ec.prop = prop;
  ec.prop_parent = prop_parent;
  ec.prop_args    = args;
  ec.prop_clone = prop_clone;
  v->viewprop = ec.prop_viewx = prop_create_root("view");
  ec.sublist = &ec.w->glw_prop_subscriptions;

  if(glw_view_eval_block(t, &ec)) {
    glw_destroy(ec.w);
    glw_view_free_chain(gr, t);
    return glw_view_error(gr, &ei, parent);
  }
  glw_view_free_chain(gr, t);
  return r;
}


/**
 *
 */
void
glw_view_cache_flush(glw_root_t *gr)
{
  glw_cached_view_t *gcv;

  while((gcv = LIST_FIRST(&gr->gr_views)) != NULL) {
    glw_view_free_chain(gr, gcv->gcv_sof);
    rstr_release(gcv->gcv_url);
    LIST_REMOVE(gcv, gcv_link);
    free(gcv);
  }
}
