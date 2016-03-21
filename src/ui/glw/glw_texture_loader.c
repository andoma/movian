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

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "glw.h"
#include "glw_texture.h"

#include "backend/backend.h"


#if 0
/**
 *
 */
static void
glt_set_state0(glw_loadable_texture_t *glt, int state,
               const char *file, int line)
{
  printf("%p goes from %d to %d by %s:%d\n", glt, glt->glt_state, state,
         file, line);
  glt->glt_state = state;
}

#define glt_set_state(a, b) glt_set_state0(a, b, __FILE__, __LINE__)
#else
#define glt_set_state(a, b) (a)->glt_state = b
#endif

/**
 *
 */
static void
glt_destroy(glw_loadable_texture_t *glt)
{
  cancellable_release(glt->glt_cancellable);
  free(glt);
}

/**
 *
 */
static void
glt_cancel(glw_loadable_texture_t *glt)
{
  cancellable_cancel(glt->glt_cancellable);
}


/**
 *
 */
static void
glw_tex_purge_stash(glw_root_t *gr, int stash)
{
  while(gr->gr_tex_stash[stash].size > gr->gr_tex_stash[stash].limit) {
    glw_loadable_texture_t *glt = TAILQ_FIRST(&gr->gr_tex_stash[stash].q);
    if(glt == NULL)
      break;

    assert(glt->glt_q == &gr->gr_tex_stash[stash].q);

    TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
    gr->gr_tex_stash[stash].size -= glt->glt_size;

    glw_tex_backend_free_loader_resources(glt);
    glw_tex_backend_free_render_resources(gr, glt);
    glt_set_state(glt, GLT_STATE_INACTIVE);
    if(glt->glt_refcnt == 0) {

      if(glt->glt_url != NULL) {
        rstr_release(glt->glt_url);
        glt->glt_url = NULL;
        LIST_REMOVE(glt, glt_global_link);
      }
      glt_destroy(glt);
    }
  }
}


/**
 *
 */
static int
glw_tex_stash(glw_root_t *gr, glw_loadable_texture_t *glt, int unreferenced)
{
  if(glt->glt_url == NULL)
    return 1;

  glt_set_state(glt, GLT_STATE_STASHED);

  int stash = glt->glt_origin_type == IMAGE_JPEG;

  glt->glt_stash = stash;
  glt->glt_q = &gr->gr_tex_stash[stash].q;

  TAILQ_INSERT_TAIL(glt->glt_q, glt, glt_work_link);
  gr->gr_tex_stash[stash].size += glt->glt_size;

  glw_tex_purge_stash(gr, stash);
  return 0;
}



static void
glw_tex_unstash(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  int stash = glt->glt_stash;

  TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
  gr->gr_tex_stash[stash].size -= glt->glt_size;
}



