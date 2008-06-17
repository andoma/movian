/*
 *  File probing functions for browser
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

#define _GNU_SOURCE
#include <pthread.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "browser.h"
#include "browser_probe.h"
#include "browser_view.h"

static void probe_figure_primary_content(browser_root_t *br,
					 browser_node_t *bn);

/**
 *
 */
static void *
browser_probe_thread(void *arg)
{
  browser_root_t *br = arg;
  browser_node_t *bn;
  int type;

  pthread_mutex_lock(&br->br_probe_mutex);

  while(br->br_probe_run) {

    if((bn = TAILQ_FIRST(&br->br_probe_queue)) != NULL) {
      TAILQ_REMOVE(&br->br_probe_queue, bn, bn_probe_link);
      pthread_mutex_unlock(&br->br_probe_mutex);


      /* If the probing code is the only one with a reference, don't probe */
      if(bn->bn_refcnt > 1) {

	pthread_mutex_lock(&bn->bn_ftags_mutex);

	if(bn->bn_type == FA_FILE) {

	  type = fa_probe(&bn->bn_ftags, bn->bn_url);

	  if(type == FA_NONE) {
	    glw_destroy(bn->bn_icon_xfader);
	  } else {
	    bn->bn_type = type;
	    browser_view_node_model_update(bn);
	  }
	}

	if(bn->bn_type == FA_DIR) {

	  type = fa_probe_dir(&bn->bn_ftags, bn->bn_url);

	  if(type == FA_NONE) {
	    glw_destroy(bn->bn_icon_xfader);
	  } else {
	    bn->bn_type = type;
	    browser_view_node_model_update(bn);
	  }
	}

	pthread_mutex_unlock(&bn->bn_ftags_mutex);
      }

      browser_node_deref(bn);
      pthread_mutex_lock(&br->br_probe_mutex);
      continue;
    }


    if((bn = TAILQ_FIRST(&br->br_autoview_queue)) != NULL) {
      TAILQ_REMOVE(&br->br_autoview_queue, bn, bn_autoview_link);
      pthread_mutex_unlock(&br->br_probe_mutex);

      probe_figure_primary_content(br, bn);

      browser_node_deref(bn);
      pthread_mutex_lock(&br->br_probe_mutex);
      continue;
    }
    pthread_cond_wait(&br->br_probe_cond, &br->br_probe_mutex);
  }

  /* Clear the probe queue */

  while((bn = TAILQ_FIRST(&br->br_probe_queue)) != NULL) {
    TAILQ_REMOVE(&br->br_probe_queue, bn, bn_probe_link);
    browser_node_deref(bn);
  }

  while((bn = TAILQ_FIRST(&br->br_autoview_queue)) != NULL) {
    TAILQ_REMOVE(&br->br_autoview_queue, bn, bn_autoview_link);
    browser_node_deref(bn);
  }

  pthread_mutex_unlock(&br->br_probe_mutex);
  return NULL;
}

/**
 * 
 */
static void
probe_figure_primary_content(browser_root_t *br, browser_node_t *bn)
{
  browser_node_t *c, **a;
  int cnt, contentcount[32], i;
  int64_t type;

  if(bn->bn_cont_xfader == NULL)
    return; /* not really safe */

  memset(contentcount, 0, sizeof(int) * 32);

  a = browser_get_array_of_childs(br, bn);

  for(cnt = 0; (c = a[cnt]) != NULL; cnt++) {

    pthread_mutex_lock(&c->bn_ftags_mutex);

    switch(c->bn_type) {
    case FA_DIR:
      contentcount[FILETYPE_DIR]++;
      break;
      
    case FA_FILE:
      if(!filetag_get_int(&c->bn_ftags, FTAG_FILETYPE, &type)) {
	type &= 31;
	contentcount[(int)type]++;
      }
      break;
    }

    pthread_mutex_unlock(&c->bn_ftags_mutex);
    browser_node_deref(c); /* 'c' may be free'd here */
  }
  free(a);

  if(cnt == 0) {
    browser_view_switch_by_name(bn, br->br_gfs, "empty");
    return;
  }

  for(i = 0; i < 32; i++)
    if(contentcount[i] > 3 * cnt / 4)
      break;

  switch(i) {
  case FILETYPE_IMAGE:
    browser_view_switch_by_name(bn, br->br_gfs, "images");
    break;
  }
}



/**
 *
 */
void
browser_probe_enqueue(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;

  browser_node_ref(bn);

  pthread_mutex_lock(&br->br_probe_mutex);
  TAILQ_INSERT_TAIL(&br->br_probe_queue, bn, bn_probe_link);
  pthread_cond_signal(&br->br_probe_cond);
  pthread_mutex_unlock(&br->br_probe_mutex);
}


/**
 *
 */
void
browser_probe_autoview_enqueue(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;

  browser_node_ref(bn);

  pthread_mutex_lock(&br->br_probe_mutex);
  TAILQ_INSERT_TAIL(&br->br_autoview_queue, bn, bn_autoview_link);
  pthread_cond_signal(&br->br_probe_cond);
  pthread_mutex_unlock(&br->br_probe_mutex);
}


/**
 *
 */
void
browser_probe_init(browser_root_t *br)
{
  br->br_probe_run = 1;
  TAILQ_INIT(&br->br_probe_queue);
  TAILQ_INIT(&br->br_autoview_queue);

  pthread_cond_init(&br->br_probe_cond, NULL);
  pthread_mutex_init(&br->br_probe_mutex, NULL);

  pthread_create(&br->br_probe_thread_id, NULL, browser_probe_thread, br);
}

/**
 *
 */
void
browser_probe_deinit(browser_root_t *br)
{
  br->br_probe_run = 0;
  pthread_cond_signal(&br->br_probe_cond);
  pthread_join(br->br_probe_thread_id, NULL);
}
