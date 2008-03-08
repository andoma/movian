/*
 *  Browser interface
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

#ifndef BROWSER_H
#define BROWSER_H

#include <libglw/glw.h>

#include "fileprobe.h"
#include "hid/input.h"

TAILQ_HEAD(browser_node_queue, browser_node);
TAILQ_HEAD(browser_view_queue, browser_view);


/**
 * A browser view is one way of looking at all the childrens in a node.
 *
 * Ie, a 'directory' may have multiple views that the user can switch
 * between: "List, Icons, Photos, etc"
 */

typedef struct browser_view {
  TAILQ_ENTRY(browser_view) bv_link;
  

} browser_view_t;



/**
 * A browser node is anything that is browsable
 */
typedef struct browser_node {

  /* Hierarchy, these five are protected by top level br_hierarchy_mutex. */

  struct browser_root      *bn_root;
  struct browser_node_queue bn_childs;
  struct browser_node      *bn_parent;
  TAILQ_ENTRY(browser_node) bn_parent_link;
  int                       bn_refcnt;      /* Increase this if you
					       hold a reference to the
					       node without holding
					       the br_hierarchy_mutex */

  /* URL may never change after creation */
  const char               *bn_url;

  enum {
    BN_DIR,
    BN_FILE,
    BN_ARCHIVE,  /* An achive we may dive into and decode */
  } bn_type;

  struct browser_protocol  *bn_protocol;

  /* These fields must be access protected with glw_lock() */
  glw_t                    *bn_icon_xfader;
  glw_t                    *bn_cont_xfader;
  const char               *bn_view;        /* name of current view */
  /* end of glw_lock() */

  /* File tags (with an associated lock) */

  pthread_mutex_t           bn_ftags_mutex;
  struct filetag_list       bn_ftags;

  /* probe link and probe linked state is protected by root
     probemutex, for more info see browser_probe.[ch] */

  TAILQ_ENTRY(browser_node) bn_probe_link;
  int                       bn_probe_linked;

} browser_node_t;




/**
 * Top level browser struct
 */
typedef struct browser_root {
  pthread_mutex_t br_hierarchy_mutex;  /* Locks insertions and deletions
					  of childs in the entire tree.
					  This lock may not be held for any
					  significant time */

  browser_node_t *br_root;

  /* Probing */

  pthread_mutex_t br_probe_mutex;
  pthread_cond_t  br_probe_cond;
  pthread_t br_probe_thread_id;
  struct browser_node_queue br_probe_queue;

} browser_root_t;


/**
 * Control struct for operating on node
 */
typedef struct browser_protocol {
  void (*bp_scan)(browser_node_t *nb); /* Scan the node for childs */
#if 0
  browser_stream_t *(*bp_open)(const char *url);
  void (*bp_close)(browser_stream_t *bs);
  int (*bp_read)(browser_stream_t *bs, void *buf, size_t size);
  offset_t (*bp_seek)(browser_stream_t *bs, offset_t pos, int whence);
#endif

} browser_protocol_t;


void browser_node_ref(browser_node_t *bn);

void browser_node_deref(browser_node_t *bn);

browser_node_t *browser_node_add_child(browser_node_t *parent,
				       const char *url, int type);

browser_root_t *browser_root_create(const char *url,
				    browser_protocol_t *proto);

#endif /* BROWSER_H */
