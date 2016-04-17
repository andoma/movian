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
#include "prop_reorder.h"
#include "htsmsg/htsmsg_store.h"

/**
 *
 */
typedef struct prop_reorder {
  prop_t *pr_dst;
  prop_sub_t *pr_srcsub;
  prop_sub_t *pr_dstsub;
  htsmsg_t *pr_order;
  char *pr_store;
} prop_reorder_t;


/**
 *
 */
static const char *
get_id(const prop_t *p)
{
  while(p->hp_originator != NULL)
    p = p->hp_originator;
  return p->hp_name;
}


/**
 *
 */
static void
save_order(prop_reorder_t *pr)
{
  htsmsg_t *out = htsmsg_create_list();
  prop_t *p;

  if(pr->pr_dst->hp_type == PROP_DIR)
    TAILQ_FOREACH(p, &pr->pr_dst->hp_childs, hp_parent_link)
      htsmsg_add_str(out, NULL, get_id(p));

  htsmsg_store_save(out, pr->pr_store);

  if(pr->pr_order)
    htsmsg_release(pr->pr_order);
  pr->pr_order = out;
}


/**
 * This is soo slow but works OK for small datasets
 */
static prop_t *
get_before(prop_reorder_t *pr, const char *id)
{
  htsmsg_field_t *f;
  prop_t *p;

  HTSMSG_FOREACH(f, pr->pr_order)
    if(f->hmf_type == HMF_STR && !strcmp(id, f->hmf_str))
      break;

  if(f == NULL)
    return NULL;

  if(pr->pr_dst->hp_type != PROP_DIR)
    return NULL;

  for(f = TAILQ_NEXT(f, hmf_link); f != NULL; f = TAILQ_NEXT(f, hmf_link)) {
    if(f->hmf_type != HMF_STR)
      continue;
    TAILQ_FOREACH(p, &pr->pr_dst->hp_childs, hp_parent_link)
      if(!strcmp(get_id(p) ?: "", f->hmf_str))
	return p;
  }
  return NULL;
}




/**
 *
 */
static void
add_child(prop_reorder_t *pr, prop_t *p)
{
  prop_t *out = prop_make(NULL, 0, NULL);
  prop_tag_set(p, pr, out);
  prop_link0(p, out, NULL, 0, 0);
  prop_set_parent0(out, pr->pr_dst, get_before(pr, get_id(p)), NULL);
}


/**
 *
 */
static void
src_cb(void *opaque, prop_event_t event, ...)
{
  prop_reorder_t *pr = opaque;
  va_list ap;
  prop_t *p, *out;
  prop_vec_t *pv;
  int i;

  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    add_child(pr, va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      add_child(pr, prop_vec_get(pv, i));
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    out = prop_tag_clear(p, pr);
    prop_destroy0(out);
    break;

  case PROP_MOVE_CHILD:
    break;

  case PROP_SET_VOID:
    break;

  case PROP_SET_DIR:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_WANT_MORE_CHILDS:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_REQ_DELETE:
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
dst_cb(void *opaque, prop_event_t event, ...)
{
  prop_reorder_t *pr = opaque;
  va_list ap;
  prop_t *p, *before;

  va_start(ap, event);

  switch(event) {
  case PROP_REQ_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    before = va_arg(ap, prop_t *);
    prop_move0(p, before, pr->pr_dstsub);
    save_order(pr);
    break;

  default:
    break;
  }
}



/**
 *
 */
void
prop_reorder_create(prop_t *dst, prop_t *src, int flags, const char *id)
{
  prop_reorder_t *pr = calloc(1, sizeof(prop_reorder_t));

  pr->pr_order = htsmsg_store_load(id);
  if(pr->pr_order == NULL)
    pr->pr_order = htsmsg_create_list();

  pr->pr_store = strdup(id);
  pr->pr_dst = flags & PROP_REORDER_TAKE_DST_OWNERSHIP ?
    dst : prop_xref_addref(dst);

  hts_mutex_lock(&prop_mutex);

  pr->pr_srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK | 
				 PROP_SUB_TRACK_DESTROY,
				 PROP_TAG_CALLBACK, src_cb, pr,
				 PROP_TAG_ROOT, src,
				 NULL);

  pr->pr_dstsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK | 
				 PROP_SUB_TRACK_DESTROY,
				 PROP_TAG_CALLBACK, dst_cb, pr,
				 PROP_TAG_ROOT, dst,
				 NULL);

  hts_mutex_unlock(&prop_mutex);
}
