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

#elif defined(__APPLE__)


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
#include "arch/arch.h"
#include <limits.h>
#include <syslog.h>
#include <sys/statvfs.h>
#include <signal.h>
#include "text/text.h"
#include "showtime.h"

#include "networking/net.h"

#include "posix.h"

static int decorate_trace;

/**
 *
 */
void
posix_init(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  srand(tv.tv_usec);

  const char *homedir = getenv("HOME");
  char buf[PATH_MAX];

  if(homedir != NULL) {

    snprintf(buf, sizeof(buf), "%s/.cache/showtime", homedir);
    gconf.cache_path = strdup(buf);

    snprintf(buf, sizeof(buf), "%s/.hts/showtime", homedir);
    gconf.persistent_path = strdup(buf);
  }

  setlocale(LC_ALL, "");
  decorate_trace = isatty(2);

  signal(SIGPIPE, SIG_IGN);
  
  TRACE(TRACE_INFO, "core", "Using %d CPU(s)", gconf.concurrency);

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

  if(gconf.trace_to_syslog)
    openlog("showtime", 0, LOG_USER);
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
  case TRACE_EMERG: sgr = "\033[31m"; prio = LOG_ERR;   break;
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

  if(gconf.trace_to_syslog)
    syslog(prio, "%s %s", prefix, str);
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
int64_t
arch_cache_avail_bytes(void)
{
  struct statvfs buf;

  if(gconf.cache_path == NULL || statvfs(gconf.cache_path, &buf))
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


#if 0
/**
 *
 */
void
arch_preload_fonts(void)
{
#ifdef __APPLE__
  freetype_load_font("file:///Library/Fonts/Arial Unicode.ttf",
		     FONT_DOMAIN_FALLBACK, NULL);
#endif
}
#endif


#include <sys/mman.h>
#include "arch/halloc.h"

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


void
my_localtime(const time_t *now, struct tm *tm)
{
  localtime_r(now, tm);
}


int
arch_pipe(int pipefd[2])
{
  return pipe(pipefd);
}
