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

#include "lockmgr.h"


/**
 *
 */
int
lockmgr_handler(void *ptr, lockmgr_op_t op)
{
  lockmgr_t *lm = ptr;

  switch(op) {
  case LOCKMGR_UNLOCK:
    hts_mutex_unlock(&lm->lm_mutex);
    return 0;
  case LOCKMGR_LOCK:
    hts_mutex_lock(&lm->lm_mutex);
    return 0;
  case LOCKMGR_TRY:
    return hts_mutex_trylock(&lm->lm_mutex);
  case LOCKMGR_RETAIN:
    atomic_inc(&lm->lm_refcount);
    return 0;
  case LOCKMGR_RELEASE:
    lm->lm_release(ptr);
    return 0;
  }
  abort();
}


void
lockmgr_init(lockmgr_t *lm, void (*release)(void *aux))
{
  hts_mutex_init(&lm->lm_mutex);
  lm->lm_release = release;
  atomic_set(&lm->lm_refcount, 1);
}


int
lockmgr_release(lockmgr_t *lm)
{
  if(atomic_dec(&lm->lm_refcount))
    return 1;

  hts_mutex_destroy(&lm->lm_mutex);
  return 0;
}
