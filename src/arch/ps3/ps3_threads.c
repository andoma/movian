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


#include <unistd.h>
#include <stdio.h>

#include <psl1ght/lv2.h>

#include "showtime.h"
#include "ps3.h"
#include "ps3_threads.h"


struct thread_info_list threads;
hts_mutex_t thread_info_mutex;


/**
 *
 */
void
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

  TRACE(TRACE_DEBUG, "THREADS", "Thread 0x%x (%s) exiting",
	my_thread_id, ti->name);

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
				prio, 131072, flags, (char *)name);
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


int
hts_mutex_trylock(hts_mutex_t *m)
{
#ifdef PS3_LW_PRIMITIVES
  int r = sys_lwmutex_trylock(m);
#else
  int r = sys_mutex_trylock(*m);
#endif
  return !!r;
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




typedef struct {
  sys_mutex_attribute_t	attr;
  sys_ppu_thread_t	owner;
  int			lock_counter;
  int			cond_ref_counter;
  sys_cond_t		cond_id;		// Always Zero (0) in this field
  uint32_t              A_POINTER;
  uint32_t		wait_threads_num;
  uint32_t		wait_all_threads_num;
} sys_dbg_mutex_information_t;




void
mutex_dump_info(sys_mutex_t lock)
{
  // sys_process_getpid() // syscall 1

  sys_dbg_mutex_information_t info;

  int pid = Lv2Syscall0(1);
  TRACE(TRACE_DEBUG, "MUTEX", "I am pid 0x%x the mutex is 0x%x", pid, lock);
  int x = Lv2Syscall3(933, pid, lock, (uint64_t)&info);

  TRACE(TRACE_DEBUG, "MUTEX", "Mutex info for 0x%x PID:%d err:0x%x", lock, pid, x);
  if(x)
    return;

  TRACE(TRACE_DEBUG, "MUTEX", "  Owner:0x%x lock:%d condref:%d, wait_threads:%d",
	info.owner, info.lock_counter, info.cond_ref_counter,
	info.wait_threads_num);
}
