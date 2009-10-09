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
#ifdef linux

#define _GNU_SOURCE
#include <sched.h>
#include <pthread.h>
#include <string.h>

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

#include <sys/types.h>
#include <sys/sysctl.h>

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

#else /* linux */

static int
get_system_concurrency(void)
{
  return 1;
}

#endif /* linux */


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>
#include "arch.h"
#include "showtime.h"

extern int concurrency;

static int decorate_trace;

/**
 *
 */
void
arch_init(void)
{
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
}

/**
 *
 */
void
arch_exit(int retcode)
{
  _exit(retcode);
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




extern int trace_level;

/**
 *
 */
void
tracev(int level, const char *subsys, const char *fmt, va_list ap)
{
  char buf[1024];
  char buf2[64];
  char *s, *p;
  const char *leveltxt, *sgr, *sgroff;
  int l;

  if(level > trace_level)
    return;

  switch(level) {
  case TRACE_ERROR: leveltxt = "ERROR"; sgr = "\033[31m"; break;
  case TRACE_INFO:  leveltxt = "INFO";  sgr = "\033[33m"; break;
  case TRACE_DEBUG: leveltxt = "DEBUG"; sgr = "\033[32m"; break;
  default:          leveltxt = "?????"; sgr = "\033[35m"; break;
  }

  if(!decorate_trace) {
    sgr = "";
    sgroff = "";
  } else {
    sgroff = "\033[0m";
  }

  vsnprintf(buf, sizeof(buf), fmt, ap);

  p = buf;

  snprintf(buf2, sizeof(buf2), "%s [%s]:", subsys, leveltxt);
  l = strlen(buf2);

  while((s = strsep(&p, "\n")) != NULL) {
    fprintf(stderr, "%s%s %s%s\n", sgr, buf2, s, sgroff);
    memset(buf2, ' ', l);
  }
}


/**
 *
 */
void
arch_sd_init(void)
{

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
