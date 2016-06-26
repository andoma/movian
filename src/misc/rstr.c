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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "rstr.h"

#ifdef RSTR_STATS
int rstr_allocs;
int rstr_dups;
int rstr_releases;
int rstr_frees;
#endif

rstr_t *
rstr_alloc(const char *in)
{
  size_t l;
  if(in == NULL)
    return NULL; 

  l = strlen(in);
  rstr_t *rs = malloc(sizeof(rstr_t) + l + 1);
#ifdef USE_RSTR_REFCOUNTING
  atomic_set(&rs->refcnt, 1);
#endif
  memcpy(rs->str, in, l + 1);

#ifdef RSTR_STATS
    atomic_add(&rstr_allocs, 1);
#endif
  return rs;
}

rstr_t *
rstr_allocl(const char *in, size_t len)
{
  rstr_t *rs = malloc(sizeof(rstr_t) + len + 1);
#ifdef USE_RSTR_REFCOUNTING
  atomic_set(&rs->refcnt, 1);
#endif
  if(in != NULL)
    memcpy(rs->str, in, len);
  rs->str[len] = 0;
#ifdef RSTR_STATS
    atomic_add(&rstr_allocs, 1);
#endif
  return rs;
}

rstr_t *
rstr_spn(rstr_t *s, const char *set, int offset)
{
  size_t len = strlen(rstr_get(s));
  size_t l;
  if(offset >= len)
    return rstr_dup(s);
  l = strcspn(rstr_get(s) + offset, set) + offset;
  if(l == len)
    return rstr_dup(s);
  return rstr_allocl(rstr_get(s), l);
}


#ifdef RSTR_STATS
static void
print_rstr_stats(void)
{
  printf("rstr stats\n");
  printf("  %d allocs\n"
	 "  %d frees\n"
	 "  %d dups\n"
	 "  %d releases\n",
	 rstr_allocs,
	 rstr_frees,
	 rstr_dups,
	 rstr_releases);
}

static void __attribute__((constructor)) rstr_setup(void)
{
  atexit(print_rstr_stats);
}
#endif


void 
rstr_vec_append(rstr_vec_t **rvp, rstr_t *str)
{
  rstr_vec_t *rv = *rvp;

  if(rv == NULL) {
    rv = malloc(sizeof(rstr_vec_t) + sizeof(rstr_t *) * 16);
    rv->capacity = 16;
    rv->size = 0;
    *rvp = rv;
  } else if(rv->size == rv->capacity) {
    rv->capacity = rv->capacity * 2;
    rv = realloc(rv, sizeof(rstr_vec_t) + sizeof(rstr_t *) * rv->capacity);
    *rvp = rv;
  }
  rv->v[rv->size++] = rstr_dup(str);
}


void
rstr_vec_free(rstr_vec_t *rv)
{
  int i;
  if(rv == NULL)
    return;
  for(i = 0; i < rv->size; i++)
    rstr_release(rv->v[i]);
  free(rv);
}

