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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "config.h"
#include "arch/atomic.h"
#include "compiler.h"

#if !ENABLE_BUGHUNT
#define USE_RSTR_REFCOUNTING
#endif


// #define RSTR_STATS

#ifdef RSTR_STATS
extern int rstr_allocs;
extern int rstr_dups;
extern int rstr_releases;
extern int rstr_frees;
#endif


typedef struct rstr {
#ifdef USE_RSTR_REFCOUNTING
  atomic_t refcnt;
#endif
  char str[0];
} rstr_t;

rstr_t *rstr_alloc(const char *in) attribute_malloc;

rstr_t *rstr_allocl(const char *in, size_t len) attribute_malloc;

static __inline const char *rstr_get(const rstr_t *rs)
{
  return rs ? rs->str : NULL;
}

static __inline const char *rstr_get_always(const rstr_t *rs)
{
  return rs->str;
}


static __inline char *rstr_data(rstr_t *rs)
{
  return rs->str;
}

static __inline rstr_t * attribute_unused_result
rstr_dup(rstr_t *rs)
{
#ifdef USE_RSTR_REFCOUNTING
  if(rs != NULL)
    atomic_inc(&rs->refcnt);
#ifdef RSTR_STATS
  atomic_add(&rstr_dups, 1);
#endif
  return rs;
#else // USE_RSTR_REFCOUNTING
  return rstr_alloc(rstr_get(rs));
#endif
}

static __inline void rstr_release(rstr_t *rs)
{
#ifdef USE_RSTR_REFCOUNTING
#ifdef RSTR_STATS
  atomic_add(&rstr_releases, 1);
#endif
  if(rs != NULL && !atomic_dec(&rs->refcnt)) {
#ifdef RSTR_STATS
    atomic_add(&rstr_frees, 1);
#endif
    free(rs);
  }
#else // USE_RSTR_REFCOUNTING
  free(rs);
#endif
}

static __inline void rstr_set(rstr_t **p, rstr_t *r)
{
  rstr_release(*p);
  *p = r ? rstr_dup(r) : NULL;
}

rstr_t *rstr_spn(rstr_t *s, const char *set, int offset);

static __inline int rstr_eq(const rstr_t *a, const rstr_t *b)
{
  if(a == NULL && b == NULL)
    return 1;
  if(a == NULL || b == NULL)
    return 0;
  return !strcmp(rstr_get(a), rstr_get(b));
}

typedef struct rstr_vec {
  int size;
  int capacity;
  rstr_t *v[0];
} rstr_vec_t;


void rstr_vec_append(rstr_vec_t **rvp, rstr_t *str);

void rstr_vec_free(rstr_vec_t *rv);

