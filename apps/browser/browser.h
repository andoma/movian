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

#include "navigator.h"


#include <libglw/glw.h>
#include <fileaccess/fileaccess.h>
#include <fileaccess/fa_probe.h>

LIST_HEAD(browser_node_list,   browser_node);
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
  
  const char *bv_path;
  const char *bv_name;

  int bv_contentfilter;

} browser_view_t;


extern struct browser_view_queue browser_views;





/**
 * A browser node is anything that is browsable
 */
typedef struct browser_node {
  int                       bn_refcnt;

  TAILQ_ENTRY(browser_node) bn_page_link;

  /* URL may never change after creation */
  char                     *bn_url;

  int                       bn_type; /* FA_ -node type */

  glw_prop_t               *bn_prop_root;
  glw_t                    *bn_model;

  /* These fields must be access protected with glw_lock() */
  glw_t                    *bn_cont_xfader;
  glw_t                    *bn_cont_preview;
  browser_view_t           *bn_view;
  /* end of glw_lock() */

  /* File tags (with an associated lock) */

  hts_mutex_t           bn_ftags_mutex;
  struct filetag_list       bn_ftags;

  /* probe link and probe linked state is protected by global
     probemutex, for more info see browser_probe.[ch] */

  LIST_ENTRY(browser_node) bn_probe_link;
  TAILQ_ENTRY(browser_node) bn_autoview_link;

} browser_node_t;




/**
 * A browser page lists multiple browser nodes
 */
typedef struct browser_page {
  nav_page_t bp_np;
  char      *bp_url;

  struct browser_node_queue bp_childs;
} browser_page_t;



#if 0
/**
 * Top level browser struct
 */
typedef struct browser_root {
  hts_mutex_t br_mutex;  /* Locks insertions and deletions
			    of childs in the entire tree.
			    This lock may not be held for any
			    significant time.
			    The lock also protects modification of br_refcount
			 */

  int br_refcnt;

  browser_node_t *br_root;

  /* Probing */

  hts_mutex_t br_probe_mutex;
  hts_cond_t  br_probe_cond;
  hts_thread_t br_probe_thread_id;
  int br_probe_run;
  struct browser_node_list br_probe_list;
  struct browser_node_queue br_autoview_queue;

} browser_root_t;
#endif


void browser_node_ref(browser_node_t *bn);

void browser_node_unref(browser_node_t *bn);

browser_node_t *browser_node_create(browser_page_t *bp, 
				    const char *url, int type);

void browser_node_update_props(browser_node_t *bn);

int browser_scandir(browser_page_t *bp, int async);

browser_node_t **browser_get_array_of_childs(browser_node_t *bn);

void browser_slideshow(browser_node_t *cur, glw_t *parent);

#endif /* BROWSER_H */
