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
#include "arch/atomic.h"


typedef struct rstr {
  int32_t refcnt;
  char str[0];
} rstr_t;

rstr_t *rstr_alloc(const char *in) __attribute__ ((malloc));

static inline const char *rstr_get(rstr_t *rs)
{
  return rs ? rs->str : NULL;
}

static inline rstr_t *
 __attribute__ ((warn_unused_result))
rstr_dup(rstr_t *rs)
{
  if(rs != NULL)
    atomic_add(&rs->refcnt, 1);
  return rs;
}

static inline void rstr_release(rstr_t *rs)
{
  if(rs != NULL && atomic_add(&rs->refcnt, -1) == 1)
    free(rs);
}



#endif /* RSTR_H__ */
