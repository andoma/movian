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
#include <assert.h>

#include <netinet/in.h>
#include <net/net.h>
#include <net/netctl.h>

#include <sysmodule/sysmodule.h>
#include <psl1ght/lv2.h>
#include <psl1ght/lv2/spu.h>
#include <lv2/process.h>

#include <rtc.h>

#include "threads.h"
#include "atomic.h"
#include "arch.h"
#include "showtime.h"
#include "service.h"
#include "misc/callout.h"
#include "text/text.h"
#include "notifications.h"
#include "fileaccess/fileaccess.h"
#include "halloc.h"

#if ENABLE_PS3_VDEC
#include "video/ps3_vdec.h"
#endif

// #define EMERGENCY_EXIT_THREAD


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


  Lv2Syscall1(352, (uint64_t) &meminfo);

  prop_set_int(prop_create(memprop, "systotal"), meminfo.total / 1024);
  prop_set_int(prop_create(memprop, "sysfree"), meminfo.avail / 1024);

#if ENABLE_JEMALLOC

#else
  struct mallinfo mi = mallinfo();
  prop_set_int(prop_create(memprop, "arena"), (mi.hblks + mi.arena) / 1024);
  prop_set_int(prop_create(memprop, "unusedChunks"), mi.ordblks);
  prop_set_int(prop_create(memprop, "activeMem"), mi.uordblks / 1024);
  prop_set_int(prop_create(memprop, "inactiveMem"), mi.fordblks / 1024);
#endif

  if(meminfo.avail < LOW_MEM_LOW_WATER && !low_mem_warning) {
    low_mem_warning = 1;
    notify_add(NULL, NOTIFY_ERROR, NULL, 5,
	       _("System is low on memory (%d kB RAM available)"),
	       meminfo.avail / 1024);
  }

  if(meminfo.avail > LOW_MEM_HIGH_WATER)
    low_mem_warning = 0;

}


int
get_system_concurrency(void)
{
  return 2;
}

static char *symbuf;

static void
load_syms(void)
{
  char sympath[256];
  char errbuf[256];

  snprintf(sympath, sizeof(sympath), "%s/showtime.syms", showtime_dataroot());

  fa_handle_t *fh = fa_open(sympath, errbuf, sizeof(errbuf));

  if(fh == NULL) {
    TRACE(TRACE_DEBUG, "SYMS", "Unable to open symbol file %s -- %s",
	  sympath, errbuf);
    return;
  }

  int size = fa_fsize(fh);
  char *buf = halloc(size + 1);

  int r = fa_read(fh, buf, size);
  if(r != size) {
    TRACE(TRACE_DEBUG, "SYMS", "Unable to read %d bytes", size);
    hfree(buf, size+1);
  } else {
    buf[size] = 0;
    TRACE(TRACE_DEBUG, "SYMS", "Loaded symbol table %d bytes to %p",
	  size, buf);
    symbuf = buf;
  }
  fa_close(fh);
}

void
arch_init(void)
{
  extern int trace_level;
  extern int concurrency;

  concurrency = 2;


  trace_level = TRACE_DEBUG;
  sysprop = prop_create(prop_get_global(), "system");
  memprop = prop_create(sysprop, "mem");
  callout_arm(&memlogger, memlogger_fn, NULL, 1);

#if ENABLE_PS3_VDEC
  TRACE(TRACE_DEBUG, "SPU", "Initializing SPUs");
  lv2SpuInitialize(6, 0);
  video_ps3_vdec_init();
#endif
  

  load_syms();
}


void
arch_exit(int retcode)
{
#if ENABLE_BINREPLACE
  extern char *showtime_bin;

  if(retcode == SHOWTIME_EXIT_RESTART)
    sysProcessExitSpawn2(showtime_bin, 0, 0, 0, 0, 1200, 0x70);
#endif

  exit(retcode);
}



LIST_HEAD(thread_info_list, thread_info);

static struct thread_info_list threads;
static hts_mutex_t thread_info_mutex;

typedef struct thread_info {
  LIST_ENTRY(thread_info) link;
  void *aux;
  void *(*fn)(void *);
  sys_ppu_thread_t id;
  char name[64];
} thread_info_t;