void
glw_tex_autoflush(glw_root_t *gr)
{
  glw_loadable_texture_t *glt;

  while((glt = LIST_FIRST(&gr->gr_tex_flush_list)) != NULL) {
    LIST_REMOVE(glt, glt_flush_link);
    switch(glt->glt_state) {
    case GLT_STATE_VALID:
      if(glw_tex_stash(gr, glt, 0)) {
        glw_tex_backend_free_render_resources(gr, glt);
        glt_set_state(glt, GLT_STATE_INACTIVE);
      }
      break;

    case GLT_STATE_QUEUED:
      glt_set_state(glt, GLT_STATE_INACTIVE);
      TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
      glw_tex_deref(gr, glt);  // beware! glt may be free'd here
      break;

    case GLT_STATE_LOADING:
      glt_set_state(glt, GLT_STATE_LOAD_ABORT);
      glt_cancel(glt);
      break;

    case GLT_STATE_STASHED:
    case GLT_STATE_LOAD_ABORT:
    case GLT_STATE_ERROR:
    case GLT_STATE_INACTIVE:
      printf("%s: %p, Autoflush unexpected state %d\n",
	     rstr_get(glt->glt_url), glt, glt->glt_state);
      abort();
    }
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
    if(gr->gr_tex_threads_running == 0)
      return NULL;
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
  glt_set_state(glt, GLT_STATE_QUEUED);

  if(q > LQ_TENTATIVE)
    hts_cond_broadcast(&gr->gr_tex_load_cond);
  else
    hts_cond_signal(&gr->gr_tex_load_cond);
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
  image_t *img;
  char errbuf[128];
  image_meta_t im = {0};
  int cache_control = 0;
  int *ccptr = NULL;

  glw_lock(gr);

  while((glt = loader_get_work(la)) != NULL) {

    TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
    glt_set_state(glt, GLT_STATE_LOADING);

    if(glt->glt_refcnt > 1) {
      rstr_t *url = rstr_dup(glt->glt_url);

      im.im_req_width  = glt->glt_req_xs;
      im.im_req_height = glt->glt_req_ys;
      im.im_max_width  = gr->gr_width;
      im.im_max_height = gr->gr_height;
      im.im_can_mono = 1;
      im.im_corner_radius = glt->glt_radius;

      im.im_intensity_analysis =
        !!(glt->glt_flags & GLW_TEX_INTENSITY_ANALYSIS);
      im.im_primary_color_analysis =
        !!(glt->glt_flags & GLW_TEX_PRIMARY_COLOR_ANALYSIS);

      im.im_corner_selection = glt->glt_flags & (GLW_TEX_CORNER_TOPLEFT |
                                                 GLW_TEX_CORNER_TOPRIGHT |
                                                 GLW_TEX_CORNER_BOTTOMLEFT |
                                                 GLW_TEX_CORNER_BOTTOMRIGHT);
      im.im_shadow = glt->glt_shadow;
      im.im_req_aspect = glt->glt_req_aspect;

      if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE]) {
	cache_control = 0;
	ccptr = &cache_control;

      } else if(glt->glt_q == &gr->gr_tex_load_queue[LQ_REFRESH]) {
	ccptr = BYPASS_CACHE;
      } else {
	ccptr = NULL;
      }

      cancellable_reset(glt->glt_cancellable);

      glw_unlock(gr);
      img = backend_imageloader(url, &im, gr->gr_fa_resolver,
                                errbuf, sizeof(errbuf),
                                ccptr, glt->glt_cancellable);

      glw_lock(gr);

#if 0
      if(pm != NULL && pm != NOT_MODIFIED) {
	static int fail_simulator;
	fail_simulator++;
	if(fail_simulator == 10) {
	  fail_simulator = 0;
	  pixmap_release(pm);
	  pm = NULL;
	  snprintf(errbuf, sizeof(errbuf), "Simulated failure");
	}
      }
#endif

      if(glt->glt_state == GLT_STATE_LOAD_ABORT) {
	if(img != NULL && img != NOT_MODIFIED)
	  image_release(img);

        if(gconf.enable_image_debug)
          TRACE(TRACE_DEBUG, "GLW", "Load of %s was aborted", rstr_get(url));

        glt_set_state(glt, GLT_STATE_INACTIVE);
      } else if(img == NULL) {

	if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE]) {

          if(glt->glt_state == GLT_STATE_LOADING)
            glt_enqueue(gr, glt, LQ_OTHER);

	} else if(glt->glt_q == &gr->gr_tex_load_queue[LQ_REFRESH]) {

          if(gconf.enable_image_debug)
            TRACE(TRACE_DEBUG, "GLW",
                  "Unable to load image %s -- %s -- using cached copy",
                  rstr_get(url), errbuf);

          glt_set_state(glt, GLT_STATE_VALID);
	} else {
	  // if glt->glt_url is NULL we have aborted so don't ERR log

          if(gconf.enable_image_debug) {

            if(glt->glt_url != NULL)
              TRACE(TRACE_ERROR, "GLW", "Unable to load image %s -- %s",
                    rstr_get(url), errbuf);
            else
              TRACE(TRACE_DEBUG, "GLW", "Aborted load of %s",
                    rstr_get(url));
          }

          glt_set_state(glt, GLT_STATE_ERROR);
	  LIST_REMOVE(glt, glt_flush_link);
          glw_need_refresh(gr, 0);
	}

      } else {

	if(glt->glt_state == GLT_STATE_LOADING) {

	  if(glt->glt_q == &gr->gr_tex_load_queue[LQ_TENTATIVE] &&
	     cache_control == 1) {
	    glt_enqueue(gr, glt, LQ_REFRESH);
	  } else {
            glt_set_state(glt, GLT_STATE_VALID);
	  }

	  if(img != NOT_MODIFIED) {

            // Actually upload the texture to the render backend

            image_component_t *ic = image_find_component(img, IMAGE_PIXMAP);

            assert(ic != NULL);

            pixmap_t *pm = ic->pm;

	    glt->glt_aspect        = pm->pm_aspect;
	    glt->glt_margin        = pm->pm_margin;
            glt->glt_xs            = pm->pm_width;
            glt->glt_ys            = pm->pm_height;

            glt->glt_origin_type   = img->im_origin_coded_type;
	    glt->glt_orientation   = img->im_orientation;
            glt->glt_intensity     = pm->pm_intensity;

            if(gconf.enable_image_debug)
              TRACE(TRACE_DEBUG, "GLW",
                    "Loaded %s (%d x %d)",
                    rstr_get(url), pm->pm_width, pm->pm_height);


	    glt->glt_size          = glw_tex_backend_load(gr, glt, pm);
	    glw_need_refresh(gr, 0);
	  }
	}

	if(img != NOT_MODIFIED)
	  image_release(img);
      }
      rstr_release(url);
    }
    glw_tex_deref(gr, glt);
  }
 
  glw_unlock(gr);
  free(la);
  return NULL;
}

