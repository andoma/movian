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
#include <unistd.h>

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <alloca.h>

#include "glw.h"
#include "glw_view.h"

#include "fileaccess/fileaccess.h"

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
};


/**
 *
 */
static void
glw_view_error(glw_t *parent, const char *error, const char *file, int line,
               glw_scope_t *scope)
{
  char buf[256];

  if(file != NULL) {
    snprintf(buf, sizeof(buf), "GLW %s:%d: Error: %s", file, line, error);
  } else {
    snprintf(buf, sizeof(buf), "Error: %s", error);
  }

  glw_t *w = glw_create(parent->glw_root, glw_class_find_by_name("label"),
                        parent, NULL, NULL, scope, NULL, 0);
  w->glw_class->gc_set_caption(w, buf, 0);
}


/**
 *
 */
typedef struct glw_cached_view {
  LIST_ENTRY(glw_cached_view) gcv_link;
  token_t *gcv_sof;
  rstr_t *gcv_url;
  rstr_t *gcv_alturl;

  char *gcv_error;
  char *gcv_error_file;
  int gcv_error_line;
  int gcv_refcount;
  int gcv_loaded;
} glw_cached_view_t;





/**
 *
 */
static void
gcv_release(glw_root_t *gr, glw_cached_view_t *gcv)
{
  gcv->gcv_refcount--;
  if(gcv->gcv_refcount > 0)
    return;

  if(gcv->gcv_sof != NULL)
    glw_view_free_chain(gr, gcv->gcv_sof);

  rstr_release(gcv->gcv_url);
  rstr_release(gcv->gcv_alturl);
  free(gcv->gcv_error);
  free(gcv->gcv_error_file);
  free(gcv);
}

/**
 *
 */
static void
eval_loaded_view(glw_root_t *gr, glw_cached_view_t *gcv, glw_view_t *view,
                 glw_scope_t *scope)
{
  if(gcv->gcv_error) {
    glw_view_error(&view->w, gcv->gcv_error,
                   gcv->gcv_error_file, gcv->gcv_error_line, scope);
    return;
  }

  token_t *t = glw_view_clone_chain(gr, gcv->gcv_sof, NULL);

  glw_view_eval_context_t ec = {};
  errorinfo_t ei;

  ec.gr = gr;
  ec.rc = NULL;
  ec.w = &view->w;
  ec.ei = &ei;

  view->viewprop =  prop_create_root(NULL);
  ec.scope = glw_scope_dup(scope, (1 << GLW_ROOT_VIEW));
  ec.scope->gs_roots[GLW_ROOT_VIEW].p = prop_ref_inc(view->viewprop);

  ec.sublist = &ec.w->glw_prop_subscriptions;

  if(glw_view_eval_block(t, &ec, NULL)) {
    glw_destroy_childs(ec.w);
    glw_view_error(ec.w, ei.error, ei.file, ei.line, scope);
  }
  glw_view_free_chain(gr, t);

  if(unlikely(gr->gr_pending_focus != NULL))
    glw_focus_check_pending(ec.w->glw_parent);

  glw_scope_release(ec.scope);
}


/**
 *
 */
static void
gcv_load(glw_root_t *gr, glw_cached_view_t *gcv, int may_unlock)
{
  char errbuf[512];
  buf_t *buf;
  errorinfo_t ei;

  if(may_unlock)
    glw_unlock(gr);

  rstr_t *file = gcv->gcv_url;
  buf = fa_load(rstr_get(gcv->gcv_url),
              FA_LOAD_RESOLVER(gr->gr_fa_resolver),
              FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
              NULL);

  if(buf == NULL && gcv->gcv_alturl != NULL) {
    file = gcv->gcv_alturl;
    buf = fa_load(rstr_get(gcv->gcv_alturl),
                  FA_LOAD_RESOLVER(gr->gr_fa_resolver),
                  FA_LOAD_ERRBUF(errbuf, sizeof(errbuf)),
                  NULL);
  }

  if(may_unlock)
    glw_lock(gr);


  if(buf == NULL) {
    char errmsg[1024];
    snprintf(errmsg, sizeof(errmsg), "Unable to open \"%s\" -- %s",
             rstr_get(file), errbuf);
    gcv->gcv_error = strdup(errmsg);
    return;
  }

  token_t *sof = glw_view_token_alloc(gr);
  sof->type = TOKEN_START;
  sof->file = rstr_dup(file);

  token_t *l = glw_view_lexer(gr, buf_cstr(buf), &ei, file, sof);
  buf_release(buf);
  if(l == NULL) {
    glw_view_free_chain(gr, sof);
    goto bad;
  }

  token_t *eof = glw_view_token_alloc(gr);
  eof->type = TOKEN_END;
  eof->file = rstr_dup(file);
  l->next = eof;

  if(glw_view_preproc(gr, sof, &ei, may_unlock) ||
     glw_view_parse(sof, &ei, gr)) {
    glw_view_free_chain(gr, sof);
    goto bad;
  }

  gcv->gcv_sof = sof;
  gcv->gcv_loaded = 1;
  return;

 bad:
  gcv->gcv_loaded = 1; // A view is also "loaded" when there is an error
  gcv->gcv_error = strdup(ei.error);
  gcv->gcv_error_file = strdup(ei.file);
  gcv->gcv_error_line = ei.line;
}


