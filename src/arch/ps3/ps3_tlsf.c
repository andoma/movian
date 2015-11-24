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
#include <string.h>
#include <assert.h>
#include <psl1ght/lv2.h>
#include <malloc.h>
#include <limits.h>
#include <errno.h>

#include "arch/arch.h"
#include "arch/threads.h"
#include "main.h"
#include "ext/tlsf/tlsf.h"
#include "networking/http_server.h"
#include "arch/halloc.h"

#define USE_VIRTUAL_MEM

#define MB(x) ((x) * 1024 * 1024)

static int total_avail;

static int memstats(http_connection_t *hc, const char *remain, void *opaque,
		    http_cmd_t method);

static hts_lwmutex_t mutex;
static tlsf_pool gpool;
uint32_t heap_base;

static void __attribute__((constructor)) mallocsetup(void)
{
  hts_lwmutex_init(&mutex);

#ifdef USE_VIRTUAL_MEM

  int size = MB(256);
  int psize = MB(96);

  Lv2Syscall6(300, size, psize, 0xFFFFFFFFU, 0x200ULL, 1UL, (u64)&heap_base);
#else

  int size = MB(96);

  Lv2Syscall3(348, size, 0x400, (u64)&heap_base);

#endif

  total_avail = size;
  gpool = tlsf_create((void *)(intptr_t)heap_base, size);

  // Malloc is initialized now so we can safely do this

  http_path_add("/showtime/memstats", NULL, memstats, 1);
}

typedef struct {
  uint64_t page_fault_ppu;
  uint64_t page_fault_spu;
  uint64_t page_in;
  uint64_t page_out;
  uint32_t pmem_total;
  uint32_t pmem_used;
  uint64_t time;
} vm_statistics;

void vm_stat_log(void);

void vm_stat_log(void)
{
#ifdef USE_VIRTUAL_MEM
  vm_statistics vs;

  Lv2Syscall2(312, heap_base, (uint64_t)&vs);
  TRACE(TRACE_DEBUG, "VM",
	"pfppu=%"PRId64" pfspu=%"PRId64" pin=%"PRId64" pout=%"PRId64" "
	"pmem=%d kB/%d kB",
	vs.page_fault_ppu,
	vs.page_fault_spu,
	vs.page_in,
	vs.page_out,
	vs.pmem_used / 1024,
	vs.pmem_total / 1024);
#endif
}











typedef struct memstats {
  int used;
  int free;
  int used_segs;
  int free_segs;

  uint16_t hist_used[33];
  uint16_t hist_free[33];

} memstats_t;


static void
mywalker(void *ptr, size_t size, int used, void *user)
{
  memstats_t *ms = user;
  const int clz = __builtin_clz(size);

  if(used) {
    ms->used += size;
    ms->used_segs++;
    ms->hist_used[clz]++;
  } else {
    ms->free += size;
    ms->free_segs++;
    ms->hist_free[clz]++;
  }
}


struct mallinfo mallinfo(void)
{
  struct mallinfo mi;
  memstats_t ms = {0};
  mi.arena =  total_avail;

  hts_lwmutex_lock(&mutex);
  tlsf_walk_heap(gpool, mywalker, &ms);
  hts_lwmutex_unlock(&mutex);

  mi.ordblks = ms.free_segs;
  mi.uordblks = ms.used;
  mi.fordblks = ms.free;
  return mi;
}


static void
memtrace(void)
{
  memstats_t ms = {0};
  hts_lwmutex_lock(&mutex);
  tlsf_walk_heap(gpool, mywalker, &ms);
  hts_lwmutex_unlock(&mutex);

  tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
        "Memory allocator status -- Used: %d (%d segs) Free: %d (%d segs)",
        ms.used, ms.used_segs, ms.free, ms.free_segs);

  for(int i = 0; i < 33; i++) {
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "%2d: %8d %8d",
          i, ms.hist_used[i], ms.hist_free[i]);
  }
}

void *malloc(size_t bytes)
{
  void *r;
  if(bytes == 0)
    return NULL;

  hts_lwmutex_lock(&mutex);
  r = tlsf_malloc(gpool, bytes);
  hts_lwmutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    panic("OOM: malloc(%d)", (int)bytes);
  }
  return r;
}

#define ROUND_UP(p, round) ((p + (round) - 1) & ~((round) - 1))


void free(void *ptr)
{
  if(ptr == NULL)
    return;
  const int bs = tlsf_block_size(ptr);


  if(bs >= 65536) {
    const int p = (intptr_t)ptr;

    const int np = ROUND_UP(p, 65536);
    int s = bs - (np - p);
    if(s > 0) {
      s &= ~0xffff;
      if(s > 0) {
#if 0
	tracelog(TRACE_NO_PROP, TRACE_DEBUG, "MEMORY",
	      "free(%p+%d) == page_free(0x%x+%d)",
	      ptr, bs, np, s);
#endif
#ifdef USE_VIRTUAL_MEM
	if(Lv2Syscall2(308, np, s))  // Invalidate
	  tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
		"Invalidate failed");
	if(Lv2Syscall2(310, np, s))  // Sync
	  tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
		"Sync failed");
#endif
      }
    }
  }
  hts_lwmutex_lock(&mutex);
  tlsf_free(gpool, ptr);
  hts_lwmutex_unlock(&mutex);
}


void *realloc(void *ptr, size_t bytes)
{
  void *r;

  if(bytes == 0) {
    free(ptr);
    return NULL;
  }

  hts_lwmutex_lock(&mutex);
  r = tlsf_realloc(gpool, ptr, bytes);
  hts_lwmutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    panic("OOM: realloc(%p, %d)", ptr, (int)bytes);
  }
  return r;
}


