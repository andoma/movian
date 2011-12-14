/*
 *  Pool allocator
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arch/halloc.h"
#include "arch/threads.h"

#include "queue.h"
#include "showtime.h"
#include "pool.h"

LIST_HEAD(pool_segment_list, pool_segment);

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
  size_t ps_size;

#ifdef POOL_DEBUG
  uint8_t *ps_mark;
#endif

} pool_segment_t;


/**
 *
 */
struct pool {
  struct pool_segment_list p_segments;
  
  size_t p_item_size;

  int p_flags;

  hts_mutex_t p_mutex;
  pool_item_t *p_item;

  int p_num_out;
  char *p_name;
};


/**
 *
 */
static void
pool_segment_create(pool_t *p)
{
  pool_segment_t *ps = malloc(sizeof(pool_segment_t));
  size_t i;
  pool_item_t *pi, *prev = NULL;

  ps->ps_size = 65536;
  ps->ps_addr = halloc(ps->ps_size);

  for(i = 0; i <= ps->ps_size - p->p_item_size; i += p->p_item_size) {
    pi = ps->ps_addr + i;
    pi->link = prev;
    prev = pi;
  }
  LIST_INSERT_HEAD(&p->p_segments, ps, ps_link);
  p->p_item = pi;
}

#define ROUND_UP(p, round) ((p + round - 1) & ~(round - 1))

/**
 *
 */
pool_t *
pool_create(const char *name, size_t item_size, int flags)
{
  pool_t *p = calloc(1, sizeof(pool_t));

  item_size = ROUND_UP(item_size, 8);

  p->p_name = strdup(name);

#ifdef POOL_DEBUG
  item_size += sizeof(pool_item_dbg_t);
#endif

  p->p_item_size = item_size;
  p->p_flags = flags;

  if(flags & POOL_REENTRANT)
    hts_mutex_init(&p->p_mutex);

  return p;
}


/**
 *
 */
void
pool_destroy(pool_t *p)
{
  pool_segment_t *ps;

#ifdef POOL_DEBUG
  if(1) {
    pool_item_t *pi;

    LIST_FOREACH(ps, &p->p_segments, ps_link) {
      ps->ps_mark = malloc(ps->ps_size / p->p_item_size);
      memset(ps->ps_mark, 0xff, ps->ps_size / p->p_item_size);
    }

    for(pi = p->p_item; pi != NULL; pi = pi->link) {
      LIST_FOREACH(ps, &p->p_segments, ps_link) {
	if((intptr_t)pi >= (intptr_t)ps->ps_addr && 
	   (intptr_t)pi < (intptr_t)ps->ps_addr + ps->ps_size) {
	  size_t off = ((void *)pi - ps->ps_addr) / p->p_item_size;
	  ps->ps_mark[off] = 0;
	}
      }
    }
   
    LIST_FOREACH(ps, &p->p_segments, ps_link) {
      int items = ps->ps_size / p->p_item_size;
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
    hfree(ps->ps_addr, ps->ps_size);
    LIST_REMOVE(ps, ps_link);
#ifdef POOL_DEBUG
    free(ps->ps_mark);
#endif
    free(ps);
  }
    
  if(p->p_num_out)
    TRACE(TRACE_INFO, "pool", "Destroying pool '%s', %d items out",
	  p->p_name, p->p_num_out);

  if(p->p_flags & POOL_REENTRANT)
    hts_mutex_destroy(&p->p_mutex);
  free(p->p_name);
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
  if(p->p_flags & POOL_REENTRANT)
    hts_mutex_lock(&p->p_mutex);

  pool_item_t *pi = p->p_item;
  if(pi == NULL) {
    pool_segment_create(p);
    pi = p->p_item;
  }
  p->p_item = pi->link;

  p->p_num_out++;

  if(p->p_flags & POOL_REENTRANT)
    hts_mutex_unlock(&p->p_mutex);

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
}

/**
 *
 */
void
pool_put(pool_t *p, void *ptr)
{
#ifdef POOL_DEBUG
  pool_item_t *pi = ptr - sizeof(pool_item_dbg_t);
#else
  pool_item_t *pi = ptr;
#endif

#ifdef POOL_DEBUG
  pool_segment_t *ps;
  LIST_FOREACH(ps, &p->p_segments, ps_link)
    if((intptr_t)pi >= (intptr_t)ps->ps_addr && 
       (intptr_t)pi < (intptr_t)ps->ps_addr + ps->ps_size)
      break;

  assert(ps != NULL);

  memset(pi, 0xff, p->p_item_size);
#endif

  if(p->p_flags & POOL_REENTRANT)
    hts_mutex_lock(&p->p_mutex);

  pi->link = p->p_item;
  p->p_item = pi;
  p->p_num_out--;

  if(p->p_flags & POOL_REENTRANT)
    hts_mutex_unlock(&p->p_mutex);
}


/**
 *
 */
int
pool_num(pool_t *p)
{
  return p->p_num_out;
}
