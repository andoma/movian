/*
 *  Arch specifics for PS3
 *
 *  Copyright (C) 2011 Andreas Öman
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>

#include <netinet/in.h>
#include <net/net.h>

#include <psl1ght/lv2.h>

#include "threads.h"
#include "atomic.h"
#include "arch.h"
#include "showtime.h"
#include "service.h"
#include "misc/callout.h"
static void my_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static uint64_t ticks_per_us;

static callout_t memlogger;


static uint64_t
mftb(void)
{
  uint64_t ret;
  asm volatile ("1: mftb %[tmp];       "
		"   cmpwi 7, %[tmp], 0;"
		"   beq-  7, 1b;       "
		: [tmp] "=r" (ret):: "cr7");
  return ret;
}

static void
memlogger_fn(callout_t *co, void *aux)
{
  callout_arm(&memlogger, memlogger_fn, NULL, 1);
  struct {
    uint32_t total;
    uint32_t avail;
  } meminfo;

  Lv2Syscall1(352, (uint64_t) &meminfo);

  TRACE(TRACE_INFO, "MEM-LV2", "Total = %d kB, Alloced = %d kB",
	meminfo.total / 1024, (meminfo.total - meminfo.avail) / 1024);

  struct mallinfo info;
  info = mallinfo();

  TRACE(TRACE_INFO, "LIBC", "Arena = %d kB, Alloced = %d kB, Non Inuse = %d kB",
	info.arena / 1024, info.uordblks / 1024, info.fordblks / 1024);

}


void
arch_init(void)
{
  extern int trace_level;

#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_key_init();
#endif

  trace_level = TRACE_DEBUG;
  callout_arm(&memlogger, memlogger_fn, NULL, 1);
}


void
arch_exit(int retcode)
{
  exit(retcode);
}



typedef struct {
  void *aux;
  void *(*fn)(void *);

} thread_aux_t;



static void *
thread_trampoline(void *aux)
{
  thread_aux_t *ta = aux;
  void *r = ta->fn(ta->aux);
#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_exit_specific();
#endif
  free(ta);
  return r;
}


static void
start_thread(const char *name, hts_thread_t *p,
	     void *(*fn)(void *), void *aux,
	     int prio, int flags)
{
  thread_aux_t *ta = malloc(sizeof(thread_aux_t));
  ta->fn = fn;
  ta->aux = aux;
  s32 r = sys_ppu_thread_create(p, (void *)thread_trampoline, (intptr_t)ta,
				prio, 65536, flags, (char *)name);
  if(r) {
    my_trace("Failed to create thread %s: error: 0x%x", name, r);
    exit(0);
  }

}




void
hts_thread_create_detached(const char *name, void *(*fn)(void *), void *aux,
			   int prio)
{
  hts_thread_t tid;
  start_thread(name, &tid, fn, aux, prio, 0);
}


void
hts_thread_create_joinable(const char *name, hts_thread_t *p,
			   void *(*fn)(void *), void *aux, int prio)
{
  start_thread(name, p, fn, aux, prio, THREAD_JOINABLE);
}


/**
 *
 */
hts_thread_t
hts_thread_current(void)
{
  hts_thread_t t;
  sys_ppu_thread_get_id(&t);
  return t;
}


/**
 *
 */
int64_t
showtime_get_ts(void)
{
  return mftb() / ticks_per_us;
}


/**
 *
 */
void
arch_sd_init(void)
{
  service_create("Root file system", "file:///",
		 "other", NULL, 0, 1);
  service_create("media", "webdav://172.31.255.1/media",
		 "other", NULL, 0, 1);
}



static int trace_fd = -1;

#define TRACEIP		"172.31.255.19"
#define TRACEPORT	4000


static int tracetally;

void
my_trace(const char *fmt, ...)
{
  char msg[1000];
  va_list ap;

  int v = atomic_add(&tracetally, 1);

  snprintf(msg, 11, "%08x: ", v);

  va_start(ap, fmt);
  vsnprintf(msg + 10, sizeof(msg) - 10, fmt, ap);
  va_end(ap);

  if(trace_fd == -1) {
    trace_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
  }

  struct sockaddr_in server;
  memset(&server, 0, sizeof(server));
  server.sin_len = sizeof(server);
  server.sin_family = AF_INET;
  inet_pton(AF_INET, TRACEIP, &server.sin_addr);
  server.sin_port = htons(TRACEPORT);

  sendto(trace_fd, msg, strlen(msg), 0,
	 (struct sockaddr*)&server, sizeof(server));
}


static int decorate_trace = 1;

/**
 *
 */
