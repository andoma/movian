/*
 *  Property concat
 *  Copyright (C) 2011 Andreas Öman
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
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#include "prop_i.h"
#include "prop_concat.h"

TAILQ_HEAD(prop_concat_source_queue, prop_concat_source);

typedef struct prop_concat_source {
  prop_concat_t *pcs_pc;
  TAILQ_ENTRY(prop_concat_source) pcs_link;
  prop_sub_t *pcs_srcsub;

  prop_t *pcs_first;
  prop_t *pcs_header;

  int pcs_count;

} prop_concat_source_t;

struct prop_concat {
  struct prop_concat_source_queue pc_queue;
  prop_t *pc_dst;

};


/**
 *
 */
static prop_t *
find_next_out(prop_concat_source_t *pcs)
{
  while((pcs = TAILQ_NEXT(pcs, pcs_link)) != NULL) {
    if(pcs->pcs_first != NULL) {
      if(pcs->pcs_header != NULL)
	return pcs->pcs_header;
      return pcs->pcs_first;
    }
  }
  return NULL;
}

/**
 *
 */
static void
src_cb(void *opaque, prop_event_t event, ...)
{
  prop_concat_source_t *pcs = opaque;
  prop_concat_t *pc = pcs->pcs_pc;
  prop_t *p, *q, *out, *before;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    out = prop_create_root(NULL);
    prop_tag_set(p, pcs, out);
    prop_link0(p, out, NULL, 0);

    prop_set_parent0(out, pc->pc_dst, find_next_out(pcs), NULL);

    if(pcs->pcs_count == 0) {
      pcs->pcs_first = out;
      if(pcs->pcs_header != NULL)
	prop_set_parent0(pcs->pcs_header, pc->pc_dst, out, NULL);
    }
    pcs->pcs_count++;
    break;

  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    out = prop_create_root(NULL);
    prop_tag_set(p, pcs, out);
    prop_link0(p, out, NULL, 0);

    q = va_arg(ap, prop_t *);
    before = prop_tag_get(q, pcs);
    assert(before != NULL);
    prop_set_parent0(out, pc->pc_dst, before, NULL);
    
    pcs->pcs_count++;
    break;

    


  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    out = prop_tag_clear(p, pcs);
    before = TAILQ_NEXT(out, hp_parent_link);
    prop_destroy0(out);

    pcs->pcs_count--;
    if(pcs->pcs_count == 0) {
      pcs->pcs_first = NULL;
      if(pcs->pcs_header != NULL)
	prop_unparent0(pcs->pcs_header, NULL);
    } else if(pcs->pcs_first == out) {
      pcs->pcs_first = before;
    }

    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    q = va_arg(ap, prop_t *);
    prop_move0(prop_tag_get(p, pcs),
	       q != NULL ? prop_tag_get(q, pcs) : find_next_out(pcs), NULL);
    break;

  case PROP_SET_VOID:
    break;

  case PROP_SET_DIR:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_HAVE_MORE_CHILDS:
  case PROP_WANT_MORE_CHILDS:
    break;

  default:
    printf("Cant handle event %d\n", event);
    abort();
  }
}



/**
 *
 */
void
prop_concat_add_source(prop_concat_t *pc, prop_t *src, prop_t *header)
{
  prop_concat_source_t *pcs = calloc(1, sizeof(prop_concat_source_t));
  
  pcs->pcs_header = header;
  pcs->pcs_pc = pc;

  hts_mutex_lock(&prop_mutex);

  TAILQ_INSERT_TAIL(&pc->pc_queue, pcs, pcs_link);

  pcs->pcs_srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
				 PROP_TAG_CALLBACK, src_cb, pcs,
				 PROP_TAG_ROOT, src,
				 NULL);

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
prop_concat_t *
prop_concat_create(prop_t *dst, int flags)
{
  prop_concat_t *pc = calloc(1, sizeof(prop_concat_t));

  pc->pc_dst = flags & PROP_CONCAT_TAKE_DST_OWNERSHIP
    ? dst : prop_xref_addref(dst);
  TAILQ_INIT(&pc->pc_queue);
  return pc;
}

