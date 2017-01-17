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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arch/halloc.h"
#include "arch/threads.h"

#include "queue.h"
#include "main.h"
#include "pool.h"

#if ENABLE_BUGHUNT
#define POOL_BY_MALLOC
#endif

//#define POOL_BY_MMAP

#ifdef POOL_BY_MMAP
#include <sys/mman.h>
#endif



/**
 *
 */
typedef struct pool_item_dbg {
  const char *file;
  intptr_t line;
} pool_item_dbg_t;


/**
 *
 */
typedef struct pool_item {
  struct pool_item *link;
} pool_item_t;


/**
 *
 */
typedef struct pool_segment {
  LIST_ENTRY(pool_segment) ps_link;
  void *ps_addr;
  size_t ps_alloc_size;
  size_t ps_avail_size;

#ifdef POOL_DEBUG
  uint8_t *ps_mark;
#endif

} pool_segment_t;


#define ROUND_UP(p, round) ((p + round - 1) & ~(round - 1))

/**
 *
 */
static void attribute_unused
pool_segment_create(pool_t *p)
{
  size_t i;
  pool_item_t *pi = NULL, *prev = NULL;

  size_t size = 65536;
  void *addr = halloc(size);
  size_t topsiz =  ROUND_UP(sizeof(pool_segment_t), sizeof(void *));

  pool_segment_t *ps = (pool_segment_t *)((char *)addr + size - topsiz);
  ps->ps_addr = addr;
  ps->ps_alloc_size = size;
  ps->ps_avail_size = ps->ps_alloc_size - topsiz;

  for(i = 0; i <= ps->ps_avail_size - p->p_item_size; i += p->p_item_size) {
    pi = (pool_item_t *)(ps->ps_addr + i);
    pi->link = prev;
    prev = pi;
  }
  LIST_INSERT_HEAD(&p->p_segments, ps, ps_link);
  assert(pi != NULL);
  p->p_item = pi;
}


/**
 *
 */
void
pool_init(pool_t *p, const char *name, size_t item_size, int flags)
{
  p->p_item_size_req = item_size;
  item_size = ROUND_UP(item_size, 8);

  p->p_name = name;

#ifdef POOL_DEBUG
  item_size += sizeof(pool_item_dbg_t);
#endif

  p->p_item_size = item_size;
  p->p_flags = flags;
}



/**
 *
 */
pool_t *
pool_create(const char *name, size_t item_size, int flags)
{
  pool_t *p = calloc(1, sizeof(pool_t));
  pool_init(p, name, item_size, flags);
  return p;
}


#ifdef POOL_DEBUG
/**
 *
 */
static void
mark_segments(pool_t *p)
{
  pool_item_t *pi;
  pool_segment_t *ps;

  LIST_FOREACH(ps, &p->p_segments, ps_link) {
    ps->ps_mark = malloc(ps->ps_avail_size / p->p_item_size);
    memset(ps->ps_mark, 0xff, ps->ps_avail_size / p->p_item_size);
  }

  for(pi = p->p_item; pi != NULL; pi = pi->link) {
    LIST_FOREACH(ps, &p->p_segments, ps_link) {
      size_t off = (void *)pi - ps->ps_addr;

      if(off < ps->ps_avail_size) {
        off /= p->p_item_size;
        ps->ps_mark[off] = 0;
        break;
      }
    }
    if(ps == NULL) {
      TRACE(TRACE_ERROR, "POOL", "%s: Item %p is not part of pool segment",
            p->p_name, pi);
    }
  }
}

static void
unmark_segments(pool_t *p)
{
  pool_segment_t *ps;

  LIST_FOREACH(ps, &p->p_segments, ps_link) {
    free(ps->ps_mark);
    ps->ps_mark = NULL;
  }
}
#endif

/**
 *
 */