void *memalign(size_t align, size_t bytes)
{
  void *r;
  if(bytes == 0)
    return NULL;

  hts_lwmutex_lock(&mutex);
  r = tlsf_memalign(gpool, align, bytes);
  hts_lwmutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    panic("OOM: memalign(%d, %d)", (int)align, (int)bytes);
  }
  return r;
}


void *calloc(size_t nmemb, size_t bytes)
{
  void *r = malloc(bytes * nmemb);
  memset(r, 0, bytes * nmemb);
  return r;
}





void _free_r(struct _reent *r, void *ptr);
void _free_r(struct _reent *r, void *ptr)
{
	free(ptr);
}

void *_malloc_r(struct _reent *r, size_t size);
void *_malloc_r(struct _reent *r, size_t size)
{
	return malloc(size);
}

void *_calloc_r(struct _reent *r, size_t nmemb, size_t size);
void *_calloc_r(struct _reent *r, size_t nmemb, size_t size)
{
	return calloc(nmemb, size);
}

void *_realloc_r(struct _reent *r, void *ptr, size_t size);
void *_realloc_r(struct _reent *r, void *ptr, size_t size)
{
	return realloc(ptr, size);
}



typedef struct {
  int size;
  int used;
} seginfo_t;

typedef struct {
  int count;
  seginfo_t *ptr;
} allsegs_t;


static int seginfo_cmp(const void *A, const void *B)
{
  const seginfo_t *a = A;
  const seginfo_t *b = B;
  if(a->used == b->used)
    return a->size - b->size;
  return a->used - b->used;
}


static void
list_all_segs_walk(void *ptr, size_t size, int used, void *user)
{
  allsegs_t *as = user;

  if(as->ptr != NULL) {
    as->ptr[as->count].size = size;
    as->ptr[as->count].used = used;
  }
  as->count++;
}


static int
memstats(http_connection_t *hc, const char *remain, void *opaque,
	 http_cmd_t method)
{
  htsbuf_queue_t out;
  allsegs_t as = {};

  hts_lwmutex_lock(&mutex);
  tlsf_walk_heap(gpool, list_all_segs_walk, &as);
  int size = as.count * sizeof(seginfo_t);
  as.ptr = halloc(size);
  as.count = 0;
  tlsf_walk_heap(gpool, list_all_segs_walk, &as);
  hts_lwmutex_unlock(&mutex);

  qsort(as.ptr, as.count, sizeof(seginfo_t), seginfo_cmp);

  htsbuf_queue_init(&out, 0);

  htsbuf_qprintf(&out, "%d segments ptr=%p\n\n", as.count, as.ptr);
  int lastsize = -1;
  int dup = 0;
  for(int i = 0; i < as.count; i++) {
    if(as.ptr[i].size == lastsize && i != as.count - 1) {
      dup++;
    } else {
      htsbuf_qprintf(&out, "%s %10d * %d\n",
		     as.ptr[i].used ? "Used" : "Free", as.ptr[i].size,
		     dup + 1);
      dup = 0;
    }
    lastsize = as.ptr[i].size;
  }

  hfree(as.ptr, size);
  

  return http_send_reply(hc, 0, "text/plain", NULL, NULL, 0, &out);
}

void verify_heap(void);


void 
verify_heap(void)
{
  hts_lwmutex_lock(&mutex);
  int r = tlsf_check_heap(gpool);
  hts_lwmutex_unlock(&mutex);

  if(r)
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "HEAPCHECK", "Heap check verify failed");
  else
    tracelog(TRACE_NO_PROP, TRACE_DEBUG, "HEAPCHECK", "Heap OK");
}


void *
mymalloc(size_t bytes)
{
  if(bytes == 0)
    return NULL;

  hts_lwmutex_lock(&mutex);
  void *r = tlsf_malloc(gpool, bytes);
  hts_lwmutex_unlock(&mutex);

  if(r == NULL) {
    memtrace();
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "malloc(%d) failed", (int)bytes);
    errno = ENOMEM;
  }
  return r;
}

void *
myrealloc(void *ptr, size_t bytes)
{
  hts_lwmutex_lock(&mutex);
  void *r = tlsf_realloc(gpool, ptr, bytes);

  hts_lwmutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "realloc(%d) failed", (int)bytes);
    errno = ENOMEM;
  }
  return r;
}

void *
mycalloc(size_t nmemb, size_t bytes)
{
  void *r = mymalloc(bytes * nmemb);
  memset(r, 0, bytes * nmemb);
  if(r == NULL) {
    memtrace();
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "calloc(%d,%d) failed", (int)nmemb, (int)bytes);
    errno = ENOMEM;
  }
  return r;
}


void *mymemalign(size_t align, size_t bytes)
{
  if(bytes == 0)
    return NULL;

  hts_lwmutex_lock(&mutex);
  void *r = tlsf_memalign(gpool, align, bytes);
  hts_lwmutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "memalign(%d,%d) failed", (int)align, (int)bytes);
    errno = ENOMEM;
  }
  return r;
}


void myfree(void *ptr);

void myfree(void *ptr)
{
  if(ptr == NULL)
    return;
  hts_lwmutex_lock(&mutex);
  tlsf_free(gpool, ptr);
  hts_lwmutex_unlock(&mutex);
}


size_t
arch_malloc_size(void *ptr)
{
  return tlsf_block_size(ptr);
}

