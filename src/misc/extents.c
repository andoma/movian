/*
 *  Extent allocator
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

#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <sys/queue.h>
#include "redblack.h"
#include "extents.h"


LIST_HEAD(extent_freeseg_list, extent_freeseg);
RB_HEAD(extent_freeseg_tree, extent_freeseg);
RB_HEAD(extent_size_tree, extent_size);

typedef struct extent_freeseg {
  RB_ENTRY(extent_freeseg) ef_link;
  LIST_ENTRY(extent_freeseg) ef_size_link;
  struct extent_size *ef_size;
  int ef_pos;
} extent_freeseg_t;

typedef struct extent_size {
  RB_ENTRY(extent_size) es_link;
  struct extent_freeseg_list es_freesegs;
  int es_size;
} extent_size_t;

struct extent_pool {
  struct extent_freeseg_tree ep_freesegs;
  struct extent_size_tree ep_sizes;
  extent_size_t *ep_size_skel;
  extent_freeseg_t *ep_freeseg_skel;
  int ep_total;
};

/**
 *
 */
static int
es_cmp(const extent_size_t *a, const extent_size_t *b)
{
  return a->es_size - b->es_size;
}


/**
 *
 */
static int
ef_cmp(const extent_freeseg_t *a, const extent_freeseg_t *b)
{
  return a->ef_pos - b->ef_pos;
}


/**
 *
 */
static void
ef_insert(extent_pool_t *ep, int size, extent_freeseg_t *ef)
{
  extent_size_t *es;
  if(ep->ep_size_skel == NULL)
    ep->ep_size_skel = malloc(sizeof(extent_size_t));

  ep->ep_size_skel->es_size = size;
  es = RB_INSERT_SORTED(&ep->ep_sizes, ep->ep_size_skel, es_link, es_cmp);
  if(es == NULL) {
    es = ep->ep_size_skel;
    ep->ep_size_skel = NULL;
    LIST_INIT(&es->es_freesegs);
  }

  ef->ef_size = es;
  LIST_INSERT_HEAD(&es->es_freesegs, ef, ef_size_link);
}


/**
 *
 */
static void
ef_unlink(extent_pool_t *ep, extent_freeseg_t *ef)
{
  extent_size_t *es = ef->ef_size;
  LIST_REMOVE(ef, ef_size_link);
  if(LIST_FIRST(&es->es_freesegs) == NULL) {
    RB_REMOVE(&ep->ep_sizes, es, es_link);
    free(es);
  }
}


/**
 *
 */
static void
ef_destroy(extent_pool_t *ep, extent_freeseg_t *ef)
{
  ef_unlink(ep, ef);
  RB_REMOVE(&ep->ep_freesegs, ef, ef_link);
  free(ef);
}


/**
 *
 */
static extent_size_t *
es_find(extent_pool_t *ep, int size)
{
  extent_size_t skel;
  skel.es_size = size;
  return RB_FIND_GE(&ep->ep_sizes, &skel, es_link, es_cmp);
}


/**
 *
 */
int
extent_alloc(extent_pool_t *ep, int size)
{
  extent_size_t *es;
  extent_freeseg_t *ef;
  int pos;

  if((es = es_find(ep, size)) == NULL)
    return -1;
  
  ef = LIST_FIRST(&es->es_freesegs);
  pos = ef->ef_pos;

  if(es->es_size == size) {
    ef_destroy(ep, ef);
  } else {
    ef->ef_pos += size;
    size = es->es_size - size;
    ef_unlink(ep, ef);
    ef_insert(ep, size, ef);
  }
  return pos;
}


/**
 *
 */
int
extent_free(extent_pool_t *ep, int pos, int size)
{
  extent_freeseg_t *ef, *p, *n;

  if(size == 0)
    return 0;

  if(ep->ep_freeseg_skel == NULL)
    ep->ep_freeseg_skel = malloc(sizeof(extent_freeseg_t));

  ep->ep_freeseg_skel->ef_pos = pos;
	
  if(RB_INSERT_SORTED(&ep->ep_freesegs, ep->ep_freeseg_skel,
		      ef_link, ef_cmp) != NULL)
    return -1;

  n = RB_NEXT(ep->ep_freeseg_skel, ef_link);
  p = RB_PREV(ep->ep_freeseg_skel, ef_link);

  if(p != NULL && p->ef_pos + p->ef_size->es_size > pos)
    return -2;

  if(n != NULL) {
    if(pos + size > n->ef_pos)
      return -3;
    if(n->ef_pos != pos + size)
      n = NULL;
  }

  if(p != NULL && p->ef_pos + p->ef_size->es_size == pos) {
    RB_REMOVE(&ep->ep_freesegs, ep->ep_freeseg_skel, ef_link);
    ef = p;
    size += ef->ef_size->es_size;
    ef_unlink(ep, ef);
  } else {
    ef = ep->ep_freeseg_skel;
    ep->ep_freeseg_skel = NULL;
  }

  if(n != NULL) {
    size += n->ef_size->es_size;
    ef_destroy(ep, n);
  }

  ef_insert(ep, size, ef);
  return 0;
}


