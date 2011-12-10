/*
 *  GL Widgets, Texture loader
 *  Copyright (C) 2007 Andreas Öman
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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "glw.h"
#include "glw_texture.h"

#include "backend/backend.h"

void
glw_tex_autoflush(glw_root_t *gr)
{
  glw_loadable_texture_t *glt;

  while((glt = LIST_FIRST(&gr->gr_tex_flush_list)) != NULL) {
    assert(glt->glt_filename != NULL);
    assert(glt->glt_state != GLT_STATE_LOADING);
    assert(glt->glt_state != GLT_STATE_ERROR);

    LIST_REMOVE(glt, glt_flush_link);
    glw_tex_backend_free_render_resources(gr, glt);

    if(glt->glt_state == GLT_STATE_QUEUED)
      TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);

    glt->glt_state = GLT_STATE_INACTIVE;
  }

  LIST_MOVE(&gr->gr_tex_flush_list, &gr->gr_tex_active_list, glt_flush_link);
  LIST_INIT(&gr->gr_tex_active_list);
}


/**
 *
 */
typedef struct loaderaux {
  glw_root_t *la_gr;
  int la_only_fast;
} loaderaux_t;


/**
 *
 */
static glw_loadable_texture_t *
loader_get_work(loaderaux_t *la)
{
  glw_root_t *gr = la->la_gr;
  int i;
  glw_loadable_texture_t *glt;
  int last_queue = la->la_only_fast ? LQ_TENTATIVE : LQ_REFRESH;
  
  while(1) {
    for(i = 0; i <= last_queue; i++)
      if((glt = TAILQ_FIRST(&gr->gr_tex_load_queue[i])) != NULL)
	return glt;

    hts_cond_wait(&gr->gr_tex_load_cond, &gr->gr_mutex);
  }
}


/**
 *
 */
static void
glt_enqueue(glw_root_t *gr, glw_loadable_texture_t *glt, int q)
{
  glt->glt_refcnt++;

  glt->glt_q = &gr->gr_tex_load_queue[q];
  TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[q], glt, glt_work_link);
  glt->glt_state = GLT_STATE_QUEUED;

  if(q > LQ_TENTATIVE)
    hts_cond_broadcast(&gr->gr_tex_load_cond);
  else
    hts_cond_signal(&gr->gr_tex_load_cond);
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
}


/**
 *
 */
static void *
loader_thread(void *aux)
{
  loaderaux_t *la = aux;
  glw_root_t *gr = la->la_gr;
  glw_loadable_texture_t *glt;
  pixmap_t *pm;
  char errbuf[128];
  image_meta_t im = {0};
  int cache_control = 0;
  int *ccptr = NULL;

  glw_lock(gr);

  while(1) {
    
    glt = loader_get_work(la);

    TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
    glt->glt_state = GLT_STATE_LOADING;
    LIST_REMOVE(glt, glt_flush_link);
    
    if(glt->glt_refcnt > 1) {
      char *url = strdup(glt->glt_filename);

      im.im_req_width  = glt->glt_req_xs;
      im.im_req_height = glt->glt_req_ys;
      im.im_max_width  = gr->gr_width;
      im.im_max_height = gr->gr_height;
      im.im_can_mono = 1;

      if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE]) {
	cache_control = 0;
	ccptr = &cache_control;
	
      } else if(glt->glt_q == &gr->gr_tex_load_queue[LQ_OTHER] ||
		glt->glt_q == &gr->gr_tex_load_queue[LQ_REFRESH]) {
	ccptr = DISABLE_CACHE;
      } else {
	ccptr = NULL;
      }

      glw_unlock(gr);
      pm = backend_imageloader(url, &im, gr->gr_vpaths, errbuf, sizeof(errbuf),
			       ccptr);

      glw_lock(gr);
      
      if(pm == NULL) {

	if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE]) {
	  glt_enqueue(gr, glt, LQ_OTHER);
	} else if(glt->glt_q == &gr->gr_tex_load_queue[LQ_REFRESH]) {
	  TRACE(TRACE_INFO, "GLW",
		"Unable to load %s -- %s -- using cached copy", url, errbuf);
	  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
	  glt->glt_state = GLT_STATE_VALID;
	} else {
	  TRACE(TRACE_ERROR, "GLW", "Unable to load %s -- %s", url, errbuf);
	  glt->glt_state = GLT_STATE_ERROR;
	}

      } else {

	if(glt->glt_state == GLT_STATE_LOADING) {

	  if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE] &&
	     cache_control == 1) {
	    glt_enqueue(gr, glt, LQ_REFRESH);
	  } else {
	    LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
	    glt->glt_state = GLT_STATE_VALID;
	  }

	  if(pm != NOT_MODIFIED) {
	    assert(!pixmap_is_coded(pm));
	    glt->glt_orientation = pm->pm_orientation;
	    glt->glt_aspect = pm->pm_aspect;
	    glw_tex_backend_load(gr, glt, pm);
	  }
	}

	if(pm != NOT_MODIFIED)
	  pixmap_release(pm);
      }
      free(url);
    }
    glw_tex_deref(gr, glt);
  }
 
  return NULL;
}

