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
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>

#include "prop_i.h"
#include "prop_concat.h"

TAILQ_HEAD(prop_concat_source_queue, prop_concat_source);


/**
 *
 */
typedef struct prop_concat_source {
  prop_concat_t *pcs_pc;
  TAILQ_ENTRY(prop_concat_source) pcs_link;
  prop_sub_t *pcs_srcsub;

  prop_t *pcs_first;
  prop_t *pcs_header;

  int pcs_count;
  int pcs_flags;

  int pcs_index;

} prop_concat_source_t;


/**
 *
 */
struct prop_concat {
  struct prop_concat_source_queue pc_queue;
  prop_t *pc_dst;
  prop_sub_t *pc_dstsub;
  int pc_index_tally;
  int pc_refcount;
};

/**
 *
 */
static void
prop_concat_release0(prop_concat_t *pc)
{
  pc->pc_refcount--;
  if(pc->pc_refcount > 0)
    return;

  prop_ref_dec_locked(pc->pc_dst);
  prop_unsubscribe0(pc->pc_dstsub);
  free(pc);
}

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
pcs_destroy(prop_concat_source_t *pcs)
{
  prop_concat_t *pc = pcs->pcs_pc;
  assert(pcs->pcs_count == 0);
  assert(pcs->pcs_first == NULL);
  TAILQ_REMOVE(&pc->pc_queue, pcs, pcs_link);
  prop_unsubscribe0(pcs->pcs_srcsub);
  if(pcs->pcs_header != NULL)
    prop_destroy0(pcs->pcs_header);
  free(pcs);
  prop_concat_release0(pc);
}


/**
 *
 */
static void
add_child(prop_concat_source_t *pcs, prop_concat_t *pc, prop_t *p)
{
  prop_t *out = prop_make(NULL, 0, NULL);
  prop_tag_set(p, pcs, out);
  prop_tag_set(out, pcs, p);
  prop_link0(p, out, NULL, 0, 0);

  prop_set_parent0(out, pc->pc_dst, find_next_out(pcs), NULL);

  if(pcs->pcs_count == 0) {
    pcs->pcs_first = out;
    if(pcs->pcs_header != NULL)
      prop_set_parent0(pcs->pcs_header, pc->pc_dst, out, NULL);
  }
  pcs->pcs_count++;
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
  prop_vec_t *pv;
  int i;
  va_list ap;


  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    add_child(pcs, pc, va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    out = prop_make(NULL, 0, NULL);
    prop_tag_set(p, pcs, out);
    prop_tag_set(out, pcs, p);
    prop_link0(p, out, NULL, 0, 0);

    q = va_arg(ap, prop_t *);
    before = prop_tag_get(q, pcs);
    assert(before != NULL);
    prop_set_parent0(out, pc->pc_dst, before, NULL);

    pcs->pcs_count++;
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      add_child(pcs, pc, prop_vec_get(pv, i));
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    out = prop_tag_clear(p, pcs);
    prop_tag_clear(out, pcs);
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
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_WANT_MORE_CHILDS:
  case PROP_REQ_DELETE:
    break;

  case PROP_DESTROYED:
    pcs_destroy(pcs);
    break;

  default:
    printf("Cant handle event %d\n", event);
    abort();
  }
}


/**
 *
 */
static void
req_move(prop_concat_t *pc, prop_t *p, prop_t *b)
{
  prop_concat_source_t *ps, *bs;

  TAILQ_FOREACH(ps, &pc->pc_queue, pcs_link) {
    prop_t *pp = prop_tag_get(p, ps);

    if(pp != NULL) {

      if(b == NULL) {
        prop_req_move0(pp, NULL, ps->pcs_srcsub);
        return;
      }

      prop_t *bb = NULL;
      TAILQ_FOREACH(bs, &pc->pc_queue, pcs_link) {

        if(b == bs->pcs_header) {
          bb = NULL;
          bs = TAILQ_PREV(bs, prop_concat_source_queue, pcs_link);
          break;
        }

        bb = prop_tag_get(b, bs);

        if(bb != NULL)
          break;
      }

      if(bs == NULL || bs->pcs_index < ps->pcs_index) {
        prop_req_move0(pp, TAILQ_FIRST(&pp->hp_parent->hp_childs), ps->pcs_srcsub);
        return;
      }

      if(bs->pcs_index > ps->pcs_index) {
        prop_req_move0(pp, NULL, ps->pcs_srcsub);
        return;
      }

      prop_req_move0(pp, bb, ps->pcs_srcsub);
      return;
    }
  }
}


/**
 *
 */
static void
dst_cb(void *opaque, prop_event_t event, ...)
{
  prop_concat_t *pc = opaque;
  va_list ap;
  prop_t *p1, *p2;
  va_start(ap, event);

  switch(event) {
  case PROP_REQ_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    req_move(pc, p1, p2);
    break;

  case PROP_DESTROYED:
    prop_concat_release0(pc);
    break;

  default:
    break;
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
  pc->pc_refcount++;
  TAILQ_INSERT_TAIL(&pc->pc_queue, pcs, pcs_link);

  pcs->pcs_srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK |
				   PROP_SUB_TRACK_DESTROY,
				   PROP_TAG_CALLBACK, src_cb, pcs,
				   PROP_TAG_ROOT, src,
				   NULL);

  pcs->pcs_index = pc->pc_index_tally++;

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
prop_concat_t *
prop_concat_create(prop_t *dst)
{
  prop_concat_t *pc = calloc(1, sizeof(prop_concat_t));

  pc->pc_dst = prop_ref_inc(dst);
  TAILQ_INIT(&pc->pc_queue);
  pc->pc_refcount = 2; // one for subscription, one for caller
  hts_mutex_lock(&prop_mutex);

  pc->pc_dstsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK |
                                 PROP_SUB_TRACK_DESTROY,
                                 PROP_TAG_CALLBACK, dst_cb, pc,
                                 PROP_TAG_ROOT, dst,
                                 NULL);
  hts_mutex_unlock(&prop_mutex);

  return pc;
}


/**
 *
 */
void
prop_concat_release(prop_concat_t *pc)
{
  hts_mutex_lock(&prop_mutex);
  prop_concat_release0(pc);
  hts_mutex_unlock(&prop_mutex);
}
