/*
 *  File access cache
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

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <arch/threads.h>
#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "misc/redblack.h"


TAILQ_HEAD(cached_segment_queue, cached_segment);
RB_HEAD(cached_segment_tree, cached_segment);
TAILQ_HEAD(cache_request_queue, cache_request);
TAILQ_HEAD(cached_file_queue, cached_file);

LIST_HEAD(cached_file_handle_list, cached_file_handle);

static char *cachefile;

static hts_mutex_t cache_mutex;

static struct cached_file_queue cached_files;

#define PAGE_SHIFT 19
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define PAGE_MASK (PAGE_SIZE - 1)

/**
 *
 */
typedef struct cached_page {
  int cp_file_offset;
} cached_page_t;


/**
 *
 */
typedef struct cached_file {
  fa_handle_t h;

  int cf_ref_count;

  uint64_t cf_pos;
  hts_cond_t cf_cond_req;
  hts_cond_t cf_cond_resp;
  int cf_pending;

  fa_handle_t *cf_src;
  uint64_t cf_src_pos;

  uint64_t cf_size;
  int cf_fd[2];
  struct cached_segment_tree cf_segments;

  struct cached_segment_queue cf_active_segments;
  struct cached_segment_queue cf_idle_segments;

  hts_mutex_t cf_mutex;

  int cf_thread_running;

  struct cached_file_handle_list cf_fhs;

  cached_page_t *cf_pages;

  int cf_page_mask;
  int cf_num_pages;

  prop_t *cf_stats_cachesize;
  prop_t *cf_stats_cachemax;

} cached_file_t;



#if 0
/**
 *
 */
static void
clear_segments(struct cached_segment_queue *q)
{
  cached_segment_t *cs;
  while((cs = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, cs, cs_link);
    free(cs);
  }
}
#endif


/**
 *
 */
static void
cf_setup_segment_vector(cached_file_t *cf)
{
  int cache_size = 256 * 1024 * 1024;
  int num_pages = cache_size / PAGE_SIZE;
  cf->cf_pages = malloc(sizeof(cached_page_t) * num_pages);
  memset(cf->cf_pages, 0xff, sizeof(cached_page_t) * num_pages);
  cf->cf_page_mask = num_pages - 1;
  cf->cf_num_pages = num_pages;
}

/**
 *
 */
static int
get_anon_file(int fd[2])
{
  int rval = 0;
  hts_mutex_lock(&cache_mutex);

  fd[0] = open(cachefile, O_CREAT | O_TRUNC | O_WRONLY, 0666);
  fd[1] = open(cachefile, O_RDONLY);
  
  if(fd[0] == -1 || fd[1] == -1 || unlink(cachefile)) {
    
    if(fd[0] != -1)
      close(fd[0]);
    if(fd[1] != -1)
      close(fd[1]);
    rval = 1;
  }
  hts_mutex_unlock(&cache_mutex);
  return rval;
}


/**
 *
 */
static void
cf_release(cached_file_t *cf)
{
  if(atomic_add(&cf->cf_ref_count, -1) > 1)
    return;
  
  close(cf->cf_fd[0]);
  close(cf->cf_fd[1]);
  fa_close(cf->cf_src);
  hts_mutex_destroy(&cf->cf_mutex);
  hts_cond_destroy(&cf->cf_cond_req);
  hts_cond_destroy(&cf->cf_cond_resp);
  prop_ref_dec(cf->cf_stats_cachesize);
  free(cf);
}


/**
 *
 */
static void
fac_close(fa_handle_t *handle)
{
  cached_file_t *cf = (cached_file_t *)handle;

  hts_mutex_lock(&cf->cf_mutex);
  cf->cf_thread_running = 0;
  hts_cond_signal(&cf->cf_cond_req);
  hts_mutex_unlock(&cf->cf_mutex);
  cf_release(cf);
}


/**
 *
 */