static void
spawn_loader(glw_root_t *gr, int only_fast, int idx)
{
  loaderaux_t *la = malloc(sizeof(loaderaux_t));
  la->la_gr = gr;
  la->la_only_fast = only_fast;
  hts_thread_create_joinable("GLW texture loader", &gr->gr_tex_threads[idx],
			     loader_thread, la,
			     only_fast ? THREAD_PRIO_UI_WORKER_MED :
			     THREAD_PRIO_UI_WORKER_LOW);
}

/**
 *
 */
void
glw_tex_init(glw_root_t *gr)
{
  int i;
  gr->gr_tex_threads_running = 1;
  hts_cond_init(&gr->gr_tex_load_cond, &gr->gr_mutex);

  TAILQ_INIT(&gr->gr_tex_rel_queue);
  TAILQ_INIT(&gr->gr_tex_stash[0].q);
  TAILQ_INIT(&gr->gr_tex_stash[1].q);

  gr->gr_tex_stash[0].limit = 16 * 1024 * 1024;
  gr->gr_tex_stash[1].limit = 16 * 1024 * 1024;

  for(i = 0; i < LQ_num; i++)
    TAILQ_INIT(&gr->gr_tex_load_queue[i]);

  for(i = 0; i < GLW_TEXTURE_THREADS; i++)
    spawn_loader(gr, i >= 4, i);
}


/**
 *
 */
void
glw_tex_fini(glw_root_t *gr)
{
  int i;
  glw_lock(gr);
  gr->gr_tex_threads_running = 0;
  hts_cond_broadcast(&gr->gr_tex_load_cond);
  glw_unlock(gr);

  for(i = 0; i < GLW_TEXTURE_THREADS; i++)
    hts_thread_join(&gr->gr_tex_threads[i]);
}

/**
 * Flush all loaded textures, must be done on the gl thread context
 */