void
trace_arch(int level, const char *prefix, const char *str)
{
  const char *sgr, *sgroff;

  switch(level) {
  case TRACE_ERROR: sgr = "\033[31m"; break;
  case TRACE_INFO:  sgr = "\033[33m"; break;
  case TRACE_DEBUG: sgr = "\033[32m"; break;
  default:          sgr = "\033[35m"; break;
  }

  if(!decorate_trace) {
    sgr = "";
    sgroff = "";
  } else {
    sgroff = "\033[0m";
  }

  my_trace("%s%s %s%s\n", sgr, prefix, str, sgroff);
}

/**
 *
 */
void
arch_set_default_paths(int argc, char **argv)
{
  char buf[PATH_MAX], *x;

  netInitialize();

  ticks_per_us = Lv2Syscall0(147) / 1000000;
  my_trace("Ticks per µs = %ld\n", ticks_per_us);


  if(argc == 0) {
    my_trace("Showtime starting from ???\n");
    return;
  }
  my_trace("Showtime starting from %s\n", argv[0]);
  snprintf(buf, sizeof(buf), "%s", argv[0]);
  x = strstr(buf, "/EBOOT.BIN");
  if(x == NULL)
    return;
  x++;
  strcpy(x, "settings");
  showtime_settings_path = strdup(buf);
  strcpy(x, "cache");
  showtime_cache_path = strdup(buf);
}

#ifndef PS3_DEBUG_MUTEX

void
hts_mutex_init(hts_mutex_t *m)
{
  sys_mutex_attribute_t attr;
  s32 r;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_FIFO;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;

  strcpy(attr.name, "mutex");

  if((r = sys_mutex_create(m, &attr)) != 0) {
    my_trace("Failed to create mutex: error: 0x%x", r);
    exit(0);
  }
}


#else

void
hts_mutex_initx(hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Init Mutex @ %p by %ld", file, line, m, hts_thread_current());
  s32 r;
  sys_mutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_FIFO;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;

  strcpy(attr.name, "mutex");

  r = sys_mutex_create(m, &attr);
  if(r) {
    my_trace("%s:%d: Failed to create mutex: error: 0x%x", file, line, r);
    exit(0);
  }
}


void
hts_mutex_lockx(hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Lock Mutex @ %p by %ld", file, line, m, hts_thread_current());
  sys_mutex_lock(*m, 0);
  my_trace("%s:%d: Lock Mutex @ %p by %ld done", file, line, m, hts_thread_current());

}

void
hts_mutex_unlockx(hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Unlock Mutex @ %p by %ld", file, line, m, hts_thread_current());
  sys_mutex_unlock(*m);
}

void
hts_mutex_destroyx(hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Destroy Mutex @ %p by %ld", file, line, m, hts_thread_current());
  sys_mutex_destroy(*m);
}
#endif



#ifndef PS3_DEBUG_COND

void
hts_cond_init(hts_cond_t *c, hts_mutex_t *m)
{
  sys_cond_attribute_t attr;
  s32 r;
  memset(&attr, 0, sizeof(attr));
  attr.attr_pshared = 0x00200;
  strcpy(attr.name, "cond");
  if((r = sys_cond_create(c, *m, &attr) != 0)) {
    my_trace("Failed to create cond: error: 0x%x", r);
    exit(0);
  }
}



int
hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delay)
{
  return !!sys_cond_wait(*c, delay * 1000LL);
}


#else // condvar debugging


void
hts_cond_initx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Init cond @ %p,%p by %ld", file, line, c, m, hts_thread_current());
  sys_cond_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_pshared = 0x00200;
  strcpy(attr.name, "cond");
  s32 r = sys_cond_create(c, *m, &attr);
  if(r) {
    my_trace("%s:%d: Failed to create cond: error: 0x%x", file, line, r);
    exit(0);
  }
}


void hts_cond_destroyx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Destroy cond @ %p by %ld", file, line, c, hts_thread_current());
  sys_cond_destroy(*c);
}

void hts_cond_signalx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Signal cond @ %p by %ld", file, line, c, hts_thread_current());
  sys_cond_signal(*c);
}

void hts_cond_broadcastx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Broadcast cond @ %p by %ld", file, line, c, hts_thread_current());
  sys_cond_signal_all(*c);
}

void hts_cond_waitx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Wait cond @ %p %p by %ld", file, line, c, m, hts_thread_current());
  sys_cond_wait(*c, 0);
  my_trace("%s:%d: Wait cond @ %p %p by %ld => done", file, line, c, m, hts_thread_current());
}

int hts_cond_wait_timeoutx(hts_cond_t *c, hts_mutex_t *m, int delay, const char *file, int line)
{
  my_trace("%s:%d: Wait cond @ %p %p %dms by %ld", file, line, c, m, delay, hts_thread_current());
  s32 v = sys_cond_wait(*c, delay * 1000LL);
  my_trace("%s:%d: Wait cond @ %p %p %dms by %ld => %d", file, line, c, m, delay, hts_thread_current(), v);
  return !!v;
}

#endif