extern hts_mutex_t gpool_mutex;

/**
 *
 */
static void
thread_reaper(void *aux)
{
  thread_info_t *ti;
  sys_ppu_thread_icontext_t ctx;

  while(1) {
    sleep(1);

    hts_mutex_lock(&thread_info_mutex);

    LIST_FOREACH(ti, &threads, link) {
      int r = sys_ppu_thread_get_page_fault_context(ti->id, &ctx);
      if(r != -2147418110)
	panic("Thread %s (0x%lx) crashed (r=0x%x)", ti->name, ti->id, r);
    }
    hts_mutex_unlock(&thread_info_mutex);
  }
}


static __thread int my_thread_id;

/**
 *
 */
static void *
thread_trampoline(void *aux)
{
  thread_info_t *ti = aux;

  sys_ppu_thread_get_id(&ti->id);
  my_thread_id = ti->id;
  hts_mutex_lock(&thread_info_mutex);
  LIST_INSERT_HEAD(&threads, ti, link);
  hts_mutex_unlock(&thread_info_mutex);


  void *r = ti->fn(ti->aux);

  TRACE(TRACE_DEBUG, "THREADS", "Thread 0x%x exiting", my_thread_id);

  hts_mutex_lock(&thread_info_mutex);
  LIST_REMOVE(ti, link);
  hts_mutex_unlock(&thread_info_mutex);
  
#if ENABLE_EMU_THREAD_SPECIFICS
  hts_thread_exit_specific();
#endif
  free(ti);

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
  thread_info_t *ti = malloc(sizeof(thread_info_t));
  snprintf(ti->name, sizeof(ti->name), "%s", name);
  ti->fn = fn;
  ti->aux = aux;

  s32 r = sys_ppu_thread_create(p, (void *)thread_trampoline, (intptr_t)ti,
				prio, 65536, flags, (char *)name);
  if(r) {
    my_trace("Failed to create thread %s: error: 0x%x", name, r);
    exit(0);
  }
  
  TRACE(TRACE_DEBUG, "THREADS", "Created thread %s (0x%x)", name, *p);

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


void
hts_thread_join(hts_thread_t *id)
{
  TRACE(TRACE_DEBUG, "THREADS", "Waiting for thread 0x%x", *id);
  sys_ppu_thread_join(*id, NULL);
  TRACE(TRACE_DEBUG, "THREADS", "Thread 0x%x joined", *id);
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
      else if(!strcmp(name, "dev_bdvd") ||
	      !strcmp(name, "dev_ps2disc")) {
	name = "BluRay Drive";
	type = "bluray";
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
  case TRACE_EMERG: sgr = "\033[31m"; break;
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


#ifdef EMERGENCY_EXIT_THREAD
/**
 *
 */
static void *
emergency_thread(void *aux)
{
  struct sockaddr_in si = {0};
  int s;
  int one = 1, r;
  struct pollfd fds;

  s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
  si.sin_family = AF_INET;
  si.sin_port = htons(31337);

  if(bind(s, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
    TRACE(TRACE_ERROR, "ER", "Unable to bind");
    return NULL;
  }

  fds.fd = s;
  fds.events = POLLIN;

  while(1) {
    r = poll(&fds, 1 , 1000);
    if(r > 0 && fds.revents & POLLIN)
      exit(0);
  }
  return NULL;
}
#endif


/**
 *
 */
void
arch_set_default_paths(int argc, char **argv)
{
  char buf[PATH_MAX], *x;

  hts_mutex_init(&thread_info_mutex);


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
  x = strrchr(buf, '/');
  if(x == NULL) {
    my_trace("Showtime starting but argv[0] seems invalid");
    exit(0);
  }
  x++;
  *x = 0;
  showtime_path = strdup(buf);
  strcpy(x, "settings");
  showtime_persistent_path = strdup(buf);
  strcpy(x, "cache");
  showtime_cache_path = strdup(buf);
  SysLoadModule(SYSMODULE_RTC);

  thread_info_t *ti = malloc(sizeof(thread_info_t));
  snprintf(ti->name, sizeof(ti->name), "main");
  sys_ppu_thread_get_id(&ti->id);
  hts_mutex_lock(&thread_info_mutex);
  LIST_INSERT_HEAD(&threads, ti, link);
  hts_mutex_unlock(&thread_info_mutex);

  sys_ppu_thread_t tid;
  s32 r = sys_ppu_thread_create(&tid, (void *)thread_reaper, 0,
				2, 16384, 0, (char *)"reaper");
  if(r) {
    my_trace("Failed to create reaper thread: %x", r);
    exit(0);
  }

#ifdef EMERGENCY_EXIT_THREAD
  r = sys_ppu_thread_create(&tid, (void *)emergency_thread, 0,
				2, 16384, 0, (char *)"emergency");
  if(r) {
    my_trace("Failed to create emergency thread: %x", r);
    exit(0);
  }
#endif
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
  s32 r;

#ifdef PS3_LW_PRIMITIVES
  sys_lwmutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  strcpy(attr.name, "mutex");
  assert(((intptr_t)m & 7) == 0);
  r = sys_lwmutex_create(m, &attr);
#else
  sys_mutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;
  strcpy(attr.name, "mutex");
  r = sys_mutex_create(m, &attr);
#endif

  if(r)
    panic("Failed to create mutex: error: 0x%x", r);
}



void
hts_mutex_init_recursive(hts_mutex_t *m)
{
  s32 r;

#ifdef PS3_LW_PRIMITIVES
  sys_lwmutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_RECURSIVE;
  strcpy(attr.name, "mutex");
  assert(((intptr_t)m & 7) == 0);
  r = sys_lwmutex_create(m, &attr);
#else
  sys_mutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;
  strcpy(attr.name, "mutex");
  r = sys_mutex_create(m, &attr);
#endif

  if(r)
    panic("Failed to create recursive mutex: error: 0x%x", r);

}


void
hts_mutex_lock(hts_mutex_t *m)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_lock(m, 0);
#else
  int r = sys_mutex_lock(*m, 0);
#endif
  if(r)
    panic("mutex_lock(%p) failed 0x%x", m, r);
}


void
hts_mutex_unlock(hts_mutex_t *m)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_unlock(m);
#else
  int r = sys_mutex_unlock(*m);
#endif
  if(r)
    panic("mutex_unlock(%p) failed 0x%x", m, r);
}


void
hts_mutex_destroy(hts_mutex_t *m)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_destroy(m);
#else
  int r = sys_mutex_destroy(*m);
#endif
  if(r)
    panic("mutex_destroy(%p) failed 0x%x", m, r);
}

#else

static void
mtxdolog(hts_mutex_t *mtx, int op, const char *file, int line)
{
  int i = atomic_add(&mtx->logptr, 1);
  i &= 15;
  mtx->log[i].op = op;
  mtx->log[i].file = file;
  mtx->log[i].line = line;
  mtx->log[i].thread = my_thread_id;
#ifdef PS3_LW_PRIMITIVES
  mtx->log[i].lock_var = mtx->mtx.lock_var;
#endif
}


static void
dumplog(hts_mutex_t *mtx)
{
  int i;
  unsigned int p;
  for(i = 0; i < 16;i++) {
    p = (mtx->logptr - 1 - i) & 0xf;
    TRACE(TRACE_EMERG, "ASSERT", "op %d by %s:%d (%x) 0x%016llx",
	  mtx->log[p].op,
	  mtx->log[p].file,
	  mtx->log[p].line,
	  mtx->log[p].thread,
#ifdef PS3_LW_PRIMITIVES
	  mtx->log[p].lock_var
#else
	  0
#endif
	  );
  }
}



void
hts_mutex_initx(hts_mutex_t *m, const char *file, int line, int dbg)
{
  if(dbg)
    my_trace("%s:%d: Init Mutex @ %p by 0x%lx", file, line, m, hts_thread_current());
  s32 r;
  memset(m, 0, sizeof(hts_mutex_t));
  m->dbg = dbg;
  mtxdolog(m, MTX_CREATE, file, line);


#ifdef PS3_LW_PRIMITIVES
  sys_lwmutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  strcpy(attr.name, "mutex");
  r = sys_lwmutex_create(&m->mtx, &attr);
#else
  sys_mutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_NOT_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;
  strcpy(attr.name, "mutex");
  r = sys_mutex_create(&m->mtx, &attr);
#endif

  if(r) {
    my_trace("%s:%d: Failed to create mutex: error: 0x%x", file, line, r);
    exit(0);
  }
}



void
hts_mutex_initx_recursive(hts_mutex_t *m, const char *file, int line)
{
  s32 r;
  memset(m, 0, sizeof(hts_mutex_t));
  m->dbg = 0;
  mtxdolog(m, MTX_CREATE, file, line);

#ifdef PS3_LW_PRIMITIVES
  sys_lwmutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_RECURSIVE;
  strcpy(attr.name, "mutex");
#else
  sys_mutex_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  attr.attr_protocol = MUTEX_PROTOCOL_PRIORITY;
  attr.attr_recursive = MUTEX_RECURSIVE;
  attr.attr_pshared  = 0x00200;
  attr.attr_adaptive = 0x02000;
  strcpy(attr.name, "mutex");
  r = sys_mutex_create(&m->mtx, &attr);
#endif

  if(r) {
    my_trace("%s:%d: Failed to create mutex: error: 0x%x", file, line, r);
    exit(0);
  }
}



void
hts_mutex_lockx(hts_mutex_t *m, const char *file, int line)
{

  if(m->dbg)
    my_trace("%s:%d: Lock Mutex @ %p by 0x%lx", file, line, m, hts_thread_current());
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_lock(&m->mtx, 10000000);
#else
  int r = sys_mutex_lock(m->mtx, 10000000);
#endif
  if(m->dbg)
    my_trace("%s:%d: Lock Mutex @ %p by 0x%lx done", file, line, m, hts_thread_current());

  mtxdolog(m, MTX_LOCK, file, line);

  if(r) {
    TRACE(TRACE_EMERG, "MUTEX",
	  "Mtx %p failed 0x%x lock_var: 0x%016llx", m, r,
#ifdef PS3_LW_PRIMITIVES
	  m->mtx.lock_var
#else
	  0
#endif
	  );
    dumplog(m);
    panic("mutex lock failed");
  }
}

void
hts_mutex_unlockx(hts_mutex_t *m, const char *file, int line)
{
  mtxdolog(m, MTX_UNLOCK, file, line);

  if(m->dbg)
    my_trace("%s:%d: Unlock Mutex @ %p by 0x%lx", file, line, m, hts_thread_current());
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_unlock(&m->mtx);
#else
  int r = sys_mutex_unlock(m->mtx);
#endif
  if(r) {
    TRACE(TRACE_EMERG, "MUTEX",
	  "mutex_unlock(%p) failed 0x%x lock_var: 0x%016llx",
	  m, r,
#ifdef PS3_LW_PRIMITIVES
 m->mtx.lock_var
#else
	  0
#endif
	  );
    dumplog(m);
    panic("mutex unlock failed");
  }
}

void
hts_mutex_destroyx(hts_mutex_t *m, const char *file, int line)
{
  mtxdolog(m, MTX_DESTROY, file, line);

  if(m->dbg)
    my_trace("%s:%d: Destroy Mutex @ %p by 0x%lx", file, line, m, hts_thread_current());
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_destroy(&m->mtx);
#else
  int r = sys_mutex_destroy(m->mtx);
#endif
  if(r) {
    TRACE(TRACE_EMERG, "MUTEX",
	  "mutex_destroy(%p) failed 0x%x lock_var: 0x%016llx", 
	  m, r,
#ifdef PS3_LW_PRIMITIVES
 m->mtx.lock_var
#else
    0
#endif
	  );
    dumplog(m);
    panic("mutex destroy failed: 0x%x", r);
  }
}

#endif

#ifndef PS3_DEBUG_COND

void
hts_cond_init(hts_cond_t *c, hts_mutex_t *m)
{
  s32 r;
#ifdef PS3_LW_PRIMITIVES
  sys_lwcond_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  strcpy(attr.name, "cond");
#ifdef PS3_DEBUG_MUTEX
  r = sys_lwcond_create(c, &m->mtx, &attr);
#else
  r = sys_lwcond_create(c, m, &attr);
#endif

#else

  sys_cond_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  strcpy(attr.name, "cond");
  attr.attr_pshared = 0x00200;
#ifdef PS3_DEBUG_MUTEX
  r = sys_cond_create(c, m->mtx, &attr);
#else
  r = sys_cond_create(c, *m, &attr);
#endif
 
#endif
  if(r) {
    my_trace("Failed to create cond: error: 0x%x", r);
    exit(0);
  }
}


void hts_cond_destroy(hts_cond_t *c)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwcond_destroy(c);
#else
  int r = sys_cond_destroy(*c);
#endif
  if(r == 0)
    return;
  panic("cond_destroy(%p) failed 0x%x", c, r);
}


