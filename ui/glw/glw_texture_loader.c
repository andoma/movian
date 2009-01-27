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

#include "showtime.h"
#include "fileaccess/fa_imageloader.h"

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
    assert(glt->glt_filename != NULL);
    LIST_REMOVE(glt, glt_flush_link);
    glw_tex_backend_free_render_resources(glt);
    glt->glt_state = GLT_STATE_INACTIVE;
  }

  LIST_MOVE(&gr->gr_tex_flush_list, &gr->gr_tex_active_list, glt_flush_link);
  LIST_INIT(&gr->gr_tex_active_list);

  hts_mutex_unlock(&gr->gr_tex_mutex);
}




static void *
loader_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_loadable_texture_t *glt;
  int r;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while(1) {
    
    while(1) {

      if((glt = TAILQ_FIRST(&gr->gr_tex_load_queue[0])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[0], glt, glt_work_link);
	break;
      }
      if((glt = TAILQ_FIRST(&gr->gr_tex_load_queue[1])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[1], glt, glt_work_link);
	break;
      }
      hts_cond_wait(&gr->gr_tex_load_cond, &gr->gr_tex_mutex);
    }

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
}


void
glw_tex_init(glw_root_t *gr)
{
  int i;
  extern int concurrency;

  hts_mutex_init(&gr->gr_tex_mutex);
  hts_cond_init(&gr->gr_tex_load_cond);

  TAILQ_INIT(&gr->gr_tex_rel_queue);
  TAILQ_INIT(&gr->gr_tex_load_queue[0]);
  TAILQ_INIT(&gr->gr_tex_load_queue[1]);

  /* Start multiple workers for decoding images */
  for(i = 0; i < concurrency; i++)
    hts_thread_create_detached(loader_thread, gr);
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
  fa_image_load_ctrl_t ctrl;
  AVCodecContext *ctx;
  AVCodec *codec;
  AVFrame *frame;
  int r, got_pic, w, h;

  if(glt->glt_filename == NULL)
    return -1;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.url = glt->glt_filename;
  
  if(fa_imageloader(&ctrl, gr->gr_theme))
    return -1;

  fflock();

  ctx = avcodec_alloc_context();
  codec = avcodec_find_decoder(ctrl.codecid);
  
  if(avcodec_open(ctx, codec) < 0) {
    ffunlock();
    av_free(ctx);
    free(ctrl.data);
    return -1;
  }
  
  ffunlock();

  frame = avcodec_alloc_frame();

  r = avcodec_decode_video(ctx, frame, &got_pic, ctrl.data, ctrl.datasize);

  free(ctrl.data);

  if(ctrl.want_thumb && ctrl.got_thumb) {
    w = 160;
    h = 160 * ctx->height / ctx->width;
  } else {
    w = ctx->width;
    h = ctx->height;
  }

  r = glw_tex_backend_load(gr, glt, frame, 
			   ctx->pix_fmt, ctx->width, ctx->height, w, h);

  av_free(frame);

  fflock();
  avcodec_close(ctx);
  ffunlock();
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

    LIST_REMOVE(glt, glt_global_link);
    free(glt->glt_filename);
  }
  
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
glw_tex_create(glw_root_t *gr, const char *filename)
{
  glw_loadable_texture_t *glt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(glt, &gr->gr_tex_list, glt_global_link)
    if(!strcmp(glt->glt_filename, filename))
      break;

  if(glt == NULL) {
    glt = calloc(1, sizeof(glw_loadable_texture_t));
    glt->glt_filename = strdup(filename);
    LIST_INSERT_HEAD(&gr->gr_tex_list, glt, glt_global_link);
    glt->glt_state = GLT_STATE_INACTIVE;
  }

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

  if(!strncmp(glt->glt_filename, "thumb://", 8)) {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[1], glt, glt_work_link);
  } else {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[0], glt, glt_work_link);
  }

  glt->glt_state = GLT_STATE_LOADING;

  hts_cond_signal(&gr->gr_tex_load_cond);
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
    glw_tex_backend_layout(glt);
    /* FALLTHRU */

  case GLT_STATE_ERROR:
    glw_tex_is_active(gr, glt);
    break;
  }
}
