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

#include <libhts/htsatomic.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"
#include "browser_probe.h"
#include "media.h"


static void *browser_scandir_thread(void *arg);

static int browser_scandir0(browser_page_t *bp);

static void browser_scandir_callback(void *arg, const char *url,
				     const char *filename, int type);

/**
 *
 */
static void
browser_node_destroy(browser_node_t *bn)
{
  assert(bn->bn_cont_xfader == NULL);

  filetag_freelist(&bn->bn_ftags);
  free(bn->bn_url);
  glw_prop_destroy(bn->bn_prop_root);
  free(bn);
}


/**
 *
 */
void
browser_node_ref(browser_node_t *bn)
{
  atomic_add(&bn->bn_refcnt, 1);  
}



/**
 *
 */
void
browser_node_unref(browser_node_t *bn)
{
  if(atomic_add(&bn->bn_refcnt, -1) > 1)
    return;
  browser_node_destroy(bn);
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
 *
 */
browser_node_t *
browser_node_create(browser_page_t *bp, const char *url, int type)
{
  browser_node_t *bn;

  bn = calloc(1, sizeof(browser_node_t));
  bn->bn_prop_root = glw_prop_create(NULL, "media");
  /* One refcount is given to the caller, one for linkage with parent */

  bn->bn_refcnt = 2;
  bn->bn_url = strdup(url);
  bn->bn_type = type;
  
  TAILQ_INSERT_TAIL(&bp->bp_childs, bn, bn_page_link);

  TAILQ_INIT(&bn->bn_ftags);
  hts_mutex_init(&bn->bn_ftags_mutex);

  browser_node_update_props(bn);
  browser_view_add_node(bn, NULL, 0, 0);

  return bn;
}




/**
 * Start scanning of a node (which more or less has to be a directory 
 * for this to work, but we expect the caller to know about that).
 *
 * We have the option to spawn a new thread to make this quick and fast
 */
int
browser_scandir(browser_page_t *bp, int async)
{
  hts_thread_t ptid;

  if(!async)
    return browser_scandir0(bp);

  hts_thread_create_detached(&ptid, browser_scandir_thread, bp);
  return 0;
}

/**
 *
 */
typedef struct browser_scanner_ctx {
  browser_page_t *bp;
  struct browser_node_list l;
} browser_scanner_ctx_t;

/**
 * Directory scanner guts
 *
 * Returns 0 if ok
 * Otherwise a system error code (errno)
 */
static int
browser_scandir0(browser_page_t *bp)
{
  int r = 0;
  const char *url;
  browser_scanner_ctx_t bsc;

#if 0
  if(bp->bp_type != FA_DIR)
    r = ENOTDIR;
#endif

  /*
   * Note: fileaccess_scandir() may take a long time to execute.
   * User may be prompted for username/password, etc
   */

  if(!r) {
    url = bp->bp_url;

    bsc.bp = bp;
    LIST_INIT(&bsc.l);
    r = fileaccess_scandir(url, browser_scandir_callback, &bsc);

    browser_probe_from_list(&bsc.l);
  }


  /**
   * Enqueue page on probe queue.
   *
   * When all files have been probed, the probe code will check all contents
   * and switch view if seems reasonable to do so
   */

  browser_probe_autoview_enqueue(bp);
  return r;
}  
    

/**
 * Directory scanner thread
 */
static void *
browser_scandir_thread(void *arg)
{
  browser_page_t *bp = arg;
  browser_scandir0(bp);
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

  c = browser_node_create(bsc->bp, url, type);
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