/**
 *
 */
typedef struct glw_view_load_request {
  TAILQ_ENTRY(glw_view_load_request) link;
  rstr_t *url;
  rstr_t *alturl;
  glw_t *w;
  glw_scope_t *scope;
  glw_cached_view_t *gcv;
} glw_view_load_request_t;




/**
 *
 */
static void
gvlr_destroy(glw_root_t *gr, glw_view_load_request_t *r)
{
  rstr_release(r->url);
  rstr_release(r->alturl);
  glw_unref(r->w);
  glw_scope_release(r->scope);
  gcv_release(gr, r->gcv);
  free(r);
}


/**
 *
 */
static void *
viewloader_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_cached_view_t *gcv;

  glw_lock(gr);

  while(gr->gr_view_loader_run) {
    glw_view_load_request_t *r = TAILQ_FIRST(&gr->gr_view_load_requests);
    if(r == NULL) {
      hts_cond_wait(&gr->gr_view_loader_cond, &gr->gr_mutex);
      continue;
    }

    gcv = r->gcv;

    if(!gcv->gcv_loaded)
      gcv_load(gr, gcv, 1);

    TAILQ_REMOVE(&gr->gr_view_load_requests, r, link);
    TAILQ_INSERT_TAIL(&gr->gr_view_eval_requests, r, link);
  }

  glw_unlock(gr);
  return NULL;
}


/**
 *
 */
void
glw_view_loader_eval(glw_root_t *gr)
{
  glw_view_load_request_t *r;

  while((r = TAILQ_FIRST(&gr->gr_view_eval_requests)) != NULL) {
    TAILQ_REMOVE(&gr->gr_view_eval_requests, r, link);

    if(!(r->w->glw_flags & GLW_DESTROYING))
      eval_loaded_view(gr, r->gcv, (glw_view_t *)r->w, r->scope);
    gvlr_destroy(gr, r);
  }
}


/**
 *
 */
void
glw_view_loader_flush(glw_root_t *gr)
{
  glw_view_load_request_t *r;

  while((r = TAILQ_FIRST(&gr->gr_view_eval_requests)) != NULL) {
    TAILQ_REMOVE(&gr->gr_view_eval_requests, r, link);
    gvlr_destroy(gr, r);
  }

  while((r = TAILQ_FIRST(&gr->gr_view_load_requests)) != NULL) {
    TAILQ_REMOVE(&gr->gr_view_load_requests, r, link);
    gvlr_destroy(gr, r);
  }
}


/**
 *
 */
glw_t *
glw_view_create(glw_root_t *gr, rstr_t *url, rstr_t *alturl, glw_t *parent,
                glw_scope_t *scope, rstr_t *file, int line)
{
  glw_cached_view_t *gcv;

  if(url == NULL) {
    assert(alturl != NULL);
    url = alturl;
    alturl = NULL;
  }

  glw_t *w = glw_create(gr, &glw_view, parent, NULL, NULL, scope, file, line);

  LIST_FOREACH(gcv, &gr->gr_views, gcv_link) {
    if(rstr_eq(gcv->gcv_url, url) && rstr_eq(gcv->gcv_alturl, alturl))
      break;
  }

  if(gcv == NULL) {

    gcv = calloc(1, sizeof(glw_cached_view_t));
    gcv->gcv_refcount = 1;
    gcv->gcv_url      = rstr_dup(url);
    gcv->gcv_alturl   = rstr_dup(alturl);
    LIST_INSERT_HEAD(&gr->gr_views, gcv, gcv_link);
  }

  if(!gcv->gcv_loaded) {

#ifdef __native_client__
    const int async_load = 1;
#else
    const int async_load = 0;
#endif
    if(async_load) {
      // Async load
      glw_view_load_request_t *r = calloc(1, sizeof(glw_view_load_request_t));
      w->glw_refcnt++;
      gcv->gcv_refcount++;

      r->url    = rstr_dup(url);
      r->alturl = rstr_dup(alturl);
      r->w      = w;
      r->scope  = glw_scope_retain(scope);
      r->gcv    = gcv;

      TAILQ_INSERT_TAIL(&gr->gr_view_load_requests, r, link);

      if(!gr->gr_view_loader_run) {
        gr->gr_view_loader_run = 1;
        hts_thread_create_joinable("viewloader",
                                   &gr->gr_view_loader_thread,
                                   viewloader_thread,
                                   gr, THREAD_PRIO_UI_WORKER_MED);
      } else {
        hts_cond_signal(&gr->gr_view_loader_cond);
      }

      return w;
    }

    gcv_load(gr, gcv, 0);

  } else {
    LIST_REMOVE(gcv, gcv_link);
    LIST_INSERT_HEAD(&gr->gr_views, gcv, gcv_link);
  }

  eval_loaded_view(gr, gcv, (glw_view_t *)w, scope);
  return w;
}


/**
 *
 */
void
glw_view_cache_flush(glw_root_t *gr)
{
  glw_cached_view_t *gcv;

  while((gcv = LIST_FIRST(&gr->gr_views)) != NULL) {
    LIST_REMOVE(gcv, gcv_link);
    gcv_release(gr, gcv);
  }
}
