#pragma once

#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/**
 * Mutexes
 */
typedef pthread_mutex_t hts_mutex_t;

#define hts_mutex_init(m)            pthread_mutex_init((m), NULL)
#define hts_mutex_lock(m)            pthread_mutex_lock(m)
#define hts_mutex_unlock(m)          pthread_mutex_unlock(m)
#define hts_mutex_destroy(m)         pthread_mutex_destroy(m)
extern void hts_mutex_init_recursive(hts_mutex_t *m);


static inline void
hts_mutex_assert0(pthread_mutex_t *l, const char *file, int line)
{
  if(pthread_mutex_trylock(l) == EBUSY)
    return;

  fprintf(stderr, "Mutex not held at %s:%d\n", file, line);
  abort();
}

#define hts_mutex_assert(l) hts_mutex_assert0(l, __FILE__, __LINE__)

/**
 * Condition variables
 */
typedef pthread_cond_t hts_cond_t;
#define hts_cond_init(c, m)            pthread_cond_init((c), NULL)
#define hts_cond_signal(c)             pthread_cond_signal(c)
#define hts_cond_broadcast(c)          pthread_cond_broadcast(c)
#define hts_cond_wait(c, m)            pthread_cond_wait(c, m)
#define hts_cond_destroy(c)            pthread_cond_destroy(c)
extern int hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta);

/**
 * Threads
 */
// These are not really used on POSIX
#define THREAD_PRIO_LOW    0
#define THREAD_PRIO_NORMAL 1
#define THREAD_PRIO_HIGH   2


typedef pthread_t hts_thread_t;

extern void hts_thread_create_detached(const char *, void *(*)(void *), void *,
				       int);

extern void hts_thread_create_joinable(const char *, hts_thread_t *, 
				       void *(*)(void *), void *, int);

#define hts_thread_detach(t)                pthread_detach(*(t));

#define hts_thread_join(t)                  pthread_join(*(t), NULL)
#define hts_thread_current()                pthread_self()

#if !ENABLE_EMU_THREAD_SPECIFICS

typedef pthread_key_t hts_key_t;

#define hts_thread_key_create(k, f) pthread_key_create((pthread_key_t *)k, f)
#define hts_thread_key_delete(k)    pthread_key_delete(k)

#define hts_thread_set_specific(k, p) pthread_setspecific(k, p)
#define hts_thread_get_specific(k)    pthread_getspecific(k)

#endif

