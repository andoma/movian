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

#include <sys/types.h>
#include <sys/stat.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <dirent.h>

#include <netinet/in.h>
#include <net/net.h>
#include <net/netctl.h>

#include <sysmodule/sysmodule.h>
#include <psl1ght/lv2.h>
#include <psl1ght/lv2/spu.h>
#include <rtc.h>

#include "threads.h"
#include "atomic.h"
#include "arch.h"
#include "showtime.h"
#include "service.h"
#include "misc/callout.h"
#include "text/text.h"
#include "notifications.h"

#if ENABLE_PS3_VDEC
#include "video/ps3_vdec.h"
#endif

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

static prop_t *sysprop;
static prop_t *memprop;

#define LOW_MEM_LOW_WATER  20 * 1024 * 1024
#define LOW_MEM_HIGH_WATER 30 * 1024 * 1024


static void
memlogger_fn(callout_t *co, void *aux)
{
  static int low_mem_warning;

  callout_arm(&memlogger, memlogger_fn, NULL, 1);

  struct {
    uint32_t total;
    uint32_t avail;
  } meminfo;

  struct mallinfo mi = mallinfo();

  Lv2Syscall1(352, (uint64_t) &meminfo);

  prop_set_int(prop_create(memprop, "systotal"), meminfo.total / 1024);
  prop_set_int(prop_create(memprop, "sysfree"), meminfo.avail / 1024);
  prop_set_int(prop_create(memprop, "arena"), (mi.hblks + mi.arena) / 1024);
  prop_set_int(prop_create(memprop, "unusedChunks"), mi.ordblks);
  prop_set_int(prop_create(memprop, "activeMem"), mi.uordblks / 1024);
  prop_set_int(prop_create(memprop, "inactiveMem"), mi.fordblks / 1024);


  if(meminfo.avail < LOW_MEM_LOW_WATER && !low_mem_warning) {
    low_mem_warning = 1;
    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("System is low on memory (%d kB RAM available)"),
	       meminfo.avail / 1024);
  }

  if(meminfo.avail > LOW_MEM_HIGH_WATER)
    low_mem_warning = 0;

}


