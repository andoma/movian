/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

#include "arch/atomic.h"

#include "main.h"
#include "prop_i.h"
#include "prop_grouper.h"
#include "misc/str.h"

LIST_HEAD(pg_node_list, pg_node);
LIST_HEAD(pg_group_list, pg_group);

/**
 *
 */
typedef struct pg_node {
  prop_t *pgn_in;
  prop_t *pgn_out;
  prop_sub_t *pgn_sub;

  struct prop_grouper *pgn_grouper;
  LIST_ENTRY(pg_node) pgn_grouper_link;

  struct pg_group *pgn_group;
  LIST_ENTRY(pg_node) pgn_group_link;

} pg_node_t;


/**
 *
 */
typedef struct pg_group {
  char *pgg_name;
  LIST_ENTRY(pg_group) pgg_link;
  prop_t *pgg_root;
  prop_t *pgg_nodes;
  struct pg_node_list pgg_entries;

} pg_group_t;


/**
 *
 */
struct prop_grouper {

  struct pg_node_list pg_nodes;

  struct pg_group_list pg_groups;

  char **pg_groupingpath;
  prop_sub_t *pg_srcsub;

  prop_t *pg_dst;

};


/**
 *
 */
static pg_group_t *
group_find(prop_grouper_t *pg, const char *name)
{
  pg_group_t *pgg;

  LIST_FOREACH(pgg, &pg->pg_groups, pgg_link)
    if(!strcmp(pgg->pgg_name, name))
      return pgg;
  pgg = calloc(1, sizeof(pg_group_t));
  LIST_INSERT_HEAD(&pg->pg_groups, pgg, pgg_link);
  pgg->pgg_name = strdup(name);
  pgg->pgg_root = prop_create0(pg->pg_dst, NULL, NULL, 0);
  prop_set_string_exl(prop_create0(pgg->pgg_root, "name", NULL, 0), 
		      NULL, name, PROP_STR_UTF8);
  pgg->pgg_nodes = prop_create0(pgg->pgg_root, "nodes", NULL, 0);
  return pgg;
}


/**
 *
 */
static void
group_destroy(pg_group_t *pgg)
{
  prop_destroy0(pgg->pgg_root);
  LIST_REMOVE(pgg, pgg_link);
  free(pgg->pgg_name);
  free(pgg);
}


/**
 *
 */
static void
node_unset(pg_node_t *pgn)
{
  if(pgn->pgn_out != NULL)
    prop_destroy0(pgn->pgn_out);

  if(pgn->pgn_group != NULL) {
    LIST_REMOVE(pgn, pgn_group_link);
    if(LIST_FIRST(&pgn->pgn_group->pgg_entries) == NULL)
      group_destroy(pgn->pgn_group);
  }
}


/**
 *
 */
static void
node_update_group(pg_node_t *pgn, const char *group)
{
  node_unset(pgn);

  if(group == NULL) {
    pgn->pgn_out = NULL;
    pgn->pgn_group = NULL;
    return;
  }

  pgn->pgn_group = group_find(pgn->pgn_grouper, group);
  LIST_INSERT_HEAD(&pgn->pgn_group->pgg_entries, pgn, pgn_group_link);

  pgn->pgn_out = prop_make(NULL, 0, NULL);
  prop_link0(pgn->pgn_in, pgn->pgn_out, NULL, 0, 0);

  prop_set_parent0(pgn->pgn_out, pgn->pgn_group->pgg_nodes, NULL, NULL);
}


/**
 *
 */
static void
node_set_group(void *opaque, prop_event_t event, ...)
{
  pg_node_t *pgn = opaque;
  char buf[32];
  va_list ap;

  va_start(ap, event);
  const char *group;

  switch(event) {
  case PROP_SET_RSTRING:
  case PROP_SET_URI:
    group = rstr_get(va_arg(ap, rstr_t *));
    break;

  case PROP_SET_CSTRING:
    group = va_arg(ap, const char *);
    break;

  case PROP_SET_INT:
    snprintf(buf, sizeof(buf), "%d", va_arg(ap, int));
    group = buf;
    break;

  case PROP_SET_FLOAT:
    snprintf(buf, sizeof(buf), "%f", va_arg(ap, double));
    group = buf;
    break;

  default:
    group = NULL;
    break;
  }
  node_update_group(pgn, group);
}


