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
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/**
 * Mutexes
 */
typedef pthread_mutex_t hts_mutex_t;
typedef pthread_mutex_t hts_lwmutex_t;

#define hts_mutex_init(m)            pthread_mutex_init((m), NULL)
#define hts_mutex_lock(m)            pthread_mutex_lock(m)
#define hts_mutex_unlock(m)          pthread_mutex_unlock(m)
#define hts_mutex_destroy(m)         pthread_mutex_destroy(m)
extern void hts_mutex_init_recursive(hts_mutex_t *m);

#define hts_lwmutex_init(m)            pthread_mutex_init((m), NULL)
#define hts_lwmutex_lock(m)            pthread_mutex_lock(m)
#define hts_lwmutex_unlock(m)          pthread_mutex_unlock(m)
#define hts_lwmutex_destroy(m)         pthread_mutex_destroy(m)
#define hts_lwmutex_init_recursive(m)  hts_mutex_init_recursive(m)


static inline void
hts_mutex_assert0(pthread_mutex_t *l, const char *file, int line)
{
  if(pthread_mutex_trylock(l) == EBUSY)
    return;

  fprintf(stderr, "Mutex not held at %s:%d\n", file, line);
  abort();
}

#define hts_mutex_assert(l) hts_mutex_assert0(l, __FILE__, __LINE__)

static inline int
hts_mutex_trylock(pthread_mutex_t *m)
{
  return pthread_mutex_trylock(m) == EBUSY;
}

/**
 * Condition variables
 */
typedef pthread_cond_t hts_cond_t;
#define hts_cond_signal(c)             pthread_cond_signal(c)
#define hts_cond_broadcast(c)          pthread_cond_broadcast(c)
#define hts_cond_wait(c, m)            pthread_cond_wait(c, m)
#define hts_cond_destroy(c)            pthread_cond_destroy(c)
extern void hts_cond_init(hts_cond_t *c, hts_mutex_t *m);
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta);
extern int hts_cond_wait_timeout_abs(hts_cond_t *c, hts_mutex_t *m, int64_t ts);

/**
 * Threads
 */
#define THREAD_PRIO_AUDIO         -10
#define THREAD_PRIO_VIDEO         -5
#define THREAD_PRIO_DEMUXER        3
#define THREAD_PRIO_UI_WORKER_HIGH 5
#define THREAD_PRIO_UI_WORKER_MED  8
#define THREAD_PRIO_FILESYSTEM     10
#define THREAD_PRIO_MODEL          12
#define THREAD_PRIO_METADATA       13
#define THREAD_PRIO_UI_WORKER_LOW  14
#define THREAD_PRIO_METADATA_BG    15
#define THREAD_PRIO_BGTASK         19


typedef pthread_t hts_thread_t;

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *,
				       int);

extern void hts_thread_create_joinable(const char *, hts_thread_t *, 
				       void *(*)(void *), void *, int);

#define hts_thread_detach(t)                pthread_detach(*(t));

#define hts_thread_join(t)                  pthread_join(*(t), NULL)
#define hts_thread_current()                pthread_self()

extern const char *hts_thread_name(char *buf, size_t len);

#if !ENABLE_EMU_THREAD_SPECIFICS

typedef pthread_key_t hts_key_t;

#define hts_thread_key_create(k, f) pthread_key_create((pthread_key_t *)k, f)
#define hts_thread_key_delete(k)    pthread_key_delete(k)

#define hts_thread_set_specific(k, p) pthread_setspecific(k, p)
#define hts_thread_get_specific(k)    pthread_getspecific(k)

#endif

#define HTS_MUTEX_DECL(name) hts_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define HTS_LWMUTEX_DECL(name) hts_mutex_t name = PTHREAD_MUTEX_INITIALIZER



