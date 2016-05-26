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
#include "prop_nodefilter.h"
#include "misc/str.h"
#include "misc/redblack.h"

#define MAX_SORT_KEYS 4

TAILQ_HEAD(nfnode_queue, nfnode);
LIST_HEAD(nfn_pred_list, nfn_pred);
LIST_HEAD(prop_nf_pred_list, prop_nf_pred);
RB_HEAD(nfnode_tree, nfnode);


/**
 *
 */
typedef struct sortmap {
  char *str;
  int val;
} sortmap_t;

/**
 *
 */
typedef struct nfn_pred {
  LIST_ENTRY(nfn_pred) nfnp_link;
  prop_sub_t *nfnp_sub;
  struct prop_nf_pred *nfnp_conf;
  struct nfnode *nfnp_nfn;
  char nfnp_set;
} nfn_pred_t;


/**
 *
 */
typedef struct nfnode {
  TAILQ_ENTRY(nfnode) in_link;
  union {
    TAILQ_ENTRY(nfnode) out_queue_link;
    RB_ENTRY(nfnode) out_tree_link;
  };

  prop_t *in;
  prop_t *out;

  prop_sub_t *multisub;

  struct nfn_pred_list preds;

  struct prop_nf *nf;
  char inserted:1;
  char sortkey_type[MAX_SORT_KEYS];

#define SORTKEY_NONE  0
#define SORTKEY_RSTR  1
#define SORTKEY_INT   2
#define SORTKEY_FLOAT 3
#define SORTKEY_CSTR  4
#define SORTKEY_VOID  5

  prop_sub_t *sortsub[MAX_SORT_KEYS];

  union {
    rstr_t *rstr;
    const char *cstr;
    int i;
    float f;
  } sk[MAX_SORT_KEYS];
} nfnode_t;


/**
 *
 */
typedef struct prop_nf_pred {
  LIST_ENTRY(prop_nf_pred) pnp_link;

  char **pnp_path;
  prop_nf_cmp_t pnp_cf;
  prop_nf_mode_t pnp_mode;
  prop_sub_t *pnp_enable_sub;
  int pnp_id;

  char pnp_enabled;

  char *pnp_str;
  int pnp_int;

  struct prop_nf *pnp_nf;
} prop_nf_pred_t;


/**
 *
 */
typedef struct prop_nf {

  int pnf_refcount;
  int flags;

  int pred_tally;

  int nodecount;
  int sorted;
  prop_t *src;
  prop_t *dst;
  prop_sub_t *srcsub;
  prop_sub_t *dstsub;

  prop_sub_t *filtersub;

  struct nfnode_queue in;
  struct nfnode_queue out_queue;
  struct nfnode_tree out_tree;

  char *filter;

  char *sortkey[MAX_SORT_KEYS];
  sortmap_t *sortmap[MAX_SORT_KEYS];


  struct prop_nf_pred_list preds;

  char pending_have_more;

  int sortorder[MAX_SORT_KEYS];
  int sort_hide_on_missing[MAX_SORT_KEYS];

} prop_nf_t;


/**
 *
 */
static void
sortmap_free(sortmap_t *map)
{
  sortmap_t *m = map;
  if(m == NULL)
    return;
  for(;m->str != NULL; m++)
    free(m->str);
  free(map);
}


/**
 *
 */
static sortmap_t *
sortmap_create(const prop_nf_sort_strmap_t *src)
{
  const prop_nf_sort_strmap_t *s = src;
  int cnt = 0;
  for(;s->str != NULL; s++)
    cnt++;

  sortmap_t *m, *map = malloc((1 + cnt) * sizeof(sortmap_t));
  for(m = map, s = src; s->str != NULL; s++, m++) {
    m->str = strdup(s->str);
    m->val = s->val;
  }
  m->str = NULL;
  m->val = s->val;
  return map;
}


/**
 *
 */
static void
nfnp_destroy(nfn_pred_t *nfnp)
{
  LIST_REMOVE(nfnp, nfnp_link);
  prop_unsubscribe0(nfnp->nfnp_sub);
  free(nfnp);
}


