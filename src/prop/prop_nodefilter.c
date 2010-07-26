/*
 *  Property trees
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

#include <arch/atomic.h>

#include "showtime.h"
#include "prop_i.h"
#include "misc/pixmap.h"
#include "misc/string.h"


TAILQ_HEAD(nfnode_queue, nfnode);


typedef struct nfnode {
  TAILQ_ENTRY(nfnode) in_link;
  TAILQ_ENTRY(nfnode) out_link;
  
  prop_t *in;
  prop_t *out;

  prop_sub_t *multisub;
  prop_sub_t *sortsub;
  prop_sub_t *enablesub;

  struct nodefilter *nf;
  int pos;
  char inserted;
  char disabled;

  rstr_t *sortkey;

} nfnode_t;


/**
 *
 */
typedef struct nodefilter {
  prop_t *src;
  prop_t *dst;
  prop_sub_t *srcsub;
  prop_sub_t *dstsub;

  prop_sub_t *filtersub;

  struct nfnode_queue in;
  struct nfnode_queue out;

  char *filter;


  int pos_valid;

  char **defsortpath;
  char **enablepath;

  int nativeorder;

} nodefilter_t;


/**
 *
 */
static void
nf_renumber(nodefilter_t *nf)
{
  int pos;
  nfnode_t *nfn;

  if(nf->pos_valid)
    return;

  nf->pos_valid = 1;
  pos = 0;
  TAILQ_FOREACH(nfn, &nf->in, in_link)
    nf->pos_valid = pos++;
}


/**
 *
 */
static int
filterstr(const char *s, const char *q)
{
  return !!mystrstr(s, q);
}


/**
 *
 */
static int
nf_filtercheck(prop_t *p, const char *q)
{
  prop_t *c;

  switch(p->hp_type) {
  case PROP_STRING:
    return filterstr(rstr_get(p->hp_rstring), q);

  case PROP_LINK:
    return filterstr(rstr_get(p->hp_link_rtitle), q);

  case PROP_DIR:
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      if(nf_filtercheck(c, q))
	return 1;
    break;
  default:
    break;
  }
  return 0;
}


/**
 *
 */
static int
nf_egress_cmp(const nfnode_t *a, const nfnode_t *b)
{
  const char *A = a->sortkey ? rstr_get(a->sortkey) : "";
  const char *B = b->sortkey ? rstr_get(b->sortkey) : "";

  int r = dictcmp(A, B);
  return r ? r : a->pos - b->pos;
}


/**
 * Insert a node according to the sorting criteria
 * Optionally move the output node if it's created
 */
static void
nf_insert_node(nodefilter_t *nf, nfnode_t *nfn)
{
  nfnode_t *b;

  if(nfn->inserted)
    TAILQ_REMOVE(&nf->out, nfn, out_link);

  nfn->inserted = 1;

  if(nfn->sortkey == NULL) {

    b = TAILQ_NEXT(nfn, in_link);

    if(b != NULL) {
      TAILQ_INSERT_BEFORE(b, nfn, out_link);
    } else {
      TAILQ_INSERT_TAIL(&nf->out, nfn, out_link);
    }

  } else {

    nf_renumber(nf);

    TAILQ_INSERT_SORTED(&nf->out, nfn, out_link, nf_egress_cmp);
  }

  if(nfn->out == NULL)
    return;

  b = nfn;
  do {
    b = TAILQ_NEXT(b, out_link);
  } while(b != NULL && b->out == NULL);

  prop_move0(nfn->out, b ? b->out : NULL, nf->dstsub);
}


/**
 * Update node in egress properety tree
 */
