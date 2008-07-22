/*
 *  Generic browser
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

#define _GNU_SOURCE
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"
#include "browser_probe.h"

static void *browser_scandir_thread(void *arg);

static int browser_scandir0(browser_node_t *bn);

static void browser_scandir_callback(void *arg, const char *url,
				     const char *filename, int type);

/**
 * check if we should free a node, br_hierarchy_mutex must be held
 */
static void
check_node_free(browser_node_t *bn)
{
  browser_node_t *p = bn->bn_parent;

  if(bn->bn_refcnt != 0 || TAILQ_FIRST(&bn->bn_childs) != NULL)
    return;

  filetag_freelist(&bn->bn_ftags);

  if(p != NULL) {
    TAILQ_REMOVE(&p->bn_childs, bn, bn_parent_link);
    check_node_free(p); /* removed from parent, need to check parent too */
  }

  assert(bn->bn_cont_xfader == NULL);

  free((void *)bn->bn_url);
  free(bn);
}


/**
 *
 */
void
browser_node_ref(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;

  pthread_mutex_lock(&br->br_hierarchy_mutex);
  bn->bn_refcnt++;
  pthread_mutex_unlock(&br->br_hierarchy_mutex);
}


/**
 *
 */
void
browser_node_deref(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;

  pthread_mutex_lock(&br->br_hierarchy_mutex);
  assert(bn->bn_refcnt > 0);
  bn->bn_refcnt--;

  check_node_free(bn);
  pthread_mutex_unlock(&br->br_hierarchy_mutex);
}


/**
 *
 */
static browser_node_t *
browser_node_create(const char *url, int type, browser_root_t *br)
{
  browser_node_t *bn;

  bn = calloc(1, sizeof(browser_node_t));
  bn->bn_refcnt = 1;
  bn->bn_url = strdup(url);
  bn->bn_type = type;
  bn->bn_root = br;
  TAILQ_INIT(&bn->bn_childs);
  TAILQ_INIT(&bn->bn_ftags);
  pthread_mutex_init(&bn->bn_ftags_mutex, NULL);

  return bn;
}




/**
 * Create a new node
 *
 * We obtain a reference for the caller, (ie caller must release it when done)
 */
browser_node_t *
browser_node_add_child(browser_node_t *parent, const char *url, int type)
{
  browser_root_t *br = parent->bn_root;
  browser_node_t *bn = browser_node_create(url, type, br);

  pthread_mutex_lock(&br->br_hierarchy_mutex);
  TAILQ_INSERT_TAIL(&parent->bn_childs, bn, bn_parent_link);
  bn->bn_parent = parent;
  pthread_mutex_unlock(&br->br_hierarchy_mutex);

  browser_view_add_node(bn, NULL, 0, 0);
  return bn;
}


/**
 * Create a new browser root
 */
browser_root_t *
browser_root_create(const char *url, glw_t *splashcontainer)
{
  browser_root_t *br = calloc(1, sizeof(browser_root_t));
  browser_node_t *bn = browser_node_create(url, FA_DIR, br);

  pthread_mutex_init(&br->br_hierarchy_mutex, NULL);

  browser_probe_init(br);
  br->br_splashcontainer = splashcontainer;
  br->br_root = bn;
  return br;
}

/**
 *
 */
void
browser_root_destroy(browser_root_t *br)
{
  browser_node_t *bn = br->br_root;
  browser_probe_deinit(br);

  assert(bn->bn_refcnt == 1); /* refcount should be 1 now */
  browser_node_deref(bn);
  free(br);
}



/**
 * Start scanning of a node (which more or less has to be a directory 
 * for this to work, but we expect the caller to know about that).
 *
 * We have the option to spawn a new thread to make this quick and fast
 */
int
browser_scandir(browser_node_t *bn, int async)
{
  pthread_t ptid;
  pthread_attr_t attr;

  if(!async)
    return browser_scandir0(bn);

  browser_node_ref(bn);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, &attr, browser_scandir_thread, bn);
  return 0;
}

/**
 * Directory scanner guts
 *
 * Returns 0 if ok
 * Otherwise a system error code (errno)
 */
static int
browser_scandir0(browser_node_t *bn)
{
  int r = 0;
  const char *url;

  if(bn->bn_type != FA_DIR)
    r = ENOTDIR;

  /*
   * Note: fileaccess_scandir() may take a long time to execute.
   * User may be prompted for username/password, etc
   */

  if(!r) {
    url = filetag_get_str2(&bn->bn_ftags, FTAG_URL) ?: bn->bn_url;
    r = fileaccess_scandir(url, browser_scandir_callback, bn);
  }

  /**
   * Enqueue directory on probe queue.
   *
   * When all files have been probed, the probe code will check all contents
   * and switch view if seems reasonable to do so
   */

  browser_probe_autoview_enqueue(bn);
  return r;
}  
    

/**
 * Directory scanner thread
 */
static void *
browser_scandir_thread(void *arg)
{
  browser_node_t *bn = arg;
  browser_scandir0(bn);
  browser_node_deref(bn);
  return NULL;
}


/**
 * Browser scandir callback for adding a new node
 */
static void
browser_scandir_callback(void *arg, const char *url, const char *filename, 
			 int type)
{
  browser_node_t *bn = arg, *c;

  if(!strcasecmp(filename, "thumbs.db"))
    return;

  c = browser_node_add_child(bn, url, type);

  browser_probe_enqueue(c);

  browser_node_deref(c);
}
			 

/**
 * Return an array of pointers to childs based on a parent node.
 *
 * We use this to avoid having to lock 'br_hierarchy_mutex' for extended times.
 *
 * Each child will have its reference count increased by one.
 *
 * Array is NULL terminated.
 */

browser_node_t **
browser_get_array_of_childs(browser_root_t *br, browser_node_t *bn)
{
  int cnt = 0;
  browser_node_t *c, **r;

  pthread_mutex_lock(&br->br_hierarchy_mutex);

  TAILQ_FOREACH(c, &bn->bn_childs, bn_parent_link)
    cnt++;
  
  r = malloc(sizeof(browser_node_t *) * (cnt + 1));

  cnt = 0;
  TAILQ_FOREACH(c, &bn->bn_childs, bn_parent_link) {
    c->bn_refcnt++;
    r[cnt++] = c;
  }
  r[cnt] = NULL;
  pthread_mutex_unlock(&br->br_hierarchy_mutex);
  return r;
}