/**
 * Evalue all predicates for a node. return 1 if the node should be filtered out
 */
static int
eval_preds(nfnode_t *nfn)
{
  nfn_pred_t *nfnp;
  prop_nf_pred_t *pnp;

  LIST_FOREACH(nfnp, &nfn->preds, nfnp_link) {
    if(!nfnp->nfnp_set)
      continue;

    pnp = nfnp->nfnp_conf;

    if(pnp->pnp_mode == PROP_NF_MODE_INCLUDE) {
      if(!pnp->pnp_enabled)
	return 1;
    } else {
      if(pnp->pnp_enabled)
	return 1;
    }
  }
  return 0;
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

  while(p->hp_originator != NULL)
    p = p->hp_originator;

  switch(p->hp_type) {
  case PROP_RSTRING:
    return filterstr(rstr_get(p->hp_rstring), q);

  case PROP_CSTRING:
    return filterstr(p->hp_cstring, q);

  case PROP_URI:
    return filterstr(rstr_get(p->hp_uri_title), q);

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
  int i;
  int r;
  for(i = 0; i < MAX_SORT_KEYS; i++) {
    if(a->nf->sortkey[i] == NULL)
      continue;

    if(a->sortkey_type[i] != b->sortkey_type[i])
      return a->sortkey_type[i] - b->sortkey_type[i];

    switch(a->sortkey_type[i]) {
    case SORTKEY_RSTR:
      r = dictcmp(rstr_get(a->sk[i].rstr), rstr_get(b->sk[i].rstr));
      break;

    case SORTKEY_CSTR:
      r = dictcmp(a->sk[i].cstr, b->sk[i].cstr);
      break;

    case SORTKEY_INT:
      r = a->sk[i].i - b->sk[i].i;
      break;

    case SORTKEY_FLOAT:
      if(a->sk[i].f < b->sk[i].f)
	r = -1;
      else if(a->sk[i].f > b->sk[i].f)
	r = 1;
      else
	r = 0;
      break;
    default:
      r = 0;
      break;
    }
    if(r)
      return r * a->nf->sortorder[i];
  }
  return a < b ? -1 : 1;
}




/**
 * Insert a node according to the sorting criteria
 * Optionally move the output node if it's created
 */
static void
nf_insert_node(prop_nf_t *nf, nfnode_t *nfn)
{
  nfnode_t *b;

  if(nfn->inserted) {

    if(nf->sorted) {
      RB_REMOVE(&nf->out_tree, nfn, out_tree_link);
    } else {
      TAILQ_REMOVE(&nf->out_queue, nfn, out_queue_link);
    }
  }

  nfn->inserted = 1;

  if(nf->sorted) {

    if(RB_INSERT_SORTED(&nf->out_tree, nfn, out_tree_link, nf_egress_cmp))
      abort();

  } else {

    b = TAILQ_NEXT(nfn, in_link);

    while(b != NULL && b->inserted == 0)
      b = TAILQ_NEXT(b, in_link);

    if(b != NULL) {
      TAILQ_INSERT_BEFORE(b, nfn, out_queue_link);
    } else {
      TAILQ_INSERT_TAIL(&nf->out_queue, nfn, out_queue_link);
    }
  }

  if(nfn->out == NULL)
    return;

  b = nfn;
  do {
    if(nf->sorted) {
      b = RB_NEXT(b, out_tree_link);
    } else {
      b = TAILQ_NEXT(b, out_queue_link);
    }
  } while(b != NULL && b->out == NULL);

  prop_move0(nfn->out, b ? b->out : NULL, nf->dstsub);
}


/**
 * Update node in egress properety tree
 */
static void
nf_update_egress(prop_nf_t *nf, nfnode_t *nfn)
{
  nfnode_t *b;
  int en = 1;
  int i;

  // If sorting is enabled but this node don't have a key, hide it
  for(i = 0; i < MAX_SORT_KEYS; i++)
    if(nf->sortkey[i] != NULL && nfn->sortkey_type[i] == SORTKEY_NONE &&
       nf->sort_hide_on_missing[i])
      en = 0;

  // Check filtering
  if(en && nf->filter != NULL && !nf_filtercheck(nfn->in, nf->filter))
    en = 0;

  if(eval_preds(nfn))
    en = 0;

  if(en != !nfn->out)
    return;

  if(en) {
    assert(nfn->out == NULL);
    nfn->out = prop_make(NULL, 0, NULL);
    prop_link0(nfn->in, nfn->out, NULL, 0, 0);

    b = nfn;
    do {
      if(nf->sorted) {
        b = RB_NEXT(b, out_tree_link);
      } else {
        b = TAILQ_NEXT(b, out_queue_link);
      }
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
  prop_nf_t *nf = nfn->nf;

  nf_update_egress(nf, nfn);
}


/**
 *
 */
static void
nf_update_multisub(prop_nf_t *nf, nfnode_t *nfn)
{
  if(!nf->filter == !nfn->multisub)
    return;

  if(nf->filter) {

    nfn->multisub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_MULTI | PROP_SUB_DONTLOCK,
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
nfnp_update_str(void *opaque, const char *str)
{
  nfn_pred_t *nfnp = opaque;
  prop_nf_pred_t *pnp = nfnp->nfnp_conf;
  nfnode_t *nfn = nfnp->nfnp_nfn;
  int s = 0;

  switch(pnp->pnp_cf) {
  case PROP_NF_CMP_EQ:
    s = !strcmp(str?:"", pnp->pnp_str);
    break;
  case PROP_NF_CMP_NEQ:
    s = !!strcmp(str?:"", pnp->pnp_str);
    break;
  }
  if(nfnp->nfnp_set == s)
    return;
  nfnp->nfnp_set = s;
  nf_update_egress(nfn->nf, nfn);
}


/**
 *
 */
static void
nfnp_update_int(void *opaque, int val)
{
  nfn_pred_t *nfnp = opaque;
  prop_nf_pred_t *pnp = nfnp->nfnp_conf;
  nfnode_t *nfn = nfnp->nfnp_nfn;

  int s = 0;

  switch(pnp->pnp_cf) {
  case PROP_NF_CMP_EQ:
    s = val == pnp->pnp_int;
    break;
  case PROP_NF_CMP_NEQ:
    s = val != pnp->pnp_int;
    break;
  }

  if(nfnp->nfnp_set == s)
    return;
  nfnp->nfnp_set = s;
  nf_update_egress(nfn->nf, nfn);
}


/**
 *
 */
static void
nfn_insert_pred(prop_nf_t *nf, nfnode_t *nfn, prop_nf_pred_t *pnp)
{
  nfn_pred_t *nfnp = calloc(1, sizeof(nfn_pred_t));

  nfnp->nfnp_conf = pnp;
  nfnp->nfnp_nfn = nfn;

  LIST_INSERT_HEAD(&nfn->preds, nfnp, nfnp_link);

  if(pnp->pnp_str != NULL) {
    nfnp->nfnp_sub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
		     PROP_TAG_CALLBACK_STRING, nfnp_update_str, nfnp,
		     PROP_TAG_NAMED_ROOT, nfn->in, "node",
		     PROP_TAG_NAME_VECTOR, pnp->pnp_path,
		     NULL);
  } else {
    nfnp->nfnp_sub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
		     PROP_TAG_CALLBACK_INT, nfnp_update_int, nfnp,
		     PROP_TAG_NAMED_ROOT, nfn->in, "node",
		     PROP_TAG_NAME_VECTOR, pnp->pnp_path,
		     NULL);
  }
}


/**
 *
 */
static void
nfn_insert_preds(prop_nf_t *nf, nfnode_t *nfn)
{
  prop_nf_pred_t *pnp;

  LIST_FOREACH(pnp, &nf->preds, pnp_link)
    nfn_insert_pred(nf, nfn, pnp);
}


static void
nf_set_sortkey_x(int x, nfnode_t *nfn, prop_event_t event, va_list ap)
{
  rstr_t *r;
  if(nfn->sortkey_type[x] == SORTKEY_RSTR)
    rstr_release(nfn->sk[x].rstr);

  switch(event) {
  case PROP_SET_RSTRING:
  case PROP_SET_URI:
    r = va_arg(ap, rstr_t *);
    const sortmap_t *map = nfn->nf->sortmap[x];
    if(map != NULL) {
      for(; map->str != NULL; map++) {
	if(!strcmp(map->str, rstr_get(r)))
	  break;
      }
      nfn->sk[x].i = map->val;
      nfn->sortkey_type[x] = SORTKEY_INT;
    } else {
      nfn->sk[x].rstr = rstr_dup(r);
      nfn->sortkey_type[x] = SORTKEY_RSTR;
    }
    break;

  case PROP_SET_INT:
    nfn->sk[x].i = va_arg(ap, int);
    nfn->sortkey_type[x] = SORTKEY_INT;
    break;

  case PROP_SET_FLOAT:
    nfn->sk[x].f = va_arg(ap, double);
    nfn->sortkey_type[x] = SORTKEY_FLOAT;
    break;

  default:
    nfn->sortkey_type[x] = SORTKEY_VOID;
    break;
  }
  nf_insert_node(nfn->nf, nfn);
  nf_update_egress(nfn->nf, nfn);
}

/**
 *
 */
static void
nf_set_sortkey_0(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  va_start(ap, event);
  nf_set_sortkey_x(0, opaque, event, ap);
  va_end(ap);
}


/**
 *
 */
static void
nf_set_sortkey_1(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  va_start(ap, event);
  nf_set_sortkey_x(1, opaque, event, ap);
  va_end(ap);
}


/**
 *
 */
static void
nf_set_sortkey_2(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  va_start(ap, event);
  nf_set_sortkey_x(2, opaque, event, ap);
  va_end(ap);
}


/**
 *
 */
static void
nf_set_sortkey_3(void *opaque, prop_event_t event, ...)
{
  va_list ap;

  va_start(ap, event);
  nf_set_sortkey_x(3, opaque, event, ap);
  va_end(ap);
}


/**
 *
 */
static void
nf_update_order_x(prop_nf_t *nf, nfnode_t *nfn, int x)
{
  if(nfn->sortsub[x]) {
    prop_unsubscribe0(nfn->sortsub[x]);
    nfn->sortsub[x] = NULL;
  }

  if(nf->sortkey[x] == NULL) {

    if(nfn->sortkey_type[x] == SORTKEY_RSTR)
      rstr_release(nfn->sk[x].rstr);
    nfn->sortkey_type[x] = SORTKEY_NONE;

    nf_insert_node(nf, nfn);

  } else {
    nfn->sortsub[x] =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK |
		     PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_CALLBACK,
		     x == 0 ? nf_set_sortkey_0 :
		     x == 1 ? nf_set_sortkey_1 :
		     x == 2 ? nf_set_sortkey_2 :
		     nf_set_sortkey_3, nfn,
		     PROP_TAG_NAMED_ROOT, nfn->in, "node",
		     PROP_TAG_NAMESTR, nf->sortkey[x],
		     NULL);
  }
}


static void
nf_update_order_all(prop_nf_t *nf, nfnode_t *nfn)
{
  int i;
  for(i = 0; i < MAX_SORT_KEYS; i++)
    nf_update_order_x(nf, nfn, i);
}


/**
 *
 */
static void
nf_add_node(prop_nf_t *nf, prop_t *node, nfnode_t *b)
{
  nfnode_t *nfn = calloc(1, sizeof(nfnode_t));

  prop_tag_set(node, nf, nfn);

  nf->nodecount++;

  if(b != NULL) {
    TAILQ_INSERT_BEFORE(b, nfn, in_link);
  } else {
    TAILQ_INSERT_TAIL(&nf->in, nfn, in_link);
  }

  nfn->nf = nf;
  nfn->in = node;

  nf_update_multisub(nf, nfn);
  nfn_insert_preds(nf, nfn);

  nf_update_order_all(nf, nfn);

  nf_update_egress(nf, nfn);
}


/**
 *
 */
static void
nf_add_nodes(prop_nf_t *nf, prop_vec_t *pv, nfnode_t *b)
{
  int i;
  nfnode_t *nfn;

  const int len = prop_vec_len(pv);
  nf->nodecount += len;
  for(i = 0; i < len; i++) {
    prop_t *p = prop_vec_get(pv, i);
    nfn = calloc(1, sizeof(nfnode_t));

    prop_tag_set(p, nf, nfn);

    if(b != NULL) {
      TAILQ_INSERT_BEFORE(b, nfn, in_link);
    } else {
      TAILQ_INSERT_TAIL(&nf->in, nfn, in_link);
    }

    nfn->nf = nf;
    nfn->in = p;

    nf_update_multisub(nf, nfn);
    nfn_insert_preds(nf, nfn);

    nf_update_order_all(nf, nfn);
    nf_update_egress(nf, nfn);
  }
}


/**
 *
 */
static void
nf_del_node(prop_nf_t *nf, nfnode_t *nfn)
{
  int i;
  nfn_pred_t *nfnp;

  nf->nodecount--;
  TAILQ_REMOVE(&nf->in, nfn, in_link);
  if(nf->sorted) {
    RB_REMOVE(&nf->out_tree, nfn, out_tree_link);
  } else {
    TAILQ_REMOVE(&nf->out_queue, nfn, out_queue_link);
  }

  if(nfn->out != NULL)
    prop_destroy0(nfn->out);

  if(nfn->multisub != NULL)
    prop_unsubscribe0(nfn->multisub);

  for(i = 0; i < MAX_SORT_KEYS; i++) {
    if(nfn->sortsub[i] != NULL)
      prop_unsubscribe0(nfn->sortsub[i]);
  }

  while((nfnp = LIST_FIRST(&nfn->preds)) != NULL)
    nfnp_destroy(nfnp);

  for(i = 0; i < MAX_SORT_KEYS; i++)
    if(nfn->sortkey_type[i] == SORTKEY_RSTR)
      rstr_release(nfn->sk[i].rstr);

  free(nfn);
}


/**
 *
 */
static void
nf_move_node(prop_nf_t *nf, nfnode_t *nfn, nfnode_t *b)
{
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
nf_find_node(prop_nf_t *nf, prop_t *node)
{
  if(node == NULL)
    return NULL;

  nfnode_t *nfn = prop_tag_get(node, nf);
  assert(nfn != NULL);
  return nfn;
}


/**
 *
 */
static void
nf_destroy_pred(struct prop_nf_pred *pnp)
{
  LIST_REMOVE(pnp, pnp_link);
  strvec_free(pnp->pnp_path);
  if(pnp->pnp_enable_sub != NULL)
    prop_unsubscribe0(pnp->pnp_enable_sub);
  free(pnp->pnp_str);
  free(pnp);
}


/**
 *
 */
static void
nf_destroy_preds(prop_nf_t *nf)
{
  struct prop_nf_pred *pnp;

  while((pnp = LIST_FIRST(&nf->preds)) != NULL)
    nf_destroy_pred(pnp);
}


/**
 *
 */
static void
nf_clear(prop_nf_t *nf)
{
  nfnode_t *nfn;

  while((nfn = TAILQ_FIRST(&nf->in)) != NULL) {
    prop_tag_clear(nfn->in, nf);
    nf_del_node(nf, nfn);
  }
}


/**
 *
 */
static void
prop_nf_release0(struct prop_nf *pnf)
{
  int i;

  pnf->pnf_refcount--;
  if(pnf->pnf_refcount > 0)
    return;

  if(pnf->srcsub != NULL)
    prop_unsubscribe0(pnf->srcsub);

  if(!(pnf->flags & PROP_NF_AUTODESTROY))
    nf_clear(pnf);

  prop_unsubscribe0(pnf->dstsub);
  if(pnf->dst != NULL)
    prop_destroy0(pnf->dst);

  assert(TAILQ_FIRST(&pnf->in) == NULL);
  if(pnf->sorted) {
    assert(pnf->out_tree.root == NULL);
  } else {
    assert(TAILQ_FIRST(&pnf->out_queue) == NULL);
  }
  if(pnf->filtersub != NULL)
    prop_unsubscribe0(pnf->filtersub);

  for(i = 0; i < MAX_SORT_KEYS; i++) {
    free(pnf->sortkey[i]);
    sortmap_free(pnf->sortmap[i]);
  }

  free(pnf->filter);

  nf_destroy_preds(pnf);
  free(pnf);
}


/**
 *
 */
static void
nf_suggest_focus(prop_nf_t *nf, nfnode_t *nfn)
{
  if(nfn->out != NULL)
    prop_suggest_focus0(nfn->out);
}



/**
 *
 */
static void
prop_nf_src_cb(void *opaque, prop_event_t event, ...)
{
  prop_nf_t *nf = opaque;
  nfnode_t *p, *q;
  prop_t *P;
  prop_vec_t *pv;

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

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    nf_add_nodes(nf, va_arg(ap, prop_vec_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    nf_add_nodes(nf, pv, nf_find_node(nf, va_arg(ap, prop_t *)));
    break;

  case PROP_DEL_CHILD:
    nf_del_node(nf, prop_tag_clear(va_arg(ap, prop_t *), nf));
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

  case PROP_REQ_DELETE_VECTOR:
  case PROP_REQ_DELETE:
    break;

  case PROP_DESTROYED:
    if(nf->srcsub != NULL)
      prop_unsubscribe0(nf->srcsub);
    nf->srcsub = NULL;
    prop_nf_release0(nf);
    break;

  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
    if(nf->filter == NULL)
      prop_have_more_childs0(nf->dst, event == PROP_HAVE_MORE_CHILDS_YES);
    else
      nf->pending_have_more = event;
    break;

  case PROP_WANT_MORE_CHILDS:
  case PROP_REQ_MOVE_CHILD:
  case PROP_SELECT_CHILD:
    break;

  case PROP_SUGGEST_FOCUS:
    nf_suggest_focus(nf, nf_find_node(nf, va_arg(ap, prop_t *)));
    break;

  default:
    printf("Unhandled event %d\n", event);
    abort();
  }
}


/**
 *
 */
static void
nf_translate_del_multi(prop_nf_t *nf, prop_vec_t *in)
{
  prop_t *p;
  int i, len = prop_vec_len(in);

  prop_vec_t *out = prop_vec_create(len);

  for(i = 0; i < len; i++) {
    p = prop_vec_get(in, i);
    while(p->hp_originator != NULL)
      p = p->hp_originator;
    out = prop_vec_append(out, p);
  }

  prop_notify_childv(out, nf->src, PROP_REQ_DELETE_VECTOR, nf->srcsub, NULL);
  prop_vec_release(out);
}



/**
 *
 */
static void
nf_translate_req_move_child(prop_nf_t *nf, prop_t *p, prop_t *before)
{
  if(nf->sortkey[0] || nf->sortkey[1] || nf->sortkey[2] || nf->sortkey[3])
    return;

  if(nf->filter != NULL)
    return;

  prop_nf_pred_t *pnp;
  LIST_FOREACH(pnp, &nf->preds, pnp_link)
    if(pnp->pnp_enabled)
      return;

  p = p->hp_originator;
  before = before ? before->hp_originator : NULL;
  prop_notify_child2(p, nf->src, before, PROP_REQ_MOVE_CHILD, nf->srcsub, 0);
}


/**
 *
 */
static void
prop_nf_dst_cb(void *opaque, prop_event_t event, ...)
{
  prop_nf_t *nf = opaque;
  prop_t *p1, *p2;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_REQ_DELETE_VECTOR:
    nf_translate_del_multi(nf, va_arg(ap, prop_vec_t *));
    break;

  case PROP_DESTROYED:
    abort();
    break;

  case PROP_REQ_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    nf_translate_req_move_child(nf, p1, p2);
    break;

  case PROP_WANT_MORE_CHILDS:
    if(nf->srcsub != NULL)
      prop_want_more_childs0(nf->srcsub);
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
  prop_nf_t *nf = opaque;
  nfnode_t *nfn;

  if(str != NULL && str[0] == 0)
    str = NULL;

  mystrset(&nf->filter, str);

  if(nf->filter == NULL && nf->pending_have_more) {
    prop_have_more_childs0(nf->dst,
                           nf->pending_have_more == PROP_HAVE_MORE_CHILDS_YES);
    nf->pending_have_more = 0;
  }


  TAILQ_FOREACH(nfn, &nf->in, in_link) {
    nf_update_multisub(nf, nfn);
    nf_update_egress(nf, nfn);
  }
}


/**
 *
 */
struct prop_nf *
prop_nf_create(prop_t *dst, prop_t *src, prop_t *filter, int flags)
{

  prop_nf_t *nf = calloc(1, sizeof(prop_nf_t));
  nf->flags = flags;
  TAILQ_INIT(&nf->in);
  TAILQ_INIT(&nf->out_queue);
  nf->pnf_refcount = 1 + (flags & PROP_NF_AUTODESTROY ? 1 : 0);

  nf->dst = flags & PROP_NF_TAKE_DST_OWNERSHIP ? dst : prop_xref_addref(dst);
  nf->src = src;

  hts_mutex_lock(&prop_mutex);

  if(filter != NULL)
    nf->filtersub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
				   PROP_TAG_CALLBACK_STRING, nf_set_filter, nf,
				   PROP_TAG_ROOT, filter,
				   NULL);

  nf->dstsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
			      PROP_TAG_CALLBACK, prop_nf_dst_cb, nf,
			      PROP_TAG_ROOT, nf->dst,
			      NULL);

  nf->srcsub = prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK |
			      (flags & PROP_NF_AUTODESTROY ?
			       PROP_SUB_TRACK_DESTROY : 0),
			      PROP_TAG_CALLBACK, prop_nf_src_cb, nf,
			      PROP_TAG_ROOT, src,
			      NULL);


  hts_mutex_unlock(&prop_mutex);

  return nf;
}


/**
 *
 */
void
prop_nf_release(struct prop_nf *pnf)
{
  hts_mutex_lock(&prop_mutex);
  prop_nf_release0(pnf);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
struct prop_nf *
prop_nf_retain(struct prop_nf *pnf)
{
  hts_mutex_lock(&prop_mutex);
  pnf->pnf_refcount++;
  hts_mutex_unlock(&prop_mutex);
  return pnf;
}


/**
 *
 */
static void
pnp_set_enable(void *opaque, int v)
{
  struct prop_nf_pred *pnp = opaque;
  prop_nf_t *nf = pnp->pnp_nf;
  nfnode_t *nfn;

  if(pnp->pnp_enabled == !!v)
    return;

  pnp->pnp_enabled = !!v;
  if(nf == NULL)
    return;

  TAILQ_FOREACH(nfn, &nf->in, in_link)
    nf_update_egress(nf, nfn);
}


/**
 *
 */
static int
prop_nf_pred_add(struct prop_nf *nf,
		 const char *path, prop_nf_cmp_t cf,
		 prop_t *enable,
		 prop_nf_mode_t mode,
		 struct prop_nf_pred *pnp)
{
  nfnode_t *nfn;

  pnp->pnp_id = ++nf->pred_tally;
  pnp->pnp_path = strvec_split(path, '.');
  pnp->pnp_cf = cf;
  pnp->pnp_mode = mode;
  pnp->pnp_nf = nf;
  LIST_INSERT_HEAD(&nf->preds, pnp, pnp_link);

  if(enable != NULL) {
    pnp->pnp_enable_sub =
      prop_subscribe(PROP_SUB_INTERNAL | PROP_SUB_DONTLOCK,
		     PROP_TAG_CALLBACK_INT, pnp_set_enable, pnp,
		     PROP_TAG_ROOT, enable,
		     NULL);
  } else {
    pnp->pnp_enabled = 1;
  }

  TAILQ_FOREACH(nfn, &nf->in, in_link)
    nfn_insert_pred(nf, nfn, pnp);
  return pnp->pnp_id;
}


/**
 *
 */
int
prop_nf_pred_str_add(struct prop_nf *nf,
		     const char *path, prop_nf_cmp_t cf,
		     const char *str, prop_t *enable,
		     prop_nf_mode_t mode)
{
  struct prop_nf_pred *pnp = calloc(1, sizeof(struct prop_nf_pred));
  pnp->pnp_str = strdup(str);
  hts_mutex_lock(&prop_mutex);
  int id = prop_nf_pred_add(nf, path, cf, enable, mode, pnp);
  hts_mutex_unlock(&prop_mutex);
  return id;
}


/**
 *
 */
int
prop_nf_pred_int_add(struct prop_nf *nf,
		     const char *path, prop_nf_cmp_t cf,
		     int value, prop_t *enable,
		     prop_nf_mode_t mode)
{
  struct prop_nf_pred *pnp = calloc(1, sizeof(struct prop_nf_pred));
  pnp->pnp_int = value;
  hts_mutex_lock(&prop_mutex);
  int id = prop_nf_pred_add(nf, path, cf, enable, mode, pnp);
  hts_mutex_unlock(&prop_mutex);
  return id;
}


/**
 *
 */
void
prop_nf_pred_remove(struct prop_nf *nf, int id)
{
  nfnode_t *nfn;
  prop_nf_pred_t *pnp;
  nfn_pred_t *nfnp;

  if(id == 0)
    return;

  hts_mutex_lock(&prop_mutex);
  LIST_FOREACH(pnp, &nf->preds, pnp_link)
    if(pnp->pnp_id == id)
      break;

  if(pnp != NULL) {
    TAILQ_FOREACH(nfn, &nf->in, in_link) {

      LIST_FOREACH(nfnp, &nfn->preds, nfnp_link)
	if(nfnp->nfnp_conf == pnp)
	  break;

      assert(nfnp != NULL);
      nfnp_destroy(nfnp);
      nf_update_egress(nf, nfn);
    }
    nf_destroy_pred(pnp);
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static int
chksorted(const struct prop_nf *nf)
{
  int i;
  for(i = 0; i < MAX_SORT_KEYS; i++)
    if(nf->sortkey[i])
      return 1;
  return 0;
}

/**
 *
 */
void
prop_nf_sort(struct prop_nf *nf, const char *path, int desc, unsigned int idx,
	     const prop_nf_sort_strmap_t *map, int hide_on_missing)
{
  nfnode_t *nfn;
  int m = desc ? -1 : 1;

  hts_mutex_lock(&prop_mutex);

  assert(idx < MAX_SORT_KEYS);

  if(nf->sortkey[idx]) {
    if(path && !strcmp(path, nf->sortkey[idx]) && nf->sortorder[idx] == m)
      goto done;
    free(nf->sortkey[idx]);
  } else {
    if(path == NULL)
      goto done;
  }

  sortmap_free(nf->sortmap[idx]);

  nf->sortmap[idx] = map ? sortmap_create(map) : NULL;

  if(path) {
    nf->sortkey[idx] = strdup(path);
    nf->sortorder[idx] = m;
    nf->sort_hide_on_missing[idx] = hide_on_missing;
  } else {
    nf->sortkey[idx] = NULL;
    nf->sortorder[idx] = 0;
    nf->sort_hide_on_missing[idx] = 0;
  }


  if(nf->sorted != chksorted(nf)) {
    // Filter switched to sorted mode
    TAILQ_FOREACH(nfn, &nf->in, in_link)
      nfn->inserted = 0;

    nf->sorted = !nf->sorted;
    if(nf->sorted) {
      RB_INIT(&nf->out_tree);
    } else {
      TAILQ_INIT(&nf->out_queue);
    }
  }

  TAILQ_FOREACH(nfn, &nf->in, in_link)
    nf_update_order_x(nf, nfn, idx);
 done:
  hts_mutex_unlock(&prop_mutex);
}