static void
nf_update_egress(nodefilter_t *nf, nfnode_t *nfn)
{
  nfnode_t *b;
  int en = 1;

  // If sorting is enabled but this node don't have a key, hide it
  if(nf->defsortpath != NULL && nfn->sortkey == NULL)
    en = 0;

  // Check filtering
  if(en && nf->filter != NULL && !nf_filtercheck(nfn->in, nf->filter))
    en = 0;

  if(nfn->disabled)
    en = 0;


  if(en != !nfn->out)
    return;

  if(en) {

    nfn->out = prop_create0(NULL, NULL, NULL, 0);
    prop_link0(nfn->in, nfn->out, NULL, 0);

    b = nfn;
    do {
      b = TAILQ_NEXT(b, out_link);
    } while(b != NULL && b->out == NULL);

    prop_set_parent0(nfn->out, nf->dst, b ? b->out : NULL, nf->dstsub);

  } else {

    prop_destroy0(nfn->out);
    nfn->out = NULL;
  }
}


/**
 *
 */
static void
nf_multi_filter(void *opaque, prop_event_t event, ...)
{
  nfnode_t *nfn = opaque;
  nodefilter_t *nf = nfn->nf;

  nf_update_egress(nf, nfn);
}


/**
 *
 */
static void
nf_update_multisub(nodefilter_t *nf, nfnode_t *nfn)
{
  if(!nf->filter == !nfn->multisub)
    return;

  if(nf->filter) {

    nfn->multisub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_MULTI | PROP_SUB_NOLOCK,
		     PROP_TAG_CALLBACK, nf_multi_filter, nfn,
		     PROP_TAG_ROOT, nfn->in,
		     NULL);

  } else {

    prop_unsubscribe0(nfn->multisub);
    nfn->multisub = NULL;
  }
}


/**
 *
 */
static void
nf_enable_filter(void *opaque, prop_event_t event, ...)
{
  nfnode_t *nfn = opaque;
  va_list ap;

  va_start(ap, event);

  switch(event) {
  case PROP_SET_RSTRING:
  case PROP_SET_RLINK:
    nfn->disabled = !atoi(rstr_get(va_arg(ap, rstr_t *)));
    break;

  case PROP_SET_INT:
    nfn->disabled = !va_arg(ap, int);
    break;

  case PROP_SET_FLOAT:
    nfn->disabled = !va_arg(ap, double);
    break;

  default:
    nfn->disabled = 1;
    break;
  }
  nf_update_egress(nfn->nf, nfn);
}


/**
 *
 */
static void
nf_update_enablesub(nodefilter_t *nf, nfnode_t *nfn)
{
  if(!nf->enablepath == !nfn->enablesub)
    return;

  if(nf->enablepath) {

    nfn->enablesub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_NOLOCK,
		     PROP_TAG_CALLBACK, nf_enable_filter, nfn,
		     PROP_TAG_NAMED_ROOT, nfn->in, "node",
		     PROP_TAG_NAME_VECTOR, nf->enablepath,
		     NULL);

  } else {

    prop_unsubscribe0(nfn->enablesub);
    nfn->enablesub = NULL;
  }
}


/**
 *
 */
static void
nf_set_sortkey(void *opaque, prop_event_t event, ...)
{
  nfnode_t *nfn = opaque;
  char buf[32];
  va_list ap;

  va_start(ap, event);
  rstr_release(nfn->sortkey);

  switch(event) {
  case PROP_SET_RSTRING:
  case PROP_SET_RLINK:
    nfn->sortkey = rstr_dup(va_arg(ap, rstr_t *));
    break;

  case PROP_SET_INT:
    snprintf(buf, sizeof(buf), "%d", va_arg(ap, int));
    nfn->sortkey = rstr_alloc(buf);
    break;

  case PROP_SET_FLOAT:
    snprintf(buf, sizeof(buf), "%f", va_arg(ap, double));
    nfn->sortkey = rstr_alloc(buf);
    break;

  default:
    nfn->sortkey = NULL;
    break;
  }
  nf_insert_node(nfn->nf, nfn);
}


/**
 *
 */
