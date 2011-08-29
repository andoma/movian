/*
 *  Arch specifics for POSIX (and equivivalent systems)
 *
 *  Copyright (C) 2008 Andreas Öman
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

/*
 * Note: Do not place #includes here.
 *
 * #define _GNU_SOURCE
 * #include <sched.h>
 * 
 * Must be first or compilation might fail on linux
 *
 */
const char *showtime_get_system_type(void);

#ifdef linux

#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <sys/prctl.h>
#include "linux.h"


const char *
showtime_get_system_type(void)
{
#if defined(__i386__)
  return "Linux/i386";
#elif defined(__x86_64__)
  return "Linux/x86_64";
#elif defined(__arm__)
  return "Linux/arm";
#else
  return "Linux/other";
#endif
}

static int
get_system_concurrency(void)
{
  cpu_set_t mask;
  int i, r = 0;

  memset(&mask, 0, sizeof(mask));
  sched_getaffinity(0, sizeof(mask), &mask);
  for(i = 0; i < CPU_SETSIZE; i++)
    if(CPU_ISSET(i, &mask))
      r++;
  return r?:1;
}

#elif defined(__APPLE__)

const char *
showtime_get_system_type(void)
{
  return "Apple";
}

#include <sys/types.h>
#include <sys/sysctl.h>
#include "darwin.h"

static int
get_system_concurrency(void)
{
  int mib[2];
  int ncpu;
  size_t len;

  mib[0] = CTL_HW;
  mib[1] = HW_NCPU;
  len = sizeof(ncpu);
  sysctl(mib, 2, &ncpu, &len, NULL, 0);

  return ncpu;

}

#else

static int
get_system_concurrency(void)
{
  return 1;
}

#endif


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include "arch.h"
#include <limits.h>
#include <syslog.h>
#include <sys/statvfs.h>

#ifdef XBMC_PLUGIN
#include "xbmc-plugin.h"
#else
#include "showtime.h"
#endif

#include "networking/net.h"

extern int concurrency;
extern int trace_to_syslog;
static int decorate_trace;

/**
 *
 */
void
arch_init(void)
{
#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_key_init();
#endif

  setlocale(LC_ALL, "");
  concurrency = get_system_concurrency();
  decorate_trace = isatty(2);

  TRACE(TRACE_INFO, "core", "Using %d CPU(s)", concurrency);

#ifdef RLIMIT_AS
  do {
    struct rlimit rlim;
    getrlimit(RLIMIT_AS, &rlim);
    rlim.rlim_cur = 512 * 1024 * 1024;
    setrlimit(RLIMIT_AS, &rlim);
  } while(0);
#endif

#ifdef RLIMIT_DATA
  do {
    struct rlimit rlim;
    getrlimit(RLIMIT_DATA, &rlim);
    rlim.rlim_cur = 512 * 1024 * 1024;
    setrlimit(RLIMIT_DATA, &rlim);
  } while(0);
#endif

  net_initialize();

  if(trace_to_syslog)
    openlog("showtime", 0, LOG_USER);
}

/**
 *
 */
void
arch_exit(int retcode)
{
  exit(retcode);
}


#include <errno.h>
/**
 *
 */
int
hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delta)
{
  struct timespec ts;

#ifdef __APPLE__
  /* darwin does not have clock_gettime */
  struct timeval tv;
  gettimeofday(&tv, NULL);
  TIMEVAL_TO_TIMESPEC(&tv, &ts);
#else
  clock_gettime(CLOCK_REALTIME, &ts);
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
void
trace_arch(int level, const char *prefix, const char *str)
{
  const char *sgr, *sgroff;
  int prio = LOG_ERR;

  switch(level) {
  case TRACE_ERROR: sgr = "\033[31m"; prio = LOG_ERR;   break;
  case TRACE_INFO:  sgr = "\033[33m"; prio = LOG_INFO;  break;
  case TRACE_DEBUG: sgr = "\033[32m"; prio = LOG_DEBUG; break;
  default:          sgr = "\033[35m"; break;
  }

  if(!decorate_trace) {
    sgr = "";
    sgroff = "";
  } else {
    sgroff = "\033[0m";
  }

  fprintf(stderr, "%s%s %s%s\n", sgr, prefix, str, sgroff);

  if(trace_to_syslog)
    syslog(prio, "%s %s", prefix, str);
}


/**
 *
 */
void
arch_sd_init(void)
{
#ifdef linux
  linux_init_cpu_monitor();
  trap_init();
#elif defined(__APPLE__)
  darwin_init_cpu_monitor();
#endif
}


/**
 *
 */
int64_t
showtime_get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}


/**
 *
 */
typedef struct {
  char *title;
  void *(*func)(void *);
  void *aux;
} trampoline_t;


/**
 *
 */
static trampoline_t *
make_trampoline(const char *title, void *(*func)(void *), void *aux)
{
  char buf[64];
  trampoline_t *t = malloc(sizeof(trampoline_t));
  snprintf(buf, sizeof(buf), "ST:%s", title);
  
  t->title = strdup(buf);
  t->func = func;
  t->aux = aux;
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

#ifdef linux
  prctl(PR_SET_NAME, t->title, 0, 0, 0);
#endif
  free(t->title);

  r = t->func(t->aux);
#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_exit_specific();
#endif

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
		 make_trampoline(title, func, aux));
  pthread_attr_destroy(&attr);
  TRACE(TRACE_DEBUG, "thread", "Created detached thread: %s", title);

}

void
hts_thread_create_joinable(const char *title, hts_thread_t *p, 
			   void *(*func)(void *), void *aux, int prio)
{
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 128 * 1024);
  pthread_create(p, &attr, thread_trampoline,
		make_trampoline(title, func, aux));
  pthread_attr_destroy(&attr);

  TRACE(TRACE_DEBUG, "thread", "Created thread: %s", title);
}


/**
 *
 */
void
arch_set_default_paths(int argc, char **argv)
{
  const char *homedir = getenv("HOME");
  char buf[PATH_MAX];

  if(homedir == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/.cache/showtime", homedir);
  showtime_cache_path = strdup(buf);


  snprintf(buf, sizeof(buf), "%s/.hts/showtime", homedir);
  showtime_persistent_path = strdup(buf);
}

int64_t
arch_cache_avail_bytes(void)
{
  struct statvfs buf;

  if(showtime_cache_path == NULL || statvfs(showtime_cache_path, &buf))
    return 0;

  return buf.f_bfree * buf.f_bsize;
}

/**
 *
 */
uint64_t
arch_get_seed(void)
{
  uint64_t v = getpid();
  v = (v << 16) ^ getppid();
  v = (v << 32) ^ time(NULL);
  return v;
}


/**
 *
 */
void
arch_preload_fonts(void)
{
}


#include <sys/mman.h>
#include "halloc.h"

/**
 *
 */
void *
halloc(size_t size)
{
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(p == MAP_FAILED)
    return NULL;
  return p;
}

/**
 *
 */
void
hfree(void *ptr, size_t size)
{
  munmap(ptr, size);
}