void hts_cond_signal(hts_cond_t *c)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwcond_signal(c);
#else
  int r = sys_cond_signal(*c);
#endif
  if(r == 0)
    return;
  panic("cond_signal(%p) failed 0x%x", c, r);
}



void hts_cond_broadcast(hts_cond_t *c)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwcond_signal_all(c);
#else
  int r = sys_cond_signal_all(*c);
#endif
  if(r == 0)
    return;
  panic("cond_signal_all(%p) failed 0x%x", c, r);
}

int
hts_cond_wait_timeout(hts_cond_t *c, hts_mutex_t *m, int delay)
{
#ifdef PS3_LW_PRIMITIVES
  unsigned int r = sys_lwcond_wait(c, delay * 1000LL);
#else
  unsigned int r = sys_cond_wait(*c, delay * 1000LL);
#endif
  if(r == 0x8001000B)
    return 1;
  if(r == 0)
    return 0;
  panic("cond_wait_timeout(%p, %d) failed 0x%x", c, delay, r);
}


#else // condvar debugging


void
hts_cond_initx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Init cond @ %p,%p by 0x%lx", file, line, c, m, hts_thread_current());
  sys_lwcond_attribute_t attr;
  memset(&attr, 0, sizeof(attr));
  strcpy(attr.name, "cond");
  s32 r = sys_lwcond_create(c, m, &attr);
  if(r) {
    my_trace("%s:%d: Failed to create cond: error: 0x%x", file, line, r);
    exit(0);
  }
}


