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


#include <errno.h>
#include <jni.h>

#include "showtime.h"
#include "arch/posix/posix_threads.h"

extern JavaVM *JVM;

int posix_set_thread_priorities;

void
hts_mutex_init_recursive(hts_mutex_t *m)
{
  pthread_mutexattr_t a;
  pthread_mutexattr_init(&a);
  pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(m, &a);
  pthread_mutexattr_destroy(&a);
}

/**
 *
 */
int
hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta)
{
  struct timespec ts;

  clock_gettime(CLOCK_REALTIME, &ts);

  ts.tv_sec  +=  delta / 1000;
  ts.tv_nsec += (delta % 1000) * 1000000;

  if(ts.tv_nsec > 1000000000) {
    ts.tv_sec++;
    ts.tv_nsec -= 1000000000;
  }
  return pthread_cond_timedwait(c, m, &ts) == ETIMEDOUT;
}


/**
 *
 */
typedef struct {
  char *title;
  void *(*func)(void *);
  void *aux;
  int prio;
} trampoline_t;


/**
 *
 */
static trampoline_t *
make_trampoline(const char *title, void *(*func)(void *), void *aux,
		int prio)
{
  trampoline_t *t = malloc(sizeof(trampoline_t));
  t->title = strdup(title);
  t->func = func;
  t->aux = aux;
  t->prio = prio;
  return t;
}

/**
 *
 */
static void *
thread_trampoline(void *aux)
{
  trampoline_t *t = aux;
  void *r;

  JNIEnv *jenv = NULL;
  (*JVM)->AttachCurrentThread(JVM, &jenv, NULL);

  r = t->func(t->aux);

#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_exit_specific();
#endif
  TRACE(TRACE_DEBUG, "thread", "Thread %s exited", t->title);

  (*JVM)->DetachCurrentThread(JVM);

  free(t->title);
  free(t);
  return r;
}


/**
 *
 */
void
hts_thread_create_detached(const char *title, void *(*func)(void *), void *aux,
			   int prio)
{
  pthread_t id;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setstacksize(&attr, 128 * 1024);
  pthread_create(&id, &attr, thread_trampoline,
		 make_trampoline(title, func, aux, prio));
  pthread_attr_destroy(&attr);
  TRACE(TRACE_DEBUG, "thread", "Created detached thread: %s", title);

}

void
hts_thread_create_joinable(const char *title, hts_thread_t *p,
			   void *(*func)(void *), void *aux, int prio)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  pthread_create(p, &attr, thread_trampoline,
		 make_trampoline(title, func, aux, prio));
  pthread_attr_destroy(&attr);

  TRACE(TRACE_DEBUG, "thread", "Created thread: %s", title);
}