/**
 *
 */
int
extent_alloc_aligned(extent_pool_t *ep, int size, int alignment)
{
  int rval, pos;
  extent_size_t *es;
  extent_freeseg_t *ef;

  if(alignment < 2)
    return extent_alloc(ep, size);

  alignment--;

  if((es = es_find(ep, size + alignment)) == NULL)
    return -1;
  
  ef = LIST_FIRST(&es->es_freesegs);
  pos = ef->ef_pos;

  rval = (pos + alignment) & ~alignment;

  const int size2 = ef->ef_pos + es->es_size - (rval + size);
  ef->ef_pos = rval + size;
  ef_unlink(ep, ef);
  ef_insert(ep, size2, ef);
  extent_free(ep, pos, rval - pos);
  return rval;
}


/**
 *
 */
extent_pool_t *
extent_create(int start, int size)
{
  extent_pool_t *ep = calloc(1, sizeof(extent_pool_t));
  RB_INIT(&ep->ep_sizes);
  RB_INIT(&ep->ep_freesegs);
  ep->ep_total = size;
  extent_free(ep, start, size);
  return ep;
}


/**
 *
 */
void
extent_destroy(extent_pool_t *ep)
{
  extent_freeseg_t *ef;
  
  while((ef = ep->ep_freesegs.root) != NULL)
    ef_destroy(ep, ef);
  
  assert(ep->ep_sizes.root == NULL);
  free(ep);
}


/**
 *
 */
void
extent_stats(const extent_pool_t *ep, int *totalp, int *availp, int *fragmentsp)
{
  const extent_freeseg_t *ef;
  int avail = 0;
  int fragments = 0;
  RB_FOREACH(ef, &ep->ep_freesegs, ef_link) {
    avail += ef->ef_size->es_size;
    fragments++;
  }

  if(totalp != NULL)
    *totalp = ep->ep_total;

  if(availp != NULL)
    *availp = avail;

  if(fragmentsp != NULL)
    *fragmentsp = fragments;
}


/**
 *
 */
void
extent_dump(extent_pool_t *ep)
{
  extent_freeseg_t *ef;
  extent_size_t *es;
  printf("      freelist\n");
  RB_FOREACH(ef, &ep->ep_freesegs, ef_link) {
    printf("  %10d +%d\n", ef->ef_pos, ef->ef_size->es_size);
  }

  printf("      size dist\n");
  RB_FOREACH(es, &ep->ep_sizes, es_link) {
    int segs = 0;
    LIST_FOREACH(ef, &es->es_freesegs, ef_size_link)
      segs++;
    printf("  %10d * %d\n", es->es_size, segs);
  }

}

#if 0

#define ASIZE 10
#define BSIZE 100
#define CSIZE 1000
#define DSIZE 999

int
main(int argc, char **argv)
{
  extent_pool_t *ep = extent_create(0, 3000);
  int a, b, c, d;
  int total, avail, fragments;

  printf("Initial\n");
  extent_dump(ep);

  a = extent_alloc(ep, ASIZE);
  b = extent_alloc(ep, BSIZE);
  c = extent_alloc(ep, CSIZE);
  d = extent_alloc(ep, DSIZE);
  
  printf("After four allocs\n");
  extent_dump(ep);

  extent_stats(ep, &total, &avail, &fragments);
  printf(" Available %d of %d in %d fragments\n", total, avail, fragments);

  extent_free(ep, c, CSIZE);
  extent_dump(ep);
  extent_free(ep, a, ASIZE);
  extent_dump(ep);
  extent_free(ep, b, BSIZE);
  extent_dump(ep);
  extent_free(ep, d, DSIZE);


  a = extent_alloc(ep, 1023);

  extent_dump(ep);

  b = extent_alloc_aligned(ep, 99, 64);
  printf("Aligned alloc @ %d\n", b);
  c = extent_alloc_aligned(ep, 99, 64);
  printf("Aligned alloc @ %d\n", c);
  d = extent_alloc_aligned(ep, 99, 64);
  printf("Aligned alloc @ %d\n", d);
  extent_dump(ep);
  extent_stats(ep, &total, &avail, &fragments);
  printf(" Available %d of %d in %d fragments\n", avail, total, fragments);

  extent_free(ep, b, 99);
  extent_free(ep, a, 1023);
  extent_free(ep, c, 99);
  extent_free(ep, d, 99);
  printf("Final\n");
  extent_dump(ep);

  return 0;
}
#endif
