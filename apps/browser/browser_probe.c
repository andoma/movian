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

/**
 *
 */
static void *
browser_probe_thread(void *arg)
{
  browser_root_t *br = arg;
  browser_node_t *bn;


  while(1) {

    pthread_mutex_lock(&br->br_probe_mutex);
    while((bn = TAILQ_FIRST(&br->br_probe_queue)) == NULL && br->br_probe_run)
      pthread_cond_wait(&br->br_probe_cond, &br->br_probe_mutex);

    if(!br->br_probe_run)
      break;

    TAILQ_REMOVE(&br->br_probe_queue, bn, bn_probe_link);
    pthread_mutex_unlock(&br->br_probe_mutex);

    pthread_mutex_lock(&bn->bn_ftags_mutex);

    fa_probe(&bn->bn_ftags, bn->bn_url);

    browser_view_node_model_update(bn);

    pthread_mutex_unlock(&bn->bn_ftags_mutex);

    browser_node_deref(bn);
  }

  /* Clear the probe queue */

  while((bn = TAILQ_FIRST(&br->br_probe_queue)) != NULL) {
    TAILQ_REMOVE(&br->br_probe_queue, bn, bn_probe_link);
    browser_node_deref(bn);
  }

  pthread_mutex_unlock(&br->br_probe_mutex);
  return NULL;
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
browser_probe_init(browser_root_t *br)
{
  br->br_probe_run = 1;
  TAILQ_INIT(&br->br_probe_queue);

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
