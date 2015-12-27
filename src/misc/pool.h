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
#pragma once
#include "main.h"
#include "arch/threads.h"
#include "misc/queue.h"

#ifndef NDEBUG
#define POOL_DEBUG
#endif

LIST_HEAD(pool_segment_list, pool_segment);


/**
 *
 */
typedef struct pool {
  struct pool_segment_list p_segments;
  
  size_t p_item_size_req;  // Size requested by user
  size_t p_item_size;      // Actual size of memory allocated
  int p_flags;

  hts_mutex_t p_mutex;
  struct pool_item *p_item;

  int p_num_out;
  const char *p_name;
} pool_t;


#define POOL_ZERO_MEM  0x2

pool_t *pool_create(const char *name, size_t item_size, int flags);

void pool_init(pool_t *pool, const char *name, size_t item_size, int flags);

#ifdef POOL_DEBUG

void *pool_get_ex(pool_t *p, const char *file, int line)
  attribute_malloc;

#define pool_get(p) pool_get_ex(p, __FILE__, __LINE__)

#else

void *pool_get(pool_t *p) attribute_malloc;

#endif

void pool_put(pool_t *p, void *ptr);

void pool_destroy(pool_t *p);

int pool_num(pool_t *p);

#ifdef POOL_DEBUG
void pool_foreach(pool_t *p, void (*fn)(void *ptr, void *opaque), void *opaque);
#endif