void
arch_init(void)
{
  extern int trace_level;
  extern int concurrency;

  concurrency = 2;

#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_key_init();
#endif

  trace_level = TRACE_DEBUG;
  sysprop = prop_create(prop_get_global(), "system");
  memprop = prop_create(sysprop, "mem");
  callout_arm(&memlogger, memlogger_fn, NULL, 1);

#if ENABLE_PS3_VDEC
  TRACE(TRACE_DEBUG, "SPU", "Initializing SPUs");
  lv2SpuInitialize(6, 0);
  video_ps3_vdec_init();
#endif

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

  extern int netFreethreadContext(long long, int);

  netFreethreadContext(0, 1);

  sys_ppu_thread_exit(0);
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
typedef struct rootfsnode {
  LIST_ENTRY(rootfsnode) link;
  char *name;
  service_t *service;
  int mark;
} rootfsnode_t;
 
static LIST_HEAD(, rootfsnode) rootfsnodes;

/**
 *
 */
static void
scan_root_fs(callout_t *co, void *aux)
{
  struct dirent *d;
  struct stat st;
  DIR *dir;
  char fname[32];
  char dpyname[32];
  rootfsnode_t *rfn, *next;

  LIST_FOREACH(rfn, &rootfsnodes, link)
    rfn->mark = 1;

  callout_arm(co, scan_root_fs, NULL, 1);

  if((dir = opendir("/")) == NULL)
    return;

  while((d = readdir(dir)) != NULL) {
    if(strncmp(d->d_name, "dev_", strlen("dev_")))
      continue;
    if(!strncmp(d->d_name, "dev_flash", strlen("dev_flash")))
      continue;

    snprintf(fname, sizeof(fname), "/%s", d->d_name);
    if(stat(fname, &st))
      continue;

    if((st.st_mode & S_IFMT) != S_IFDIR)
      continue;

    LIST_FOREACH(rfn, &rootfsnodes, link)
      if(!strcmp(rfn->name, d->d_name))
	break;

    if(rfn == NULL) {
      rfn = malloc(sizeof(rootfsnode_t));
      rfn->name = strdup(d->d_name);

      snprintf(fname, sizeof(fname), "file:///%s", d->d_name);

      const char *name = d->d_name;
      const char *type = "other";
      if(!strcmp(name, "dev_hdd0"))
	name = "PS3 HDD";
      else if(!strncmp(name, "dev_usb", strlen("dev_usb"))) {
	snprintf(dpyname, sizeof(dpyname), "USB Drive %d",
		 atoi(name + strlen("dev_usb")));
	type = "usb";
	name = dpyname;
      }

      rfn->service = service_create(name, fname, type, NULL, 0, 1);
      LIST_INSERT_HEAD(&rootfsnodes, rfn, link);
    }
    rfn->mark = 0;
  }
  closedir(dir);
  
  for(rfn = LIST_FIRST(&rootfsnodes); rfn != NULL; rfn = next) {
    next = LIST_NEXT(rfn, link);
    if(!rfn->mark)
      continue;

    LIST_REMOVE(rfn, link);
    service_destroy(rfn->service);
    free(rfn->name);
    free(rfn);
  }

}



/**
 *
 */
void
arch_sd_init(void)
{
  static callout_t co;
  scan_root_fs(&co, NULL);
}



static int trace_fd = -1;
static struct sockaddr_in log_server;
extern const char *showtime_logtarget;

void
my_trace(const char *fmt, ...)
{
  char msg[1000];
  va_list ap;

  if(trace_fd == -2)
    return;

  if(trace_fd == -1) {
    int port = 4000;
    char *p;

    log_server.sin_len = sizeof(log_server);
    log_server.sin_family = AF_INET;
    
    snprintf(msg, sizeof(msg), "%s", showtime_logtarget);
    p = strchr(msg, ':');
    if(p != NULL) {
      *p++ = 0;
      port = atoi(p);
    }
    log_server.sin_port = htons(port);
    if(inet_pton(AF_INET, msg, &log_server.sin_addr) != 1) {
      trace_fd = -2;
      return;
    }

    trace_fd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(trace_fd == -1)
      return;
  }

  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  sendto(trace_fd, msg, strlen(msg), 0,
	 (struct sockaddr*)&log_server, sizeof(log_server));
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
  netCtlInit();

  
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
  showtime_persistent_path = strdup(buf);
  strcpy(x, "cache");
  showtime_cache_path = strdup(buf);
  SysLoadModule(SYSMODULE_RTC);
}


int64_t
arch_cache_avail_bytes(void)
{
  return 1024 * 1024 * 1024;
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



void
hts_mutex_init_recursive(hts_mutex_t *m)
{
  sys_mutex_attribute_t attr;
  s32 r;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_FIFO;
  attr.attr_recursive = MUTEX_RECURSIVE;
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

/**
 *
 */
uint64_t
arch_get_seed(void)
{
  return mftb();
}



/**
 *
 */
void
arch_preload_fonts(void)
{
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-VR-R-LATIN2.TTF");
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-NR-R-JPN.TTF");
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-YG-R-KOR.TTF");
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-DH-R-CGB.TTF");
  freetype_load_font("file:///dev_flash/data/font/SCE-PS3-CP-R-KANA.TTF");
}

const char *
showtime_get_system_type(void)
{
  return "PS3";
}



#include "halloc.h"

/**
 *
 */
void *
halloc(size_t size)
{
#define ROUND_UP(p, round) ((p + (round) - 1) & ~((round) - 1))

  size_t allocsize = ROUND_UP(size, 64*1024);
  u32 taddr;

  if(Lv2Syscall3(348, allocsize, 0x200, (u64)&taddr))
    return NULL;

  return (void *)(uint64_t)taddr;
}


/**
 *
 */
void
hfree(void *ptr, size_t size)
{
  Lv2Syscall1(349, (uint64_t)ptr);
}


void
my_localtime(const time_t *now, struct tm *tm)
{
  rtc_datetime dt;
  rtc_tick utc, local;

  rtc_convert_time_to_datetime(&dt, *now);
  rtc_convert_datetime_to_tick(&dt, &utc);
  rtc_convert_utc_to_localtime(&utc, &local);
  rtc_convert_tick_to_datetime(&dt, &local);

  memset(tm, 0, sizeof(struct tm));

  tm->tm_year = dt.year - 1900;
  tm->tm_mon  = dt.month - 1;
  tm->tm_mday = dt.day;
  tm->tm_hour = dt.hour;
  tm->tm_min  = dt.minute;
  tm->tm_sec  = dt.second;
}
