#include <string.h>
#include <assert.h>
#include <psl1ght/lv2.h>
#include <malloc.h>
#include <limits.h>
#include <errno.h>

#include "arch/threads.h"
#include "showtime.h"
#include "ext/tlsf/tlsf.h"
#include "networking/http_server.h"

#define GPOOL_SIZE (96 * 1024 * 1024)

static int memstats(http_connection_t *hc, const char *remain, void *opaque,
		    http_cmd_t method);

static hts_mutex_t mutex;
static tlsf_pool gpool;

static void __attribute__((constructor)) mallocsetup(void)
{
  hts_mutex_init(&mutex);

  u32 taddr;
  int size =  GPOOL_SIZE;

  Lv2Syscall3(348, size, 0x400, (u64)&taddr);
  
  gpool = tlsf_create((void *)(intptr_t)taddr, size);

  // Malloc is initialized now so we can safely do this

  http_path_add("/showtime/memstats", NULL, memstats, 1);
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
  mi.arena =  GPOOL_SIZE;

  hts_mutex_lock(&mutex);
  tlsf_walk_heap(gpool, mywalker, &ms);
  hts_mutex_unlock(&mutex);

  mi.ordblks = ms.free_segs;
  mi.uordblks = ms.used;
  mi.fordblks = ms.free;
  return mi;
}


static void
memtrace(void)
{
  memstats_t ms = {0};
  hts_mutex_lock(&mutex);
  tlsf_walk_heap(gpool, mywalker, &ms);
  hts_mutex_unlock(&mutex);

  trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
        "Memory allocator status -- Used: %d (%d segs) Free: %d (%d segs)",
        ms.used, ms.used_segs, ms.free, ms.free_segs);

#if ENABLE_SPIDERMONKEY
  extern int js_get_mem_usage(void);
  trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
        "Memory used by Spidermonkey: %d bytes", js_get_mem_usage());
#endif

  for(int i = 0; i < 33; i++) {
    trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "%d: %8d %8d",
          i, ms.hist_used[i], ms.hist_free[i]);
  }
}

void *malloc(size_t bytes)
{
  void *r;
  if(bytes == 0)
    return NULL;

  hts_mutex_lock(&mutex);
  r = tlsf_malloc(gpool, bytes);
  hts_mutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    panic("OOM: malloc(%d)", (int)bytes);
  }
  return r;
}


void free(void *ptr)
{
  if(ptr == NULL)
    return;
  hts_mutex_lock(&mutex);
  tlsf_free(gpool, ptr);
  hts_mutex_unlock(&mutex);
}

void *realloc(void *ptr, size_t bytes)
{
  void *r;
  hts_mutex_lock(&mutex);
  r = tlsf_realloc(gpool, ptr, bytes);
  hts_mutex_unlock(&mutex);
  if(r == NULL && bytes > 0) {
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

  hts_mutex_lock(&mutex);
  r = tlsf_memalign(gpool, align, bytes);
  hts_mutex_unlock(&mutex);
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


static int
memstats(http_connection_t *hc, const char *remain, void *opaque,
	 http_cmd_t method)
{
  htsbuf_queue_t out;
  memstats_t ms = {0};

  hts_mutex_lock(&mutex);
  tlsf_walk_heap(gpool, mywalker, &ms);
  hts_mutex_unlock(&mutex);
  
  htsbuf_queue_init(&out, 0);
  htsbuf_qprintf(&out, "Used: %d (%d segs), Free: %d (%d segs)\n",
		 ms.used, ms.used_segs, ms.free, ms.free_segs);

  return http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
}

void verify_heap(void);


void 
verify_heap(void)
{
  hts_mutex_lock(&mutex);
  int r = tlsf_check_heap(gpool);
  hts_mutex_unlock(&mutex);

  if(r)
    trace(TRACE_NO_PROP, TRACE_ERROR, "HEAPCHECK", "Heap check verify failed");
  else
    trace(TRACE_NO_PROP, TRACE_DEBUG, "HEAPCHECK", "Heap OK");
}


void *
mymalloc(size_t bytes)
{
  if(bytes == 0)
    return NULL;

  hts_mutex_lock(&mutex);
  void *r = tlsf_malloc(gpool, bytes);
  hts_mutex_unlock(&mutex);

  if(r == NULL) {
    memtrace();
    trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "malloc(%d) failed", (int)bytes);
    errno = ENOMEM;
  }
  return r;
}

void *
myrealloc(void *ptr, size_t bytes)
{
  hts_mutex_lock(&mutex);
  void *r = tlsf_realloc(gpool, ptr, bytes);

  hts_mutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
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
    trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
          "calloc(%d,%d) failed", (int)nmemb, (int)bytes);
    errno = ENOMEM;
  }
  return r;
}


void *mymemalign(size_t align, size_t bytes)
{
  if(bytes == 0)
    return NULL;

  hts_mutex_lock(&mutex);
  void *r = tlsf_memalign(gpool, align, bytes);
  hts_mutex_unlock(&mutex);
  if(r == NULL) {
    memtrace();
    trace(TRACE_NO_PROP, TRACE_ERROR, "MEMORY",
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
  hts_mutex_lock(&mutex);
  tlsf_free(gpool, ptr);
  hts_mutex_unlock(&mutex);
}