static int64_t
fac_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  cached_file_t *cf = (cached_file_t *)handle;
  uint64_t np;

  hts_mutex_lock(&cf->cf_mutex);

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = cf->cf_pos + pos;
    break;

  case SEEK_END:
    np = cf->cf_size + pos;
    break;

  default:
    hts_mutex_unlock(&cf->cf_mutex);
    return -1;
  }

  if(np < 0) {
    hts_mutex_unlock(&cf->cf_mutex);
    return -1;
  }

  if(cf->cf_pos != np) {
    cf->cf_pos = np;
    hts_cond_signal(&cf->cf_cond_req);
  }
  hts_mutex_unlock(&cf->cf_mutex);
  return np;
}


/**
 *
 */
static int64_t
fac_fsize(fa_handle_t *handle)
{
  cached_file_t *cf = (cached_file_t *)handle;
  return cf->cf_size;
}


/**
 *
 */
static int
cache_page(cached_file_t *cf, int vpage, void *buf)
{
  int dpage = vpage & cf->cf_page_mask;
  cached_page_t *cp = cf->cf_pages + dpage;
  
  if(cp->cp_file_offset == vpage)
    return 0;

  int64_t srcoffset = (int64_t) vpage << PAGE_SHIFT;
  
  if(srcoffset >= cf->cf_size)
    return 0;

  cp->cp_file_offset = -1;
  hts_mutex_unlock(&cf->cf_mutex);

  int r;

  if(fa_seek(cf->cf_src, srcoffset, SEEK_SET) == srcoffset) {
    r = fa_read(cf->cf_src, buf, PAGE_SIZE);
  } else {
    r = -1;
  }

  if(r <= 0) {
    hts_mutex_lock(&cf->cf_mutex);
    return -1;
  }

  int64_t voff = dpage << PAGE_SHIFT;

  r = lseek(cf->cf_fd[0], voff, SEEK_SET) != voff ||
    write(cf->cf_fd[0], buf, r) <= 0;
  
  hts_mutex_lock(&cf->cf_mutex);
  if(r)
    return -1;

  cp->cp_file_offset = vpage;
  return 1;
}


/**
 *
 */
static void
cache_pages(cached_file_t *cf, void *buf)
{
  int i;
  int basepage = cf->cf_pos >> PAGE_SHIFT;
  int ra = 0;

  for(i = 0; i < cf->cf_num_pages - 10; i++) {
    if(cf->cf_pending == 1 || cf->cf_thread_running == 0)
      break;
    int r = cache_page(cf, basepage + i, buf);
    if(r == -1)
      break;
    if(r == 1)
      prop_set_int(cf->cf_stats_cachesize, ra * PAGE_SIZE);
    ra++;
  }
}


/**
 *
 */
static void *
fac_thread(void *aux)
{
  cached_file_t *cf = aux;
  void *buf = malloc(PAGE_SIZE);

  hts_mutex_lock(&cf->cf_mutex);

  while(cf->cf_thread_running == 1) {

    if(cf->cf_pending == 1) {
      TRACE(TRACE_DEBUG, "Read-ahead", "Cache miss at position %"PRId64,
	    cf->cf_pos);

      if(cache_page(cf, cf->cf_pos >> PAGE_SHIFT, buf) == -1) {
	cf->cf_pending = 2;
      } else {
	cf->cf_pending = 0;
      }
      hts_cond_signal(&cf->cf_cond_resp);
      continue;
    }

    cache_pages(cf, buf);

    if(cf->cf_pending)
      continue;

    if(cf->cf_thread_running == 0)
      break;

    hts_cond_wait(&cf->cf_cond_req, &cf->cf_mutex);
  }
  hts_mutex_unlock(&cf->cf_mutex);
  free(buf);
  cf_release(cf);
  return NULL; 
}


/**
 *
 */
static int
fac_load(cached_file_t *cf, void *buf, int64_t offset, int size)
{
  int poffset = offset & PAGE_MASK;
  int vpage = offset >> PAGE_SHIFT;
  int dpage = vpage & cf->cf_page_mask;
  
  cached_page_t *cp = cf->cf_pages + dpage;

  if(cp->cp_file_offset != vpage)
    return 0;

  int count = MIN(size, PAGE_SIZE - poffset);
  int64_t voff = (dpage << PAGE_SHIFT) + poffset;

  hts_mutex_unlock(&cf->cf_mutex);

  int r;

  if(lseek(cf->cf_fd[1], voff, SEEK_SET) != voff) {
    r = 1;
  } else {
    r = read(cf->cf_fd[1], buf, count) != count;
  }

  hts_mutex_lock(&cf->cf_mutex);
  
  return r ? -1 : count;
}


