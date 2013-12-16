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

#include <stdlib.h>

#include "misc/queue.h"
#include "misc/pool.h"
#include "misc/minmax.h"
#include "threads.h"

static hts_mutex_t ets_mutex;

LIST_HEAD(ets_value_list, ets_value);
LIST_HEAD(ets_key_list, ets_key);

typedef struct ets_value {
  LIST_ENTRY(ets_value) ev_link;
  hts_thread_t ev_tid;
  void *ev_value;
} ets_value_t;

typedef struct ets_key {
  LIST_ENTRY(ets_key) ek_link;
  unsigned int ek_key;
  struct ets_value_list ek_values;
  void (*ek_dtor)(void *);
} ets_key_t;

static struct ets_key_list ets_keys;

static int keytally;

static pool_t ets_pool;

static void __attribute__((constructor))
hts_thread_key_init(void)
{
  pool_init(&ets_pool, "ETS", MAX(sizeof(ets_key_t),
				  sizeof(ets_value_t)), 0);
  hts_mutex_init(&ets_mutex);
}

int
hts_thread_key_create(unsigned int *k, void (*destructor)(void *))
{
  hts_mutex_lock(&ets_mutex);
  ets_key_t *ek = pool_get(&ets_pool);
  ek->ek_dtor = destructor;
  keytally++;
  ek->ek_key = keytally;
  *k = keytally;
  LIST_INIT(&ek->ek_values);
  LIST_INSERT_HEAD(&ets_keys, ek, ek_link);
  hts_mutex_unlock(&ets_mutex);
  return 0;
}


/**
 *
 */
static ets_key_t *
findkey(unsigned int k)
{
  ets_key_t *ek;
  LIST_FOREACH(ek, &ets_keys, ek_link)
    if(ek->ek_key == k)
      return ek;
  return NULL;
}

int
hts_thread_key_delete(unsigned int k)
{
  ets_key_t *ek;
  ets_value_t *ev;

  hts_mutex_lock(&ets_mutex);
  ek = findkey(k);

  while((ev = LIST_FIRST(&ek->ek_values)) != NULL) {
    LIST_REMOVE(ev, ev_link);
    pool_put(&ets_pool, ev);
  }

  LIST_REMOVE(ek, ek_link);
  pool_put(&ets_pool, ek);

  hts_mutex_unlock(&ets_mutex);
  return 0;
}


int
hts_thread_set_specific(unsigned int k, void *p)
{
  ets_key_t *ek;
  ets_value_t *ev;
  hts_thread_t self = hts_thread_current();

  hts_mutex_lock(&ets_mutex);
  ek = findkey(k);

  LIST_FOREACH(ev, &ek->ek_values, ev_link)
    if(ev->ev_tid == self)
      break;

  if(ev == NULL) {
    ev = pool_get(&ets_pool);
    ev->ev_tid = self;
    LIST_INSERT_HEAD(&ek->ek_values, ev, ev_link);
  }
  ev->ev_value = p;
  hts_mutex_unlock(&ets_mutex);
  return 0;
}


void *
hts_thread_get_specific(unsigned int k)
{
  ets_key_t *ek;
  ets_value_t *ev;
  hts_thread_t self = hts_thread_current();
  void *ret;

  hts_mutex_lock(&ets_mutex);
  ek = findkey(k);

  LIST_FOREACH(ev, &ek->ek_values, ev_link)
    if(ev->ev_tid == self)
      break;

  ret = ev ? ev->ev_value : NULL;
  hts_mutex_unlock(&ets_mutex);
  return ret;
}


void
hts_thread_exit_specific(void)
{
  hts_thread_t self = hts_thread_current();

  ets_key_t *ek;
  ets_value_t *ev, *next;

  hts_mutex_lock(&ets_mutex);
  LIST_FOREACH(ek, &ets_keys, ek_link) {
    
    for(ev = LIST_FIRST(&ek->ek_values); ev != NULL; ev = next) {
      next = LIST_NEXT(ev, ev_link);
      if(ev->ev_tid != self)
	continue;

      ek->ek_dtor(ev->ev_value);
      LIST_REMOVE(ev, ev_link);
      pool_put(&ets_pool, ev);
    }
  }
  hts_mutex_unlock(&ets_mutex);
}