static void
spawn_loader(glw_root_t *gr, int only_fast)
{
  loaderaux_t *la = malloc(sizeof(loaderaux_t));
  la->la_gr = gr;
  la->la_only_fast = only_fast;
  hts_thread_create_detached("GLW texture loader", loader_thread, la,
			     THREAD_PRIO_LOW);
}

/**
 *
 */
void
glw_tex_init(glw_root_t *gr)
{
  int i;

  hts_cond_init(&gr->gr_tex_load_cond, &gr->gr_mutex);
  
  TAILQ_INIT(&gr->gr_tex_rel_queue);
  for(i = 0; i < LQ_num; i++) 
    TAILQ_INIT(&gr->gr_tex_load_queue[i]);

  for(i = 0; i < 4; i++)
    spawn_loader(gr, 0);

  for(i = 0; i < 2; i++)
    spawn_loader(gr, 1);
}

/**
 * Flush all loaded textures, must be done on the gl thread context
 */
void
glw_tex_flush_all(glw_root_t *gr)
{
  glw_loadable_texture_t *glt;

  LIST_FOREACH(glt, &gr->gr_tex_list, glt_global_link) {
    if(glt->glt_state == GLT_STATE_INACTIVE)
      continue;
    LIST_REMOVE(glt, glt_flush_link);
    if(glt->glt_state == GLT_STATE_VALID)
      glw_tex_backend_free_render_resources(gr, glt);
    if(glt->glt_state == GLT_STATE_QUEUED)
      TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
    glt->glt_state = GLT_STATE_INACTIVE;
  }
}


/**
 *
 */
void
glw_tex_purge(glw_root_t *gr)
{
  glw_loadable_texture_t *glt; 

  while((glt = TAILQ_FIRST(&gr->gr_tex_rel_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_tex_rel_queue, glt, glt_work_link);
    glw_tex_backend_free_render_resources(gr, glt);
    free(glt);
  }
}

/**
 *
 */
void
glw_tex_deref(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  glt->glt_refcnt--;

  if(glt->glt_refcnt > 0)
    return;
  
  if(glt->glt_filename != NULL) {
    if(glt->glt_state != GLT_STATE_INACTIVE)
      LIST_REMOVE(glt, glt_flush_link);

    free(glt->glt_filename);
  }
  
  LIST_REMOVE(glt, glt_global_link);

  glw_tex_backend_free_loader_resources(glt);

  TAILQ_INSERT_TAIL(&gr->gr_tex_rel_queue, glt, glt_work_link);
}


/**
 *
 */
glw_loadable_texture_t *
glw_tex_create(glw_root_t *gr, const char *filename, int flags, int xs,
	       int ys)
{
  glw_loadable_texture_t *glt;

  assert(xs != 0);
  assert(ys != 0);

  LIST_FOREACH(glt, &gr->gr_tex_list, glt_global_link)
    if(glt->glt_filename != NULL && !strcmp(glt->glt_filename, filename) &&
       glt->glt_flags == flags &&
       glt->glt_req_xs == xs &&
       glt->glt_req_ys == ys)
      break;

  if(glt == NULL) {
    glt = calloc(1, sizeof(glw_loadable_texture_t));
    glt->glt_filename = strdup(filename);
    LIST_INSERT_HEAD(&gr->gr_tex_list, glt, glt_global_link);
    glt->glt_state = GLT_STATE_INACTIVE;
    glt->glt_flags = flags;
    glt->glt_req_xs = xs;
    glt->glt_req_ys = ys;
  }

  glt->glt_refcnt++;
  return glt;
}



/**
 *
 */
static void
gl_tex_req_load(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  int q;

  if(!strncmp(glt->glt_filename, "theme://", 8)) {
    q = LQ_THEME;
  } else {
    q = LQ_TENTATIVE;
  }

  glt_enqueue(gr, glt, q);
}


/**
 *
 */
void
glw_tex_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  if(glt->glt_pixmap != NULL)
    glw_tex_backend_layout(gr, glt);

  switch(glt->glt_state) {
  case GLT_STATE_INACTIVE:
    gl_tex_req_load(gr, glt);
    break;

  case GLT_STATE_VALID:
    // FALLTHRU
  case GLT_STATE_QUEUED:
    LIST_REMOVE(glt, glt_flush_link);
    LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
    break;

  case GLT_STATE_LOADING:
  case GLT_STATE_ERROR:
    break;
  }
}
