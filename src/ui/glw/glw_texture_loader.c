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

#define LQ_ALL_OTHER 0
#define LQ_THEME     1
#define LQ_THUMBS    2


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
  glw_root_t *gr;
  struct glw_loadable_texture_queue *q;
  hts_cond_t *cond;
} loaderaux_t;



/**
 *
 */
static void *
loader_thread(void *aux)
{
  loaderaux_t *la = aux;
  glw_root_t *gr = la->gr;
  glw_loadable_texture_t *glt;
  pixmap_t *pm;
  char errbuf[128];
  image_meta_t im = {0};

  glw_lock(gr);

  while(1) {
    
    if((glt = TAILQ_FIRST(la->q)) == NULL) {
      hts_cond_wait(la->cond, &gr->gr_mutex);
      continue;
    }

    TAILQ_REMOVE(la->q, glt, glt_work_link);
    glt->glt_state = GLT_STATE_LOADING;
    LIST_REMOVE(glt, glt_flush_link);

    if(glt->glt_refcnt > 1) {
      char *url = strdup(glt->glt_filename);

      im.im_req_width  = glt->glt_req_xs;
      im.im_req_height = glt->glt_req_ys;
      im.im_max_width  = gr->gr_width;
      im.im_max_height = gr->gr_height;
      im.im_can_mono = 1;

      glw_unlock(gr);
      pm = backend_imageloader(url, &im, gr->gr_vpaths, errbuf, sizeof(errbuf));
      free(url);
      glw_lock(gr);
      
      if(pm == NULL) {
	TRACE(TRACE_ERROR, "GLW", "Unable to load %s -- %s", url, errbuf);
	glt->glt_state = GLT_STATE_ERROR;

      } else {
	assert(!pixmap_is_coded(pm));

	if(glt->glt_state == GLT_STATE_LOADING) {
	  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
	  glt->glt_state = GLT_STATE_VALID;

	  glt->glt_orientation = pm->pm_orientation;
	  glt->glt_aspect = pm->pm_aspect;
	  glw_tex_backend_load(gr, glt, pm);
	}
	pixmap_release(pm);
      }
    }
    glw_tex_deref(gr, glt);
  }
 
  return NULL;
}

static loaderaux_t *
lacreate(glw_root_t *gr, int i)
{
  loaderaux_t *la = malloc(sizeof(loaderaux_t));
  la->gr = gr;

  la->q = &gr->gr_tex_load_queue[i];
  TAILQ_INIT(la->q);

  la->cond = &gr->gr_tex_load_cond[i];
  hts_cond_init(la->cond, &gr->gr_mutex);
  return la;
}

/**
 *
 */
void
glw_tex_init(glw_root_t *gr)
{
  int i;
  extern int concurrency;
  loaderaux_t *la;

  TAILQ_INIT(&gr->gr_tex_rel_queue);

  la = lacreate(gr, LQ_ALL_OTHER);

  for(i = 0; i < GLW_MAX(concurrency / 2, 2); i++) {
    hts_thread_create_detached("GLW texture loader", loader_thread, la,
			       THREAD_PRIO_NORMAL);
    break;
  }

  la = lacreate(gr, LQ_THEME);
  hts_thread_create_detached("texture theme loader", loader_thread, la,
			     THREAD_PRIO_NORMAL);

  la = lacreate(gr, LQ_THUMBS);
  for(i = 0; i < 2; i++)
    hts_thread_create_detached("texture thumbs", loader_thread, la,
			       THREAD_PRIO_NORMAL);
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
  glt->glt_refcnt++;

  int q = LQ_ALL_OTHER;

  if(glt->glt_filename != NULL) {
    if(!strncmp(glt->glt_filename, "thumb://", 8) ||
       strstr(glt->glt_filename, "#") != NULL)
      q = LQ_THUMBS;
    else if(!strncmp(glt->glt_filename, "theme://", 8) ||
	    !strncmp(glt->glt_filename, "skin://", 7))
      q  = LQ_THEME;
  }

  glt->glt_q = &gr->gr_tex_load_queue[q];
  TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[q], glt, glt_work_link);
  glt->glt_state = GLT_STATE_QUEUED;

  hts_cond_signal(&gr->gr_tex_load_cond[q]);
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
}


/**
 *
 */
void
glw_tex_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  switch(glt->glt_state) {
  case GLT_STATE_INACTIVE:
    gl_tex_req_load(gr, glt);
    break;

  case GLT_STATE_VALID:
    glw_tex_backend_layout(gr, glt);
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
