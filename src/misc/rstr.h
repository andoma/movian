/*
 *  Referenced strings
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef RSTR_H__
#define RSTR_H__

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "arch/atomic.h"

#define USE_RSTR

#ifdef USE_RSTR

// #define RSTR_STATS

#ifdef RSTR_STATS
extern int rstr_allocs;
extern int rstr_dups;
extern int rstr_releases;
extern int rstr_frees;
#endif


typedef struct rstr {
  int32_t refcnt;
  char str[0];
} rstr_t;

rstr_t *rstr_alloc(const char *in) __attribute__ ((malloc));

rstr_t *rstr_allocl(const char *in, size_t len) __attribute__ ((malloc));

static inline const char *rstr_get(const rstr_t *rs)
{
  return rs ? rs->str : NULL;
}


static inline char *rstr_data(rstr_t *rs)
{
  return rs->str;
}

static inline rstr_t *
 __attribute__ ((warn_unused_result))
rstr_dup(rstr_t *rs)
{
  if(rs != NULL)
    atomic_add(&rs->refcnt, 1);
#ifdef RSTR_STATS
  atomic_add(&rstr_dups, 1);
#endif
  return rs;
}

static inline void rstr_release(rstr_t *rs)
{
#ifdef RSTR_STATS
  atomic_add(&rstr_releases, 1);
#endif
  if(rs != NULL && atomic_add(&rs->refcnt, -1) == 1) {
#ifdef RSTR_STATS
    atomic_add(&rstr_frees, 1);
#endif
    free(rs);
  }
}

static inline void rstr_set(rstr_t **p, rstr_t *r)
{
  rstr_release(*p);
  *p = r ? rstr_dup(r) : NULL;
}

rstr_t *rstr_spn(rstr_t *s, const char *set, int offset);

static inline int rstr_eq(const rstr_t *a, const rstr_t *b)
{
  if(a == NULL && b == NULL)
    return 1;
  if(a == NULL || b == NULL)
    return 0;
  return !strcmp(rstr_get(a), rstr_get(b));
}



#else

#include <string.h>

typedef char rstr_t;

void rstr_release(rstr_t *);

rstr_t *rstr_dup(rstr_t *);

#define rstr_get(n) (n)

#define rstr_data(n) (n)

rstr_t *rstr_alloc(const char *in);

rstr_t *rstr_allocl(const char *in, size_t len);

static inline void rstr_set(rstr_t **p, rstr_t *r)
{
  free(*p);
  *p = r ? strdup(r) : NULL;
}

#endif


#endif /* RSTR_H__ */
