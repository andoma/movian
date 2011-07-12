/*
 *  Blob cache
 *  Copyright (C) 2010 Andreas Öman
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
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>

#include <libavutil/sha.h>

#include "showtime.h"
#include "blobcache.h"
#include "arch/arch.h"
#include "misc/fs.h"
#include "misc/string.h"
#include "misc/callout.h"

#ifdef LOCK_SH
#define BC_USE_FILE_LOCKS
#endif



#ifndef BC_USE_FILE_LOCKS
#define BC_NUM_FILE_LOCKS 16
#define BC_FILE_LOCKS_MASK (BC_NUM_FILE_LOCKS - 1)
static hts_mutex_t blobcache_lock[BC_NUM_FILE_LOCKS];
#endif


static hts_mutex_t blobcache_mutex;
static callout_t blobcache_callout;

static uint64_t blobcache_size_current;
static uint64_t blobcache_size_max;

static void blobcache_do_prune(struct callout *c, void *opaque);

/**
 *
 */
static void
digest_key(const char *key, const char *stash, uint8_t *d)
{
  struct AVSHA *shactx = alloca(av_sha_size);

  av_sha_init(shactx, 160);
  av_sha_update(shactx, (const uint8_t *)key, strlen(key));
  av_sha_update(shactx, (const uint8_t *)stash, strlen(stash));
  av_sha_final(shactx, d);
}


/**
 *
 */
static void
digest_to_path(uint8_t *d, char *path, size_t pathlen)
{
  snprintf(path, pathlen, "%s/blobcache/"
	   "%02x/%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x",
	   showtime_cache_path,
	   d[0],  d[1],  d[2],  d[3],
	   d[4],  d[5],  d[6],  d[7],
	   d[8],  d[9],  d[10], d[11],
	   d[12], d[13], d[14], d[15],
	   d[16], d[17], d[18], d[19]);
}


/**
 *
 */
static void *
blobcache_load(const char *path, int fd, size_t *sizep, int pad, int lockhash)
{
  struct stat st;
  uint8_t buf[4];
  void *r;
  time_t exp;
  size_t l;

#ifndef BC_USE_FILE_LOCKS
  hts_mutex_lock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#else
  if(flock(fd, LOCK_SH))
    return NULL;
#endif

  if(fstat(fd, &st))
    goto bad;

  if(read(fd, buf, 4) != 4)
    goto bad;

  exp = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

  if(exp < time(NULL)) {
    unlink(path);
    hts_mutex_lock(&blobcache_mutex);
    blobcache_size_current -= st.st_size;
    if(blobcache_size_current < 0)
      blobcache_size_current = 0; // Just to be sure
    hts_mutex_unlock(&blobcache_mutex);
    goto bad;
  }

  l = st.st_size - 4;

  r = malloc(l + pad);

  if(read(fd, r, l) != l) {
    free(r);
    goto bad;
  }

  memset(r + l, 0, pad);

  *sizep = l;
#ifndef BC_USE_FILE_LOCKS
  hts_mutex_unlock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#endif
  return r;
 bad:
#ifndef BC_USE_FILE_LOCKS
  hts_mutex_unlock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#endif
  return NULL;
}


/**
 *
 */
void *
blobcache_get(const char *key, const char *stash, size_t *sizep, int pad)
{
  char path[PATH_MAX];
  int fd;
  void *r;
  uint8_t d[20];

  digest_key(key, stash, d);

  digest_to_path(d, path, sizeof(path));
  if((fd = open(path, O_RDONLY, 0)) == -1)
    return NULL;

  r = blobcache_load(path, fd, sizep, pad, d[0]);
  close(fd);
  return r;
}


/**
 *
 */
static int
blobcache_save(int fd, const void *data, size_t size, time_t expire,
	       int lockhash)
{
  uint8_t buf[4];

#ifndef BC_USE_FILE_LOCKS
  hts_mutex_lock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#else
  if(flock(fd, LOCK_EX))
    return -1;
#endif
  
  buf[0] = expire >> 24;
  buf[1] = expire >> 16;
  buf[2] = expire >> 8;
  buf[3] = expire;

  if(write(fd, buf, 4) != 4)
    goto bad;

  if(write(fd, data, size) != size)
    goto bad;

  if(ftruncate(fd, size + 4))
    goto bad;

  hts_mutex_lock(&blobcache_mutex);
  blobcache_size_current += size + 4;

  if(blobcache_size_current > blobcache_size_max) {

    if(!callout_isarmed(&blobcache_callout))
      callout_arm(&blobcache_callout, blobcache_do_prune, NULL, 5);
  }

  hts_mutex_unlock(&blobcache_mutex);

#ifndef BC_USE_FILE_LOCKS
  hts_mutex_unlock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#endif
  return 0;
 bad:
#ifndef BC_USE_FILE_LOCKS
  hts_mutex_unlock(&blobcache_lock[lockhash & BC_FILE_LOCKS_MASK]);
#endif
  return -1;
}


/**
 *
 */
