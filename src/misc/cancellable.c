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

#include <stdlib.h>

#include "arch/threads.h"
#include "cancellable.h"

static HTS_MUTEX_DECL(cancellable_mutex);

struct cancellable {
  atomic_t refcount;
  int cancelled;
  void (*cancel)(void *opaque);
  void *opaque;
};


/**
 *
 */
cancellable_t *
cancellable_bind(cancellable_t *c, void (*fn)(void *opaque), void *opaque)
{
  atomic_inc(&c->refcount);
  hts_mutex_lock(&cancellable_mutex);
  c->cancel = fn;
  c->opaque = opaque;

  if(c->cancelled)
    fn(opaque);

  hts_mutex_unlock(&cancellable_mutex);
  return c;
}


/**
 *
 */
void
cancellable_unbind(cancellable_t *c, void *opaque)
{
  if(c == NULL)
    return;

  hts_mutex_lock(&cancellable_mutex);

  if(c->opaque == opaque) {
    c->cancel = NULL;
    c->opaque = NULL;
  }

  hts_mutex_unlock(&cancellable_mutex);
  cancellable_release(c);
}


/**
 *
 */
void
cancellable_cancel_locked(cancellable_t *c)
{
  c->cancelled = 1;
  if(c->cancel != NULL)
    c->cancel(c->opaque);
}


/**
 *
 */
void
cancellable_cancel(cancellable_t *c)
{
  hts_mutex_lock(&cancellable_mutex);
  cancellable_cancel_locked(c);
  hts_mutex_unlock(&cancellable_mutex);
}


/**
 *
 */
void cancellable_reset(cancellable_t *c)
{
  hts_mutex_lock(&cancellable_mutex);
  c->cancelled = 0;
  c->cancel = NULL;
  hts_mutex_unlock(&cancellable_mutex);
}


/**
 *
 */
int
cancellable_is_cancelled(const cancellable_t *c)
{
  if(c == NULL)
    return 0;

  return c->cancelled;
}


/**
 *
 */
cancellable_t *
cancellable_create(void)
{
  cancellable_t *c = calloc(1, sizeof(cancellable_t));

  atomic_set(&c->refcount, 1);
  return c;
}


void
cancellable_release(cancellable_t *c)
{
  if(c == NULL)
    return;

  if(atomic_dec(&c->refcount))
    return;

  free(c);
}


cancellable_t *
cancellable_retain(cancellable_t *c)
{
  if(c != NULL)
    atomic_inc(&c->refcount);
  return c;
}