static void
nf_update_order(nodefilter_t *nf, nfnode_t *nfn)
{
  char **p = nf->defsortpath;

  if(nfn->sortsub) {
    prop_unsubscribe0(nfn->sortsub);
    nfn->sortsub = NULL;
  }

  if(p == NULL) {

    if(nfn->sortkey != NULL) {
      rstr_release(nfn->sortkey);
      nfn->sortkey = NULL;
    }

    nf_insert_node(nf, nfn);      

  } else {
    nfn->sortsub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_NOLOCK |
		     PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_CALLBACK, nf_set_sortkey, nfn,
		     PROP_TAG_NAMED_ROOT, nfn->in, "node",
		     PROP_TAG_NAME_VECTOR, p,
		     NULL);
  }
}


/**
 *
 */
static void
nf_add_node(nodefilter_t *nf, prop_t *node, nfnode_t *b)
{
  nfnode_t *nfn = calloc(1, sizeof(nfnode_t));

  if(b != NULL) {
    TAILQ_INSERT_BEFORE(b, nfn, in_link);
    nf->pos_valid = 0;

  } else {

    if(nf->pos_valid) {
      nfnode_t *l = TAILQ_LAST(&nf->in, nfnode_queue);
      nfn->pos = l ? l->pos + 1 : 0;
    }

    TAILQ_INSERT_TAIL(&nf->in, nfn, in_link);
  }

  nfn->nf = nf;
  nfn->in = node;

  nf_update_multisub(nf, nfn);
  nf_update_enablesub(nf, nfn);

  nf_update_order(nf, nfn);

  nf_update_egress(nf, nfn);
}


/**
 *
 */
static void
nf_del_node(nodefilter_t *nf, nfnode_t *nfn)
{
  nf->pos_valid = 0;
  TAILQ_REMOVE(&nf->in, nfn, in_link);
  TAILQ_REMOVE(&nf->out, nfn, out_link);

  if(nfn->out != NULL)
    prop_destroy0(nfn->out);

  if(nfn->multisub != NULL)
    prop_unsubscribe0(nfn->multisub);

  if(nfn->sortsub != NULL)
    prop_unsubscribe0(nfn->sortsub);

  if(nfn->enablesub != NULL)
    prop_unsubscribe0(nfn->enablesub);
  
  free(nfn);
}


/**
 *
 */
static void
nf_move_node(nodefilter_t *nf, nfnode_t *nfn, nfnode_t *b)
{
  nf->pos_valid = 0;

  TAILQ_REMOVE(&nf->in, nfn, in_link);

  if(b != NULL) {
    TAILQ_INSERT_BEFORE(b, nfn, in_link);
  } else {
    TAILQ_INSERT_TAIL(&nf->in, nfn, in_link);
  }
  nf_insert_node(nf, nfn);
}


/**
 *
 */
static nfnode_t *
nf_find_node(nodefilter_t *nf, prop_t *node)
{
  nfnode_t *nfn;

  TAILQ_FOREACH(nfn, &nf->in, in_link)
    if(nfn->in == node)
      return nfn;
  return NULL;
}


/**
 *
 */
static void
nf_maydestroy(nodefilter_t *nf)
{
  if(nf->srcsub != NULL || nf->dstsub != NULL)
    return;

  prop_unsubscribe0(nf->filtersub);

  if(nf->defsortpath)
    strvec_free(nf->defsortpath);

  if(nf->enablepath)
    strvec_free(nf->enablepath);

  free(nf->filter);

  free(nf);
}

/**
 *
 */
static void
nf_clear(nodefilter_t *nf)
{
  nfnode_t *nfn;

  while((nfn = TAILQ_FIRST(&nf->in)) != NULL)
    nf_del_node(nf, nfn);
  nf->pos_valid = 1;
}


/**
 *
 */
