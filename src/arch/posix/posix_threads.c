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
#if defined(linux)
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

#include <sys/time.h>
#include <sys/types.h>

#include "main.h"
#include "posix_threads.h"

#include <errno.h>


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

#if defined(__APPLE__)
  struct timeval tv;
  gettimeofday(&tv, NULL);
  ts.tv_sec  = tv.tv_sec;
  ts.tv_nsec = tv.tv_usec * 1000;
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif

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
int
hts_cond_wait_timeout_abs(hts_cond_t *c, hts_mutex_t *m, int64_t deadline)
{
#if defined(__APPLE__)
  int64_t ts = deadline - arch_get_ts();
  if(ts <= 0)
    return 1;

  return hts_cond_wait_timeout(c, m, ts / 1000);
#else
  struct timespec ts;

  ts.tv_sec  =  deadline / 1000000LL;
  ts.tv_nsec = (deadline % 1000000LL) * 1000;

  return pthread_cond_timedwait(c, m, &ts) == ETIMEDOUT;
#endif
}


/**
 *
 */
extern void
hts_cond_init(hts_cond_t *c, hts_mutex_t *m)
{
#if defined(__APPLE__)
  pthread_cond_init(c, NULL);
#else
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  pthread_cond_init(c, &attr);
  pthread_condattr_destroy(&attr);
#endif
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


const char *
hts_thread_name(char *buf, size_t len)
{
#if defined(linux)
  char tmp[17];
  tmp[16] = 0;
  prctl(PR_GET_NAME, tmp, 0, 0, 0);
  snprintf(buf, len, "%s", tmp);
  return buf;
#elif defined(APPLE)
  pthread_getname_np(pthread_self(), buf, len);
  return buf;
#else
  return "thread-name-unset";
#endif
}

/**
 *
 */
static void *
thread_trampoline(void *aux)
{
  trampoline_t *t = aux;
  void *r;

#if defined(linux)
  errno = 0;
  long tid = syscall(SYS_gettid);
  setpriority(PRIO_PROCESS, tid, t->prio);
  prctl(PR_SET_NAME, t->title, 0, 0, 0);
#elif defined(APPLE)
  pthread_setname_np(t->title);
#endif

  r = t->func(t->aux);
#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_exit_specific();
#endif
  if(gconf.enable_thread_debug)
    TRACE(TRACE_DEBUG, "thread", "Thread %s exited", t->title);

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
  pthread_create(&id, &attr, thread_trampoline,
		 make_trampoline(title, func, aux, prio));
  pthread_attr_destroy(&attr);
  if(gconf.enable_thread_debug)
    tracelog(TRACE_NO_PROP, TRACE_DEBUG,
             "thread", "Created detached thread: %s", title);

}

void
hts_thread_create_joinable(const char *title, hts_thread_t *p, 
			   void *(*func)(void *), void *aux, int prio)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);

#if defined(linux)
  if(prio <= -10 && posix_set_thread_priorities) {
    pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    pthread_attr_setschedpolicy(&attr, SCHED_RR);
    struct sched_param param = {0};
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
  }
#endif

  pthread_create(p, &attr, thread_trampoline,
		 make_trampoline(title, func, aux, prio));
  pthread_attr_destroy(&attr);

  if(gconf.enable_thread_debug)
    tracelog(TRACE_NO_PROP, TRACE_DEBUG,
             "thread", "Created thread: %s", title);
}