void
pool_destroy(pool_t *p)
{
  pool_segment_t *ps;

#ifdef POOL_DEBUG
  if(1) {

    mark_segments(p);

    LIST_FOREACH(ps, &p->p_segments, ps_link) {
      int items = ps->ps_avail_size / p->p_item_size;
      int i;
      for(i = 0; i < items; i++) {
	if(ps->ps_mark[i]) {
#ifdef POOL_DEBUG
	  pool_item_dbg_t *pid = ps->ps_addr + i * p->p_item_size;
	  printf("Leak at %p (%s:%d)\n",
		 pid, pid->file, (int)pid->line);
#else
	  printf("Leak at %p\n",
		 ps->ps_addr + i * p->p_item_size);
#endif
	}
      }
    }
  }
#endif

  while((ps = LIST_FIRST(&p->p_segments)) != NULL) {
    LIST_REMOVE(ps, ps_link);
#ifdef POOL_DEBUG
    free(ps->ps_mark);
#endif
    hfree(ps->ps_addr, ps->ps_alloc_size);
  }
    
  if(p->p_num_out)
    TRACE(TRACE_INFO, "pool", "Destroying pool '%s', %d items out",
	  p->p_name, p->p_num_out);

  free(p);
}





/**
 *
 */
void *
#ifdef POOL_DEBUG
pool_get_ex(pool_t *p, const char *file, int line)
#else
pool_get(pool_t *p)
#endif
{
  p->p_num_out++;
#if defined(POOL_BY_MMAP)
  return mmap(NULL, p->p_item_size_req, PROT_WRITE | PROT_READ,
              MAP_ANON | MAP_PRIVATE, -1, 0);

#elif defined(POOL_BY_MALLOC)
  if(p->p_flags & POOL_ZERO_MEM)
    return calloc(1, p->p_item_size_req);
  else
    return malloc(p->p_item_size_req);
#else
  pool_item_t *pi = p->p_item;
  if(pi == NULL) {
    pool_segment_create(p);
    pi = p->p_item;
  }
  p->p_item = pi->link;


  if(p->p_flags & POOL_ZERO_MEM)
    memset(pi, 0, p->p_item_size);

#ifdef POOL_DEBUG

  pool_item_dbg_t *pid = (void *)pi;
  pid->file = file;
  pid->line = line;
  return (void *)pi + sizeof(pool_item_dbg_t);
#else
  return pi;
#endif
#endif

}

/**
 *
 */
void
pool_put(pool_t *p, void *ptr)
{
#if defined(POOL_BY_MMAP)

#if defined(MADV_FREE)
  madvise(ptr, p->p_item_size_req, MADV_FREE);
#elif defined(MADV_DONTNEED)
  madvise(ptr, p->p_item_size_req, MADV_DONTNEED);
#endif
  mprotect(ptr, p->p_item_size_req, PROT_NONE);
#elif defined(POOL_BY_MALLOC)
  free(ptr);
#else

#ifdef POOL_DEBUG
  pool_item_t *pi = ptr - sizeof(pool_item_dbg_t);
#else
  pool_item_t *pi = ptr;
#endif

#ifdef POOL_DEBUG
  pool_segment_t *ps;
  LIST_FOREACH(ps, &p->p_segments, ps_link)
    if((uintptr_t)pi >= (uintptr_t)ps->ps_addr &&
       (uintptr_t)pi < (uintptr_t)ps->ps_addr + ps->ps_avail_size)
      break;

  if(ps == NULL) {
    TRACE(TRACE_ERROR, "POOL", "%s: Item %p not in any segment",
          p->p_name, pi);
    pool_segment_t *ps;
    LIST_FOREACH(ps, &p->p_segments, ps_link) {
      TRACE(TRACE_ERROR, "POOL", "    segment %p +%zd\n",
            ps->ps_addr, ps->ps_avail_size);
    }
    abort();
  }

  assert(ps != NULL);

  memset(pi, 0xff, p->p_item_size);
#endif

  pi->link = p->p_item;
  p->p_item = pi;
#endif
  p->p_num_out--;
}


/**
 *
 */
int
pool_num(pool_t *p)
{
  return p->p_num_out;
}


#ifdef POOL_DEBUG
/**
 *
 */
void
pool_foreach(pool_t *p, void (*fn)(void *ptr, void *opaque), void *opaque)
{
  pool_segment_t *ps;

  mark_segments(p);

  LIST_FOREACH(ps, &p->p_segments, ps_link) {
    int items = ps->ps_avail_size / p->p_item_size;
    int i;
    for(i = 0; i < items; i++) {
      if(ps->ps_mark[i]) {
        fn(ps->ps_addr + i * p->p_item_size + sizeof(pool_item_dbg_t), opaque);
      }
    }
  }

  unmark_segments(p);
}
#endif