static void
nodefilter_src_cb(void *opaque, prop_event_t event, ...)
{
  nodefilter_t *nf = opaque;
  nfnode_t *p, *q;
  prop_t *P;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    nf_add_node(nf, va_arg(ap, prop_t *), NULL);
    break;
  
  case PROP_ADD_CHILD_BEFORE:
    P = va_arg(ap, prop_t *);
    nf_add_node(nf, P, nf_find_node(nf, va_arg(ap, prop_t *)));
    break;

  case PROP_DEL_CHILD:
    nf_del_node(nf, nf_find_node(nf, va_arg(ap, prop_t *)));
    break;

  case PROP_MOVE_CHILD:
    p = nf_find_node(nf, va_arg(ap, prop_t *));
    q = nf_find_node(nf, va_arg(ap, prop_t *));
    nf_move_node(nf, p, q);
    break;

  case PROP_SET_DIR:
    break;

  case PROP_SET_VOID:
    nf_clear(nf);
    break;

  case PROP_REQ_DELETE_MULTI:
    break;

  case PROP_DESTROYED:
    assert(TAILQ_FIRST(&nf->in) == NULL);
    prop_unsubscribe0(nf->srcsub);
    nf->srcsub = NULL;
    nf_maydestroy(nf);
    break;
    
  default:
    abort();
  }
}


/**
 *
 */
static void
nf_translate_del_multi(nodefilter_t *nf, prop_t **pv)
{
  prop_t *p;
  int i, len = prop_pvec_len(pv);

  for(i = 0; i < len; i++) {
    p = pv[i];
    while(p->hp_originator != NULL)
      p = p->hp_originator;
    prop_ref_inc(p);
    prop_ref_dec(pv[i]);
    pv[i] = p;
  }

  prop_notify_childv(pv, nf->src, PROP_REQ_DELETE_MULTI, nf->srcsub);

}

/**
 *
 */
static void
nodefilter_dst_cb(void *opaque, prop_event_t event, ...)
{
  nodefilter_t *nf = opaque;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_REQ_DELETE_MULTI:
    nf_translate_del_multi(nf, va_arg(ap, prop_t **));
    break;

  case PROP_DESTROYED:
    assert(TAILQ_FIRST(&nf->out) == NULL);
    prop_unsubscribe0(nf->dstsub);
    nf->dstsub = NULL;
    nf_maydestroy(nf);
    break;

  default:
    break;
  }
}


/**
 *
 */
static void
nf_set_filter(void *opaque, const char *str)
{
  nodefilter_t *nf = opaque;
  nfnode_t *nfn;

  if(str != NULL && str[0] == 0)
    str = NULL;

  mystrset(&nf->filter, str);

  TAILQ_FOREACH(nfn, &nf->in, in_link) {
    nf_update_multisub(nf, nfn);
    nf_update_egress(nf, nfn);
  }
}



/**
 *
 */
void
prop_make_nodefilter(prop_t *dst, prop_t *src, prop_t *filter, 
		     const char *defsortpath, const char *enablepath)
{
  nodefilter_t *nf = calloc(1, sizeof(nodefilter_t));

  TAILQ_INIT(&nf->in);
  TAILQ_INIT(&nf->out);

  nf->dst = dst;
  nf->src = src;

  nf->defsortpath = defsortpath ? strvec_split(defsortpath, '.') : NULL;
  nf->enablepath  = enablepath  ? strvec_split(enablepath , '.') : NULL;

  nf->filtersub = prop_subscribe(PROP_SUB_INTERNAL,
				 PROP_TAG_CALLBACK_STRING, nf_set_filter, nf,
				 PROP_TAG_ROOT, filter,
				 NULL);

  nf->dstsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_TRACK_DESTROY,
			      PROP_TAG_CALLBACK, nodefilter_dst_cb, nf,
			      PROP_TAG_ROOT, dst,
			      NULL);

  nf->srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_TRACK_DESTROY,
			      PROP_TAG_CALLBACK, nodefilter_src_cb, nf,
			      PROP_TAG_ROOT, src,
			      NULL);


  prop_ref_inc(dst);
  prop_ref_inc(src);

}









