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

static void glw_tex_deref_locked(glw_root_t *gr, glw_loadable_texture_t *glt);

static int glw_tex_load(glw_root_t *gr, glw_loadable_texture_t *glt);


void
glw_tex_is_active(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  LIST_REMOVE(glt, glt_flush_link);
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


void
glw_tex_autoflush(glw_root_t *gr)
{
  glw_loadable_texture_t *glt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while((glt = LIST_FIRST(&gr->gr_tex_flush_list)) != NULL) {
    assert(glt->glt_filename != NULL || glt->glt_pixmap != NULL);
    LIST_REMOVE(glt, glt_flush_link);
    glw_tex_backend_free_render_resources(glt);
    glt->glt_state = GLT_STATE_INACTIVE;
  }

  LIST_MOVE(&gr->gr_tex_flush_list, &gr->gr_tex_active_list, glt_flush_link);
  LIST_INIT(&gr->gr_tex_active_list);

  hts_mutex_unlock(&gr->gr_tex_mutex);
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
  int r;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while(1) {
    
    if((glt = TAILQ_FIRST(la->q)) == NULL) {
      hts_cond_wait(la->cond, &gr->gr_tex_mutex);
      continue;
    }

    TAILQ_REMOVE(la->q, glt, glt_work_link);

    if(glt->glt_refcnt > 1) {
      hts_mutex_unlock(&gr->gr_tex_mutex);
      r = glw_tex_load(gr, glt);
      hts_mutex_lock(&gr->gr_tex_mutex);
    } else {
      r = 0;
    }
    
    glt->glt_state =  r < 0 ? GLT_STATE_ERROR : GLT_STATE_VALID;

    LIST_INSERT_HEAD(&gr->gr_tex_active_list, glt, glt_flush_link);

    glw_tex_deref_locked(gr, glt);
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
  hts_cond_init(la->cond);
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

  hts_mutex_init(&gr->gr_tex_mutex);
  TAILQ_INIT(&gr->gr_tex_rel_queue);

  la = lacreate(gr, LQ_ALL_OTHER);

  for(i = 0; i < GLW_MAX(concurrency / 2, 2); i++)
    hts_thread_create_detached("GLW texture loader", loader_thread, la);

  la = lacreate(gr, LQ_THEME);
  hts_thread_create_detached("texture theme loader", loader_thread, la);

  la = lacreate(gr, LQ_THUMBS);
  for(i = 0; i < 2; i++)
    hts_thread_create_detached("texture thumbs", loader_thread, la);
}

/**
 * Flush all loaded textures, must be done on the gl thread context
 */
void
glw_tex_flush_all(glw_root_t *gr)
{
  glw_loadable_texture_t *glt;
  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(glt, &gr->gr_tex_list, glt_global_link) {
    if(glt->glt_state != GLT_STATE_VALID)
      continue;
    LIST_REMOVE(glt, glt_flush_link);
    glw_tex_backend_free_render_resources(glt);
    glt->glt_state = GLT_STATE_INACTIVE;
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

/**
 *
 */
static int
glw_tex_load(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  AVCodecContext *ctx;
  AVCodec *codec;
  AVFrame *frame;
  int r, got_pic, w, h, want_thumb;
  const char *url;
  char errbuf[128];

  if(glt->glt_pixmap != NULL) {
    pixmap_t *pm = glt->glt_pixmap;
    r = glw_tex_backend_load(gr, glt, &pm->pm_pict,
			     pm->pm_pixfmt,
			     pm->pm_width, pm->pm_height,
			     pm->pm_width, pm->pm_height);
    return r;
  }

  if(glt->glt_filename == NULL)
    return -1;

  url = glt->glt_filename;
  if(!strncmp(url, "thumb://", 8)) {
    url = url + 8;
    want_thumb = 1;
  } else {
    want_thumb = 0;
  }

  pixmap_t *pm = backend_imageloader(url, want_thumb, gr->gr_theme, errbuf, 
				     sizeof(errbuf));
  if(pm == NULL) {
    TRACE(TRACE_DEBUG, "GLW", "Unable to load %s -- %s", url, errbuf);
    return -1;
  }

  glt->glt_orientation = pm->pm_orientation;

  if(pm->pm_codec == CODEC_ID_NONE) {
    glt->glt_aspect = (float)pm->pm_width / (float)pm->pm_height;

    r = glw_tex_backend_load(gr, glt, &pm->pm_pict,
			     pm->pm_pixfmt,
			     pm->pm_width, pm->pm_height,
			     pm->pm_width, pm->pm_height);
    pixmap_release(pm);
    return r;
  }

  ctx = avcodec_alloc_context();
  codec = avcodec_find_decoder(pm->pm_codec);
  
  ctx->codec_id   = codec->id;
  ctx->codec_type = codec->type;

  if(avcodec_open(ctx, codec) < 0) {
    av_free(ctx);
    pixmap_release(pm);
    TRACE(TRACE_ERROR, "glw", "%s: unable to open codec", url);
    return -1;
  }
  

  if(ctx->width == 0 || ctx->height == 0) {
    av_free(ctx);
    pixmap_release(pm);
    TRACE(TRACE_INFO, "glw", "%s: invalid picture dimensions", url);
    return -1;
  }

  frame = avcodec_alloc_frame();

#ifdef WII
  if(pm->pm_width > 1280 || pm->pm_height > 960)
    ctx->lowres = 1;
  if(pm->pm_width > 2560  || pm->pm_height > 1920)
    ctx->lowres = 2;
#endif

  if(ctx->lowres)
    TRACE(TRACE_DEBUG, "GLW", "%s: DCT-Scaling image down by factor %d",
	  url, 1 << ctx->lowres);

  r = avcodec_decode_video(ctx, frame, &got_pic, pm->pm_data,  pm->pm_size);

  if(want_thumb && pm->pm_flags & PIXMAP_THUMBNAIL) {
    w = 160;
    h = 160 * ctx->height / ctx->width;
  } else {
    w = ctx->width;
    h = ctx->height;
  }

  pixmap_release(pm);

  if(glt->glt_req_xs != -1 && glt->glt_req_ys != -1) {
    w = glt->glt_req_xs;
    h = glt->glt_req_ys;

  } else if(glt->glt_req_xs != -1) {
    w = glt->glt_req_xs;
    h = glt->glt_req_xs * ctx->height / ctx->width;

  } else if(glt->glt_req_ys != -1) {
    w = glt->glt_req_ys * ctx->width / ctx->height;
    h = glt->glt_req_ys;

  } else if(w > 64 && h > 64) {
    if(w > gr->gr_width) {
      h = h * gr->gr_width / w;
      w = gr->gr_width;
    }

    if(h > gr->gr_height) {
      w = w * gr->gr_height / h;
      h = gr->gr_height;
    }
  }

  // Compute correct aspect ratio based on orientation
  // See pixmap.h for the secret constant '5'
  if(glt->glt_orientation < 5) {
    glt->glt_aspect = (float)w / (float)h;
  } else {
    glt->glt_aspect = (float)h / (float)w;
  }



  r = glw_tex_backend_load(gr, glt, (AVPicture *)frame, 
			   ctx->pix_fmt, ctx->width, ctx->height, w, h);

  av_free(frame);

  avcodec_close(ctx);
  av_free(ctx);

  return r;
}


/**
 *
 */
void
glw_tex_purge(glw_root_t *gr)
{
  glw_loadable_texture_t *glt; 
  hts_mutex_lock(&gr->gr_tex_mutex);

  while((glt = TAILQ_FIRST(&gr->gr_tex_rel_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_tex_rel_queue, glt, glt_work_link);
    glw_tex_backend_free_render_resources(glt);
    free(glt);
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

/**
 *
 */
static void
glw_tex_deref_locked(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  glt->glt_refcnt--;

  if(glt->glt_refcnt > 0)
    return;
  
  if(glt->glt_filename != NULL) {
    if(glt->glt_state == GLT_STATE_VALID || glt->glt_state == GLT_STATE_ERROR)
      LIST_REMOVE(glt, glt_flush_link);

    free(glt->glt_filename);
  }
  
  if(glt->glt_pixmap != NULL)
    pixmap_release(glt->glt_pixmap);

  LIST_REMOVE(glt, glt_global_link);

  glw_tex_backend_free_loader_resources(glt);

  TAILQ_INSERT_TAIL(&gr->gr_tex_rel_queue, glt, glt_work_link);
}


/**
 *
 */
void
glw_tex_deref(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  glw_tex_deref_locked(gr, glt);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


/**
 *
 */
glw_loadable_texture_t *
glw_tex_create(glw_root_t *gr, const char *filename, int flags, int xs,
	       int ys)
{
  glw_loadable_texture_t *glt;

  hts_mutex_lock(&gr->gr_tex_mutex);
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

  hts_mutex_unlock(&gr->gr_tex_mutex);
  return glt;
}


/**
 *
 */
glw_loadable_texture_t *
glw_tex_create_from_pixmap(glw_root_t *gr, pixmap_t *pm)
{
  glw_loadable_texture_t *glt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  glt = calloc(1, sizeof(glw_loadable_texture_t));
  glt->glt_filename = NULL;
  glt->glt_pixmap = pixmap_dup(pm);

  LIST_INSERT_HEAD(&gr->gr_tex_list, glt, glt_global_link);
  glt->glt_state = GLT_STATE_INACTIVE;

  glt->glt_refcnt++;

  hts_mutex_unlock(&gr->gr_tex_mutex);
  return glt;
}


/**
 *
 */
static void
gl_tex_req_load(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  glt->glt_refcnt++;

  int q = LQ_ALL_OTHER;

  if(glt->glt_filename != NULL) {
    if(!strncmp(glt->glt_filename, "thumb://", 8))
      q = LQ_THUMBS;
    else if(!strncmp(glt->glt_filename, "theme://", 8))
      q  = LQ_THEME;
  }

  TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[q], glt, glt_work_link);
  glt->glt_state = GLT_STATE_LOADING;

  hts_cond_signal(&gr->gr_tex_load_cond[q]);
  hts_mutex_unlock(&gr->gr_tex_mutex);
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
    return;
    
  case GLT_STATE_LOADING:
    return;

  case GLT_STATE_VALID:
    glw_tex_backend_layout(gr, glt);
    /* FALLTHRU */

  case GLT_STATE_ERROR:
    glw_tex_is_active(gr, glt);
    break;
  }
}