/**
 *
 */
static int
fac_read(fa_handle_t *handle, void *buf, size_t size)
{
  cached_file_t *cf = (cached_file_t *)handle;
  uint64_t off = cf->cf_pos;

  if(off > cf->cf_size) 
    return 0;

  if(off + size > cf->cf_size)
    size = cf->cf_size - off;

  int total = size;

  hts_mutex_lock(&cf->cf_mutex);

  while(size > 0) {
    int r = fac_load(cf, buf, off, size);

    if(r == -1) {
      hts_mutex_unlock(&cf->cf_mutex);
      return -1;
    }

    if(r > 0) {
      size -= r;
      buf += r;
      off += r;
      cf->cf_pos += r;
      continue;
    }

    cf->cf_pending = 1;
    hts_cond_signal(&cf->cf_cond_req);
    while(cf->cf_pending == 1)
      hts_cond_wait(&cf->cf_cond_resp, &cf->cf_mutex);
  
    if(cf->cf_pending == 2) {
      hts_mutex_unlock(&cf->cf_mutex);
      return -1;
    }
  }

  hts_cond_signal(&cf->cf_cond_req);
  hts_mutex_unlock(&cf->cf_mutex);
  return total;
}


/**
 *
 */
static fa_protocol_t fa_protocol_cache = {
  .fap_name  = "cache",
  .fap_close = fac_close,
  .fap_read  = fac_read,
  .fap_seek  = fac_seek,
  .fap_fsize = fac_fsize,
};


/**
 *
 */
fa_handle_t *
fa_cache_open(const char *url, char *errbuf, size_t errsize, int flags,
	      struct prop *stats)
{
  fa_handle_t *fh = fa_open_ex(url, errbuf, errsize, flags | FA_HUGE_BUFFER,
			       stats);
  if(fh == NULL)
    return NULL;

  if(!(fh->fh_proto->fap_flags & FAP_ALLOW_CACHE))
    return fh;

  TRACE(TRACE_INFO, "RA", "Enabling read-ahead cache for %s", url);

  cached_file_t *cf = calloc(1, sizeof(cached_file_t));
  if(get_anon_file(cf->cf_fd)) {
    TRACE(TRACE_ERROR, "RA", "Unable to create page file for %s", url);
    free(cf);
    return fh;
  }

  hts_mutex_init(&cf->cf_mutex);
  hts_cond_init(&cf->cf_cond_req, &cf->cf_mutex);
  hts_cond_init(&cf->cf_cond_resp, &cf->cf_mutex);

  cf_setup_segment_vector(cf);

  // File we cache 

  if(stats != NULL) {
    cf->cf_stats_cachesize =
      prop_ref_inc(prop_create(stats, "cacheSizeCurrent"));
    prop_set_int(prop_create(stats, "cacheSizeValid"), 1);
    prop_set_int(prop_create(stats, "cacheSizeMax"),
		 cf->cf_num_pages * PAGE_SIZE);
  }

  cf->cf_src = fh;
  cf->cf_size = fa_fsize(fh);
  // Boot thread

  cf->cf_ref_count = 2;
  cf->cf_thread_running = 1;
  hts_thread_create_detached("facache", fac_thread, cf, THREAD_PRIO_NORMAL);

  cf->h.fh_proto = &fa_protocol_cache;
  return &cf->h;
}


/**
 *
 */
void
fa_cache_init(void)
{
  char path[200];

  snprintf(path, sizeof(path), "%s/facache", showtime_cache_path);
  mkdir(path, 0777);
  snprintf(path, sizeof(path), "%s/facache/tmp", showtime_cache_path);

  cachefile = strdup(path);
  
  hts_mutex_init(&cache_mutex);
  TAILQ_INIT(&cached_files);
}