/**
 *
 */
static void
pg_add_node(prop_grouper_t *pg, prop_t *node)
{
  pg_node_t *pgn = calloc(1, sizeof(pg_node_t));
  LIST_INSERT_HEAD(&pg->pg_nodes, pgn, pgn_grouper_link);
  pgn->pgn_grouper = pg;
  prop_tag_set(node, pg, pgn);
  pgn->pgn_in = node;

  pgn->pgn_sub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
				PROP_TAG_CALLBACK, node_set_group, pgn,
				PROP_TAG_NAMED_ROOT, node, "node",
				PROP_TAG_NAME_VECTOR, pg->pg_groupingpath,
				NULL);
}


/**
 *
 */
static void
pg_add_nodes(prop_grouper_t *pg, prop_vec_t *pv)
{
  int i;
  for(i = 0; i < prop_vec_len(pv); i++)
    pg_add_node(pg, prop_vec_get(pv, i));
}


/**
 *
 */
static void
pg_del_node(prop_grouper_t *pg, pg_node_t *pgn)
{
  LIST_REMOVE(pgn, pgn_grouper_link);
  node_unset(pgn);
  prop_unsubscribe0(pgn->pgn_sub);
  free(pgn);
}


/**
 *
 */
static void
pg_clear(prop_grouper_t *pg)
{
  pg_node_t *pgn;

  while((pgn = LIST_FIRST(&pg->pg_nodes)) != NULL) {
    prop_tag_clear(pgn->pgn_in, pg);
    pg_del_node(pg, pgn);
  }
}


/**
 *
 */
static void
src_cb(void *opaque, prop_event_t event, ...)
{
  prop_grouper_t *pg = opaque;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    pg_add_node(pg, va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_BEFORE:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pg_add_nodes(pg, va_arg(ap, prop_vec_t *));
    break;

  case PROP_DEL_CHILD:
    pg_del_node(pg, prop_tag_clear(va_arg(ap, prop_t *), pg));
    break;


  case PROP_SET_VOID:
    pg_clear(pg);
    break;

  case PROP_REQ_DELETE_VECTOR:
  case PROP_WANT_MORE_CHILDS:
  case PROP_MOVE_CHILD:
  case PROP_SET_DIR:
  case PROP_REQ_DELETE:
    break;

  default:
    abort();
  }
}



/**
 *
 */
prop_grouper_t *
prop_grouper_create(prop_t *dst, prop_t *src, const char *groupkey,
		    int flags)
{
  prop_grouper_t *pg = calloc(1, sizeof(prop_grouper_t));

  pg->pg_dst = flags & PROP_GROUPER_TAKE_DST_OWNERSHIP
    ? dst : prop_xref_addref(dst);

  pg->pg_groupingpath = strvec_split(groupkey, '.');

  hts_mutex_lock(&prop_mutex);

  pg->pg_srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
				 PROP_TAG_CALLBACK, src_cb, pg,
				 PROP_TAG_ROOT, src,
				 NULL);
  hts_mutex_unlock(&prop_mutex);
  return pg;
}



/**
 *
 */
void
prop_grouper_destroy(prop_grouper_t *pg)
{
  hts_mutex_lock(&prop_mutex);

  pg_clear(pg);
  prop_unsubscribe0(pg->pg_srcsub);
  prop_destroy0(pg->pg_dst);

  assert(LIST_FIRST(&pg->pg_nodes) == NULL);
  assert(LIST_FIRST(&pg->pg_groups) == NULL);
  hts_mutex_unlock(&prop_mutex);

  strvec_free(pg->pg_groupingpath);
  free(pg);
}
