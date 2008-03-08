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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"
#include "browser_probe.h"

/**
 * check if we should free a node, br_hierarchy_mutex must be held
 */
static void
check_node_free(browser_node_t *bn)
{
  browser_node_t *p = bn->bn_parent;

  if(bn->bn_refcnt != 0 || TAILQ_FIRST(&bn->bn_childs) != NULL)
    return;

  printf("freeing %s\n", bn->bn_url);

  filetag_freelist(&bn->bn_ftags);

  TAILQ_REMOVE(&p->bn_childs, bn, bn_parent_link);
  check_node_free(p); /* removed from parent, need to check parent too */

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
  printf("deref %s, cnt before = %d\n", bn->bn_url, bn->bn_refcnt);

  assert(bn->bn_refcnt > 0);
  bn->bn_refcnt--;

  check_node_free(bn);
  pthread_mutex_unlock(&br->br_hierarchy_mutex);
}


/**
 *
 */
static browser_node_t *
browser_node_create(const char *url, browser_protocol_t *proto, int type,
		    browser_root_t *br)
{
  browser_node_t *bn;

  bn = calloc(1, sizeof(browser_node_t));
  bn->bn_refcnt = 1;
  bn->bn_url = strdup(url);
  bn->bn_type = type;
  bn->bn_protocol = proto;
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
  browser_node_t *bn = browser_node_create(url, parent->bn_protocol, type, br);

  pthread_mutex_lock(&br->br_hierarchy_mutex);
  TAILQ_INSERT_TAIL(&parent->bn_childs, bn, bn_parent_link);
  bn->bn_parent = parent;
  pthread_mutex_unlock(&br->br_hierarchy_mutex);

  browser_view_add_node(bn);
  return bn;
}


/**
 * Create a new browser root
 */
browser_root_t *
browser_root_create(const char *url, browser_protocol_t *proto)
{
  browser_root_t *br = calloc(1, sizeof(browser_root_t));
  browser_node_t *bn = browser_node_create(url, proto, BN_DIR, br);

  pthread_mutex_init(&br->br_hierarchy_mutex, NULL);

  browser_probe_init(br);

  br->br_root = bn;
  return br;
}
