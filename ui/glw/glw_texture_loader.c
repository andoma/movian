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

#include "config.h"

#include <assert.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "glw.h"
#include "showtime.h"
#include "glw_texture.h"

static void glw_tex_deref_locked(glw_root_t *gr, glw_texture_t *gt);


void
glw_tex_is_active(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  LIST_REMOVE(gt, gt_flush_link);
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, gt, gt_flush_link);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


void
glw_tex_autoflush(glw_root_t *gr)
{
  glw_texture_t *gt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while((gt = LIST_FIRST(&gr->gr_tex_flush_list)) != NULL) {
    assert(gt->gt_filename != NULL);
    LIST_REMOVE(gt, gt_flush_link);
    glw_tex_backend_free_render_resources(gt);
    gt->gt_state = GT_STATE_INACTIVE;
  }

  LIST_MOVE(&gr->gr_tex_flush_list, &gr->gr_tex_active_list, gt_flush_link);
  LIST_INIT(&gr->gr_tex_active_list);

  hts_mutex_unlock(&gr->gr_tex_mutex);
}




static void *
loader_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_texture_t *gt;
  int r;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while(1) {
    
    while(1) {

      if((gt = TAILQ_FIRST(&gr->gr_tex_load_queue[0])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[0], gt, gt_work_link);
	break;
      }
      if((gt = TAILQ_FIRST(&gr->gr_tex_load_queue[1])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[1], gt, gt_work_link);
	break;
      }
      hts_cond_wait(&gr->gr_tex_load_cond, &gr->gr_tex_mutex);
    }

    if(gt->gt_refcnt > 1) {
      hts_mutex_unlock(&gr->gr_tex_mutex);
      r = glw_tex_backend_decode(gr, gt);
      hts_mutex_lock(&gr->gr_tex_mutex);
    } else {
      r = 0;
    }
    
    gt->gt_state =  r < 0 ? GT_STATE_ERROR : GT_STATE_VALID;

    LIST_INSERT_HEAD(&gr->gr_tex_active_list, gt, gt_flush_link);

    glw_tex_deref_locked(gr, gt);
  }
}


void
glw_tex_init(glw_root_t *gr)
{
  int i;
  hts_thread_t imageptid;
  extern int concurrency;

  hts_mutex_init(&gr->gr_tex_mutex);
  hts_cond_init(&gr->gr_tex_load_cond);

  TAILQ_INIT(&gr->gr_tex_rel_queue);
  TAILQ_INIT(&gr->gr_tex_load_queue[0]);
  TAILQ_INIT(&gr->gr_tex_load_queue[1]);

  /* Start multiple workers for decoding images */
  for(i = 0; i < concurrency; i++)
    hts_thread_create(&imageptid, loader_thread, gr);
}

/**
 * Flush all loaded textures, must be done on the gl thread context
 */
void
glw_tex_flush_all(glw_root_t *gr)
{
  glw_texture_t *gt;
  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(gt, &gr->gr_tex_list, gt_global_link) {
    if(gt->gt_state != GT_STATE_VALID)
      continue;
    LIST_REMOVE(gt, gt_flush_link);
    glw_tex_backend_free_render_resources(gt);
    gt->gt_state = GT_STATE_INACTIVE;
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
}



/*****************************************************************************
 *
 *
 *
 */
void
glw_tex_purge(glw_root_t *gr)
{
  glw_texture_t *gt; 
  hts_mutex_lock(&gr->gr_tex_mutex);

  while((gt = TAILQ_FIRST(&gr->gr_tex_rel_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_tex_rel_queue, gt, gt_work_link);
    glw_tex_backend_free_render_resources(gt);
    free(gt);
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


static void
glw_tex_deref_locked(glw_root_t *gr, glw_texture_t *gt)
{
  gt->gt_refcnt--;

  if(gt->gt_refcnt > 0)
    return;
  
  if(gt->gt_filename != NULL) {
    if(gt->gt_state == GT_STATE_VALID || gt->gt_state == GT_STATE_ERROR)
      LIST_REMOVE(gt, gt_flush_link);

    LIST_REMOVE(gt, gt_global_link);
    free((void *)gt->gt_filename);
  }
  
  glw_tex_backend_free_loader_resources(gt);

  TAILQ_INSERT_TAIL(&gr->gr_tex_rel_queue, gt, gt_work_link);
}

void
glw_tex_deref(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  glw_tex_deref_locked(gr, gt);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

glw_texture_t *
glw_tex_create(glw_root_t *gr, const char *filename)
{
  glw_texture_t *gt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(gt, &gr->gr_tex_list, gt_global_link)
    if(!strcmp(gt->gt_filename, filename))
      break;

  if(gt == NULL) {
    gt = calloc(1, sizeof(glw_texture_t));
    gt->gt_filename = strdup(filename);
    LIST_INSERT_HEAD(&gr->gr_tex_list, gt, gt_global_link);
    gt->gt_state = GT_STATE_INACTIVE;
  }

  gt->gt_refcnt++;

  hts_mutex_unlock(&gr->gr_tex_mutex);
  return gt;
}


static void
gl_tex_req_load(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  gt->gt_refcnt++;

  if(!strncmp(gt->gt_filename, "thumb://", 8)) {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[1], gt, gt_work_link);
  } else {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[0], gt, gt_work_link);
  }

  gt->gt_state = GT_STATE_LOADING;

  hts_cond_signal(&gr->gr_tex_load_cond);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

void
glw_tex_layout(glw_root_t *gr, glw_texture_t *gt)
{
  switch(gt->gt_state) {
  case GT_STATE_INACTIVE:
    gl_tex_req_load(gr, gt);
    return;
    
  case GT_STATE_LOADING:
    return;

  case GT_STATE_VALID:
    glw_tex_backend_layout(gt);
    /* FALLTHRU */

  case GT_STATE_ERROR:
    glw_tex_is_active(gr, gt);
    break;
  }
}
