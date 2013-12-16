/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include "pratom.h"
#include "prlock.h"
#include "prcvar.h"
#include "prthread.h"

#include "arch/atomic.h"
#include <stdlib.h>

PRInt32
PR_AtomicIncrement(PRInt32 *val)
{
  return atomic_add(val, 1) + 1;
}

PRInt32
PR_AtomicDecrement(PRInt32 *val)
{
  return atomic_add(val, -1) - 1;
}

PRLock *
PR_NewLock(void)
{
  hts_mutex_t *m = malloc(sizeof(hts_mutex_t));
  hts_mutex_init(m);
  return m;
}

void
PR_DestroyLock(PRLock *m)
{
  hts_mutex_destroy(m);
  free(m);
}

PRCondVar *
PR_NewCondVar(PRLock *lock)
{
  PRCondVar *c = malloc(sizeof(PRCondVar));
  c->mtx = lock;
  hts_cond_init(&c->cond, c->mtx);
  return c;
}

void
PR_DestroyCondVar(PRCondVar *cvar)
{
  hts_cond_destroy(&cvar->cond);
  free(cvar);
}

PRStatus
PR_WaitCondVar(PRCondVar *cvar, PRIntervalTime timeout)
{
  hts_cond_wait(&cvar->cond, cvar->mtx);
  return PR_SUCCESS;
}

PRThread *
PR_GetCurrentThread(void)
{
  return (PRThread *)(intptr_t)hts_thread_current();
}


PRStatus
PR_NewThreadPrivateIndex(PRUintn *newIndex, PRThreadPrivateDTOR destructor)
{
  hts_thread_key_create(newIndex, destructor);
  return PR_SUCCESS;
}

PRStatus
PR_SetThreadPrivate(PRUintn key, void *priv)
{
  hts_thread_set_specific(key, priv);
  return PR_SUCCESS;
}

void *
PR_GetThreadPrivate(PRUintn key)
{
  return hts_thread_get_specific(key);
}