void
blobcache_put(const char *key, const char *stash,
	      const void *data, size_t size, int maxage)
{
  char path[PATH_MAX];
  int fd;
  uint8_t d[20];

  digest_key(key, stash, d);
  snprintf(path, sizeof(path), "%s/blobcache/%02x", showtime_cache_path, d[0]);

  if(makedirs(path))
    return;

  digest_to_path(d, path, sizeof(path));

  if((fd = open(path, O_CREAT | O_WRONLY, 0666)) == -1)
    return;

  // max 30 days of cache
  if(maxage > 86400 * 30) 
    maxage = 86400 * 30;

  if(blobcache_save(fd, data, size, time(NULL) + maxage, d[0]))
    unlink(path);

  close(fd);
}

LIST_HEAD(cachfile_list, cachefile);

/**
 *
 */
typedef struct cachefile {
  LIST_ENTRY(cachefile) link;
  uint8_t d[20];
  time_t time;
  uint64_t size;
} cachefile_t;


/**
 *
 */
static void
addfile(struct cachfile_list *l,
	const char *prefix, const char *name, struct stat *st)
{
  cachefile_t *c = calloc(1, sizeof(cachefile_t));
  hex2bin(&c->d[0], 1,  prefix);
  hex2bin(&c->d[1], 19, name);

  c->time = st->st_mtime > st->st_atime ? st->st_mtime : st->st_atime;
  c->size = st->st_size;

  LIST_INSERT_HEAD(l, c, link);
}


/**
 *
 */
static int
cfcmp(const void *p1, const void *p2)
{
  struct cachefile *a = *(struct cachefile **)p1;
  struct cachefile *b = *(struct cachefile **)p2;
 
  return a->time - b->time;
}


/**
 *
 */
static uint64_t 
blobcache_compute_size(uint64_t csize)
{
  uint64_t avail, maxsize;

  avail = arch_cache_avail_bytes() + csize;
  maxsize = avail / 10;

  if(maxsize < 10 * 1000 * 1000)
    maxsize  = 10 * 1000 * 1000;

  if(maxsize > 1000 * 1000 * 1000)
    maxsize  = 1000 * 1000 * 1000;
  return maxsize;
}


/**
 *
 */
static void
blobcache_prune(void)
{
  DIR *d1, *d2;
  struct dirent *de1, *de2;
  char path[PATH_MAX];
  char path2[PATH_MAX];
  char path3[PATH_MAX];
  struct stat st;
  
  uint64_t tsize = 0, msize;
  int files = 0;
  struct cachfile_list list;

  snprintf(path, sizeof(path), "%s/blobcache", showtime_cache_path);

  if((d1 = opendir(path)) == NULL)
    return;

  LIST_INIT(&list);

  while((de1 = readdir(d1)) != NULL) {
    if(de1->d_name[0] != '.') {
      snprintf(path2, sizeof(path2), "%s/blobcache/%s",
	       showtime_cache_path, de1->d_name);

      if((d2 = opendir(path2)) != NULL) {
	while((de2 = readdir(d2)) != NULL) {
          if(de2->d_name[0] != '.') {

	    snprintf(path3, sizeof(path3), "%s/blobcache/%s/%s",
		     showtime_cache_path, de1->d_name,
		     de2->d_name);
	    
	    if(!stat(path3, &st)) {
	      addfile(&list, de1->d_name, de2->d_name, &st);
	      files++;
	      tsize += st.st_size;
	    }
	  }
	}
	closedir(d2);
      }
    }
  }
  closedir(d1);
  
  msize = blobcache_compute_size(tsize);
  
  if(files > 0) {
    struct cachefile **v, *c, *next;
    int i;

    time_t now;
    time(&now);

    for(c = LIST_FIRST(&list); c != NULL; c = next) {
      next = LIST_NEXT(c, link);

      digest_to_path(c->d, path, sizeof(path));
      int fd = open(path, O_RDONLY, 0);
      if(fd != -1) {
	uint8_t buf[4];
	
	if(read(fd, buf, 4) == 4) {
	  time_t exp = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

	  if(exp < now) {
	    tsize -= c->size;
	    files--;
	    unlink(path);
	    LIST_REMOVE(c, link);
	    free(c);
	  }
	}
	close(fd);
      }
    }

    v = malloc(sizeof(struct cachfile_list *) * files);
    i = 0;
    LIST_FOREACH(c, &list, link)
      v[i++] = c;

    assert(i == files);

    qsort(v, files, sizeof(struct cachfile_list *), cfcmp);

    for(i = 0; i < files; i++) {
      c = v[i];
    
      if(tsize > msize) {
	digest_to_path(c->d, path, sizeof(path));
	if(!unlink(path))
	  tsize -= c->size;
      }
      free(c);
    }
    free(v);
  }


  hts_mutex_lock(&blobcache_mutex);

  blobcache_size_max = msize;
  blobcache_size_current = tsize;

  TRACE(TRACE_DEBUG, "blobcache", "Using %lld MB out of %lld MB",
	blobcache_size_current / 1000000LL, 
	blobcache_size_max     / 1000000LL);

  hts_mutex_unlock(&blobcache_mutex);
}


/**
 *
 */
void
blobcache_init(void)
{
#ifndef BC_USE_FILE_LOCKS
  int i;
  for(i = 0; i < BC_NUM_FILE_LOCKS; i++)
    hts_mutex_init(&blobcache_lock[i]);
#endif
  hts_mutex_init(&blobcache_mutex);
  callout_arm(&blobcache_callout, blobcache_do_prune, NULL, 1);
}

static void
blobcache_do_prune(struct callout *c, void *opaque)
{
  blobcache_prune();
}
