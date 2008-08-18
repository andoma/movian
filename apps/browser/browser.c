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
#include "media.h"


static void *browser_scandir_thread(void *arg);

static int browser_scandir0(browser_node_t *bn);

static void browser_scandir_callback(void *arg, const char *url,
				     const char *filename, int type);

/**
 * check if we should free a node, br_hierarchy_mutex must be held
 */
static int
check_node_free(browser_node_t *bn, int norootref)
{
  browser_node_t *p = bn->bn_parent;
  int decr = 0;

  if(bn->bn_refcnt != 0 || TAILQ_FIRST(&bn->bn_childs) != NULL)
    return 0;

  filetag_freelist(&bn->bn_ftags);

  if(p != NULL) {
    TAILQ_REMOVE(&p->bn_childs, bn, bn_parent_link);
    /* removed from parent, need to check parent too */
    decr = check_node_free(p, norootref);
  }

  assert(bn->bn_cont_xfader == NULL);

  free((void *)bn->bn_url);
  glw_prop_destroy(bn->bn_prop_root);
  free(bn);
  return decr + 1;
}


/**
 *
 */
void
browser_node_ref(browser_node_t *bn)
{
  browser_root_t *br = bn->bn_root;

  hts_mutex_lock(&br->br_mutex);
  bn->bn_refcnt++;
  hts_mutex_unlock(&br->br_mutex);
}



/**
 *
 */
void
browser_node_unref(browser_node_t *bn)
{
  int decr;
  browser_root_t *br = bn->bn_root;

  hts_mutex_lock(&br->br_mutex);

  assert(bn->bn_refcnt > 0);
  bn->bn_refcnt--;

  decr = check_node_free(bn, 0);

  br->br_refcnt -= decr;
  if(br->br_refcnt == 0) {
    /* Destruction of browser root */
    browser_probe_deinit(br);
    free(br);
    return;
  }

  hts_mutex_unlock(&br->br_mutex);
}


/**
 *
 */
static browser_node_t *
browser_node_create(const char *url, int type, browser_root_t *br)
{
  browser_node_t *bn;

  bn = calloc(1, sizeof(browser_node_t));
  bn->bn_prop_root = glw_prop_create(NULL, "media", GLW_GP_DIRECTORY);
  bn->bn_refcnt = 1;
  bn->bn_url = strdup(url);
  bn->bn_type = type;
  TAILQ_INIT(&bn->bn_childs);
  TAILQ_INIT(&bn->bn_ftags);
  hts_mutex_init(&bn->bn_ftags_mutex);

  return bn;
}



/**
 * Update properties
 */
void
browser_node_update_props(browser_node_t *bn)
{
  media_fill_properties(bn->bn_prop_root, bn->bn_url, bn->bn_type,
			&bn->bn_ftags);
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

  hts_mutex_lock(&br->br_mutex);
  
  bn->bn_root = br;
  br->br_refcnt++;

  TAILQ_INSERT_TAIL(&parent->bn_childs, bn, bn_parent_link);
  bn->bn_parent = parent;
  hts_mutex_unlock(&br->br_mutex);

  browser_node_update_props(bn);

  browser_view_add_node(bn, NULL, 0, 0);
  return bn;
}


/**
 * Create a new browser root
 */
browser_root_t *
browser_root_create(const char *url)
{
  browser_root_t *br = calloc(1, sizeof(browser_root_t));
  browser_node_t *bn = browser_node_create(url, FA_DIR, br);

  hts_mutex_init(&br->br_mutex);

  bn->bn_root = br;

  browser_probe_init(br);
  br->br_root = bn;
  br->br_refcnt = 1;
  return br;
}


/**
 *
 */
void
browser_root_destroy(browser_root_t *br)
{
  browser_node_unref(br->br_root);
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
  hts_thread_t ptid;

  if(!async)
    return browser_scandir0(bn);

  browser_node_ref(bn);

  hts_thread_create_detached(&ptid, browser_scandir_thread, bn);
  return 0;
}

/**
 *
 */
typedef struct browser_scanner_ctx {
  browser_node_t *bn;
  struct browser_node_list l;
} browser_scanner_ctx_t;

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
  browser_scanner_ctx_t bsc;
  browser_node_t *c;
  browser_root_t *br = bn->bn_root;

  if(bn->bn_type != FA_DIR)
    r = ENOTDIR;

  /*
   * Note: fileaccess_scandir() may take a long time to execute.
   * User may be prompted for username/password, etc
   */

  if(!r) {
    url = filetag_get_str2(&bn->bn_ftags, FTAG_URL) ?: bn->bn_url;

    bsc.bn = bn;
    LIST_INIT(&bsc.l);
    r = fileaccess_scandir(url, browser_scandir_callback, &bsc);

    hts_mutex_lock(&br->br_probe_mutex);

    while((c = LIST_FIRST(&bsc.l)) != NULL) {
      LIST_REMOVE(c, bn_probe_link);
      LIST_INSERT_HEAD(&br->br_probe_list, c, bn_probe_link);
    }
    hts_cond_signal(&br->br_probe_cond);
    hts_mutex_unlock(&br->br_probe_mutex);
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
  browser_node_unref(bn);
  return NULL;
}


/**
 * Browser scandir callback for adding a new node
 */
static void
browser_scandir_callback(void *arg, const char *url, const char *filename, 
			 int type)
{
  browser_scanner_ctx_t *bsc = arg;
  browser_node_t *c;

  if(!strcasecmp(filename, "thumbs.db"))
    return;

  c = browser_node_add_child(bsc->bn, url, type);
  LIST_INSERT_HEAD(&bsc->l, c, bn_probe_link);
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

  hts_mutex_lock(&br->br_mutex);

  TAILQ_FOREACH(c, &bn->bn_childs, bn_parent_link)
    cnt++;
  
  r = malloc(sizeof(browser_node_t *) * (cnt + 1));

  cnt = 0;
  TAILQ_FOREACH(c, &bn->bn_childs, bn_parent_link) {
    c->bn_refcnt++;
    r[cnt++] = c;
  }
  r[cnt] = NULL;
  hts_mutex_unlock(&br->br_mutex);
  return r;
}