void hts_cond_destroyx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Destroy cond @ %p by 0x%lx", file, line, c, hts_thread_current());
  sys_lwcond_destroy(c);
}

void hts_cond_signalx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Signal cond @ %p by 0x%lx", file, line, c, hts_thread_current());
  sys_lwcond_signal(c);
}

void hts_cond_broadcastx(hts_cond_t *c, const char *file, int line)
{
  my_trace("%s:%d: Broadcast cond @ %p by 0x%lx", file, line, c, hts_thread_current());
  sys_lwcond_signal_all(c);
}

void hts_cond_waitx(hts_cond_t *c, hts_mutex_t *m, const char *file, int line)
{
  my_trace("%s:%d: Wait cond @ %p %p by 0x%lx", file, line, c, m, hts_thread_current());
  sys_lwcond_wait(c, 0);
  my_trace("%s:%d: Wait cond @ %p %p by 0x%lx => done", file, line, c, m, hts_thread_current());
}

int hts_cond_wait_timeoutx(hts_cond_t *c, hts_mutex_t *m, int delay, const char *file, int line)
{
  my_trace("%s:%d: Wait cond @ %p %p %dms by 0x%lx", file, line, c, m, delay, hts_thread_current());
  s32 v = sys_lwcond_wait(c, delay * 1000LL);
  my_trace("%s:%d: Wait cond @ %p %p %dms by 0x%lx => %d", file, line, c, m, delay, hts_thread_current(), v);
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
    panic("halloc(%d) failed", (int)size);

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


#define BT_MAX    64
#define BT_IGNORE 1

static int
backtrace(void **vec)
{

#define	BT_FRAME(i)							\
  if ((i) < BT_IGNORE + BT_MAX) {					\
    void *p;								\
    if (__builtin_frame_address(i) == 0)				\
      return i - BT_IGNORE;						\
    p = __builtin_return_address(i);					\
    if (p == NULL || (intptr_t)p < 0x11000)				\
      return i - BT_IGNORE;						\
    if (i >= BT_IGNORE) {						\
      vec[i - BT_IGNORE] = p-4;						\
    }									\
  } else								\
    return i - BT_IGNORE;

	BT_FRAME(0)
	BT_FRAME(1)
	BT_FRAME(2)
	BT_FRAME(3)
	BT_FRAME(4)
	BT_FRAME(5)
	BT_FRAME(6)
	BT_FRAME(7)
	BT_FRAME(8)
	BT_FRAME(9)

	BT_FRAME(10)
	BT_FRAME(11)
	BT_FRAME(12)
	BT_FRAME(13)
	BT_FRAME(14)
	BT_FRAME(15)
	BT_FRAME(16)
	BT_FRAME(17)
	BT_FRAME(18)
	BT_FRAME(19)

	BT_FRAME(20)
	BT_FRAME(21)
	BT_FRAME(22)
	BT_FRAME(23)
	BT_FRAME(24)
	BT_FRAME(25)
	BT_FRAME(26)
	BT_FRAME(27)
	BT_FRAME(28)
	BT_FRAME(29)

	BT_FRAME(30)
	BT_FRAME(31)
	BT_FRAME(32)
	BT_FRAME(33)
	BT_FRAME(34)
	BT_FRAME(35)
	BT_FRAME(36)
	BT_FRAME(37)
	BT_FRAME(38)
	BT_FRAME(39)

	BT_FRAME(40)
	BT_FRAME(41)
	BT_FRAME(42)
	BT_FRAME(43)
	BT_FRAME(44)
	BT_FRAME(45)
	BT_FRAME(46)
	BT_FRAME(47)
	BT_FRAME(48)
	BT_FRAME(49)

	BT_FRAME(50)
	BT_FRAME(51)
	BT_FRAME(52)
	BT_FRAME(53)
	BT_FRAME(54)
	BT_FRAME(55)
	BT_FRAME(56)
	BT_FRAME(57)
	BT_FRAME(58)
	BT_FRAME(59)

	BT_FRAME(60)
	BT_FRAME(61)
	BT_FRAME(62)
	BT_FRAME(63)
	BT_FRAME(64)
	  return 64;
}


void
__assert_func(const char *file, int line,
	      const char *func, const char *failedexpr);

void
__assert_func(const char *file, int line,
	      const char *func, const char *failedexpr)
{
  panic("Assertion failed %s:%d in %s (%s)", file, line, func, failedexpr);
}


static void
resolve_syms(void **ptr, const char **symvec, int *symoffset, int frames)
{
  char *s = symbuf;

  int i;
  for(i = 0; i < frames; i++) {
    symvec[i] = NULL;
    symoffset[i] = 0;
  }

  if(s == NULL)
    return;
  
  while(s) {
    int64_t addr = strtol(s, NULL, 16);
    if(addr > 0x10000) {
      for(i = 0; i < frames; i++) {
	int64_t a0 = (intptr_t)ptr[i];
	if(a0 >= addr) {
	  symvec[i] = s + 17;
	  symoffset[i] = a0 - addr;
	}
      }
    }

    s = strchr(s, '\n');
    if(s == NULL)
      return;
    *s++ = 0;
  }
}

void
panic(const char *fmt, ...)
{
  va_list ap;
  void *vec[64];
  const char *sym[64];
  int symoffset[64];

  va_start(ap, fmt);
  tracev(0, TRACE_EMERG, "PANIC", fmt, ap);
  va_end(ap);

  int frames = backtrace(vec);
  int i;
  resolve_syms(vec, sym, symoffset, frames);
  for(i = 0; i < frames; i++) 
    if(sym[i])
      TRACE(TRACE_EMERG, "BACKTRACE", "%p: %s+0x%x", vec[i], sym[i], symoffset[i]);
    else
      TRACE(TRACE_EMERG, "BACKTRACE", "%p", vec[i]);

  hts_thread_t tid = hts_thread_current();

  TRACE(TRACE_EMERG, "PANIC", "Thread list (self=0x%lx)", tid);

  thread_info_t *ti;
  LIST_FOREACH(ti, &threads, link) 
    TRACE(TRACE_EMERG, "PANIC", "0x%lx: %s", ti->id, ti->name);
  exit(1);
}
