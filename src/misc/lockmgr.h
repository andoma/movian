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

#include "arch/threads.h"
#include "arch/atomic.h"

typedef enum {
  LOCKMGR_UNLOCK,
  LOCKMGR_LOCK,
  LOCKMGR_TRY,
  LOCKMGR_RETAIN,
  LOCKMGR_RELEASE,
} lockmgr_op_t;

typedef struct lockmgr {
  hts_mutex_t lm_mutex;
  void (*lm_release)(void *ptr);
  atomic_t lm_refcount;
} lockmgr_t;

typedef int (lockmgr_fn_t)(void *ptr, lockmgr_op_t op);

void lockmgr_init(lockmgr_t *lm, void (*release)(void *aux));

int lockmgr_release(lockmgr_t *lm);

lockmgr_fn_t lockmgr_handler;
