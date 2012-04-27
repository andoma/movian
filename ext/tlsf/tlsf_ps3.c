#include <string.h>
#include <assert.h>
#include <psl1ght/lv2.h>
#include <malloc.h>

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



void *malloc(size_t bytes)
{
  void *r;
  if(bytes == 0)
    return NULL;

  hts_mutex_lock(&mutex);
  r = tlsf_malloc(gpool, bytes);
  hts_mutex_unlock(&mutex);
  if(r == NULL)
    panic("OOM: malloc(%d)", (int)bytes);
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
  if(r == NULL && bytes > 0)
    panic("OOM: realloc(%p, %d)", ptr, (int)bytes);
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
  if(r == NULL)
    panic("OOM: memalign(%d, %d)", (int)align, (int)bytes);
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

typedef struct memstats {
  int used;
  int free;
  int segs;
} memstats_t;


static void
mywalker(void *ptr, size_t size, int used, void *user)
{
  memstats_t *ms = user;
  if(used)
    ms->used += size;
  else {
    ms->free += size;
    ms->segs++;
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

  mi.ordblks = ms.segs;
  mi.uordblks = ms.used;
  mi.fordblks = ms.free;
  return mi;
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
  htsbuf_qprintf(&out, "Used: %d, Free: %d, Segments: %d\n",
		 ms.used, ms.free, ms.segs);

  return http_send_reply(hc, 0, "text/ascii", NULL, NULL, 0, &out);
}

void verify_heap(void);


void 
verify_heap(void)
{
  hts_mutex_lock(&mutex);
  if(tlsf_check_heap(gpool))
    trace(TRACE_NO_PROP, TRACE_ERROR, "HEAPCHECK", "Heap check verify failed");
  else
    trace(TRACE_NO_PROP, TRACE_DEBUG, "HEAPCHECK", "Heap OK");
  hts_mutex_unlock(&mutex);
}
