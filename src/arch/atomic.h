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
#include "compiler.h"


#if (__GNUC__ >= 4 && __GNUC_MINOR__ >=3) || (__GNUC__ >= 5) || defined(__APPLE__) || defined(__native_client__)

typedef struct atomic {
  int v;
} atomic_t;


static inline void
atomic_inc(atomic_t *a)
{
  __sync_add_and_fetch(&a->v, 1);
}

static inline int __attribute__((warn_unused_result))
atomic_add_and_fetch(atomic_t *a, int v)
{
  return __sync_add_and_fetch(&a->v, v);
}

static inline int
atomic_dec(atomic_t *a)
{
  return __sync_add_and_fetch(&a->v, -1);
}

static inline int
atomic_get(const atomic_t *a)
{
  return (*(volatile int *)&(a)->v);
}

static inline void
atomic_set(atomic_t *a, int v)
{
  a->v = v;
}

#elif defined(_MSC_VER)

#include <Windows.h>

typedef struct atomic {
  long v;
} atomic_t;


static __inline void
atomic_inc(atomic_t *a)
{
  InterlockedIncrement(&a->v);
}

static __inline int
atomic_add_and_fetch(atomic_t *a, int v)
{
  return InterlockedAdd(&a->v, v);
}

static __inline int
atomic_dec(atomic_t *a)
{
  return InterlockedDecrement(&a->v);
}

static __inline int
atomic_get(const atomic_t *a)
{
  return (*(volatile int *)&(a)->v);
}

static __inline void
atomic_set(atomic_t *a, int v)
{
  a->v = v;
}

#else
#error Missing atomic ops
#endif