void
glw_tex_flush_all(glw_root_t *gr)
{
  glw_loadable_texture_t *glt, *next;
  for(glt = LIST_FIRST(&gr->gr_tex_list); glt != NULL; glt = next) {
    next = LIST_NEXT(glt, glt_global_link);
    switch(glt->glt_state) {

    case GLT_STATE_INACTIVE:
      continue;

    case GLT_STATE_STASHED:
      glw_tex_unstash(gr, glt);
      glw_tex_backend_free_render_resources(gr, glt);
      break;

    case GLT_STATE_VALID:
      LIST_REMOVE(glt, glt_flush_link);
      glw_tex_backend_free_render_resources(gr, glt);
      break;

    case GLT_STATE_QUEUED:
      LIST_REMOVE(glt, glt_flush_link);
      TAILQ_REMOVE(glt->glt_q, glt, glt_work_link);
      glt_set_state(glt, GLT_STATE_INACTIVE);
      glw_tex_deref(gr, glt);
      continue;

    case GLT_STATE_LOADING:
      LIST_REMOVE(glt, glt_flush_link);
      break;

    case GLT_STATE_LOAD_ABORT:
      continue;

    case GLT_STATE_ERROR:
      break;
    }
    glt_set_state(glt, GLT_STATE_INACTIVE);
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
    glt_destroy(glt);
  }
}

/**
 *
 */
void
glw_tex_deref(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  glt->glt_refcnt--;

  if(glt->glt_refcnt > 0) {

    // Loading state holds a ref, so this means that we're the only one
    if(glt->glt_refcnt == 1 && glt->glt_state == GLT_STATE_LOADING)
      goto unlink;
    return;
  }

  switch(glt->glt_state) {
  case GLT_STATE_VALID:
    LIST_REMOVE(glt, glt_flush_link);
    if(glw_tex_stash(gr, glt, 1))
      break;
    return;

  case GLT_STATE_STASHED:
    return;

  case GLT_STATE_QUEUED:
  case GLT_STATE_LOADING:
    LIST_REMOVE(glt, glt_flush_link);
    break;
  case GLT_STATE_INACTIVE:
  case GLT_STATE_ERROR:
  case GLT_STATE_LOAD_ABORT:
    break;
  }

  glw_tex_backend_free_loader_resources(glt);
  TAILQ_INSERT_TAIL(&gr->gr_tex_rel_queue, glt, glt_work_link);

  if(glt->glt_url == NULL)
    return;

 unlink:
  if(glt->glt_state == GLT_STATE_LOADING)
    glt_cancel(glt);
  rstr_release(glt->glt_url);
  glt->glt_url = NULL;
  LIST_REMOVE(glt, glt_global_link);
}


/**
 *
 */
glw_loadable_texture_t *
glw_tex_create(glw_root_t *gr, rstr_t *filename, int flags, int xs, int ys,
	       int radius, int shadow, float aspect)
{
  glw_loadable_texture_t *glt;

  assert(xs == -1 || xs > 0);
  assert(ys == -1 || ys > 0);

  LIST_FOREACH(glt, &gr->gr_tex_list, glt_global_link)
    if(!strcmp(rstr_get(glt->glt_url), rstr_get(filename)) &&
       glt->glt_flags == flags &&
       glt->glt_req_xs == xs &&
       glt->glt_req_ys == ys &&
       glt->glt_radius == radius &&
       glt->glt_shadow == shadow &&
       glt->glt_req_aspect == aspect)
      break;

  if(glt == NULL) {
    glt = calloc(1, sizeof(glw_loadable_texture_t));
    glt->glt_cancellable = cancellable_create();
    glt->glt_url = rstr_dup(filename);
    LIST_INSERT_HEAD(&gr->gr_tex_list, glt, glt_global_link);
    glt_set_state(glt, GLT_STATE_INACTIVE);
    glt->glt_flags = flags;
    glt->glt_req_xs = xs;
    glt->glt_req_ys = ys;
    glt->glt_radius = radius;
    glt->glt_shadow = shadow;
    glt->glt_req_aspect = aspect;
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

  if(!strncmp(rstr_get(glt->glt_url), "skin://", strlen("skin://"))) {
    q = LQ_SKIN;
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

  case GLT_STATE_STASHED:
    glw_tex_unstash(gr, glt);
    glt_set_state(glt, GLT_STATE_VALID);
    break;

  case GLT_STATE_VALID:
  case GLT_STATE_QUEUED:
  case GLT_STATE_LOADING:
    LIST_REMOVE(glt, glt_flush_link);
    break;

  case GLT_STATE_ERROR:
    return;

  case GLT_STATE_LOAD_ABORT:
    // We need to wait for it to enter inactive state again
    return;
  }
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
}
