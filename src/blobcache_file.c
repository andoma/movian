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

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <errno.h>

#include "showtime.h"
#include "blobcache.h"
#include "misc/pool.h"
#include "misc/callout.h"
#include "misc/sha.h"
#include "misc/md5.h"
#include "arch/arch.h"
#include "arch/threads.h"
#include "arch/atomic.h"

#define BC2_MAGIC 0x62630200

typedef struct blobcache_item {
  struct blobcache_item *bi_link;
  uint64_t bi_key_hash;
  uint64_t bi_content_hash;
  uint32_t bi_lastaccess;
  uint32_t bi_expiry;
  uint32_t bi_modtime;
  uint32_t bi_size;
} blobcache_item_t;

typedef struct blobcache_diskitem {
  uint64_t di_key_hash;
  uint64_t di_content_hash;
  uint32_t di_lastaccess;
  uint32_t di_expiry;
  uint32_t di_modtime;
  uint32_t di_size;
} __attribute__((packed)) blobcache_diskitem_t;

#define ITEM_HASH_SIZE 256
#define ITEM_HASH_MASK (ITEM_HASH_SIZE - 1)

static blobcache_item_t *hashvector[ITEM_HASH_SIZE];

static pool_t *item_pool;
static hts_mutex_t cache_lock;





static callout_t blobcache_callout;

static void blobcache_do_prune(struct callout *c, void *opaque);

#define BLOB_CACHE_MINSIZE  (10 * 1000 * 1000)
#define BLOB_CACHE_MAXSIZE (500 * 1000 * 1000)

static uint64_t current_cache_size;

/**
 *
 */
static uint64_t 
blobcache_compute_maxsize(void)
{
  uint64_t avail = arch_cache_avail_bytes() + current_cache_size;
  avail = MAX(BLOB_CACHE_MINSIZE, MIN(avail / 10, BLOB_CACHE_MAXSIZE));
  return avail;
}


/**
 *
 */
static uint64_t
digest_key(const char *key, const char *stash)
{
  union {
    uint8_t d[20];
    uint64_t u64;
  } u;
  sha1_decl(shactx);
  sha1_init(shactx);
  sha1_update(shactx, (const uint8_t *)key, strlen(key));
  sha1_update(shactx, (const uint8_t *)stash, strlen(stash));
  sha1_final(shactx, u.d);
  return u.u64;
}


/**
 *
 */
static uint64_t
digest_content(const void *data, size_t len)
{
  union {
    uint8_t d[16];
    uint64_t u64;
  } u;
  md5_decl(ctx);
  md5_init(ctx);
  md5_update(ctx, data, len);
  md5_final(ctx, u.d);
  return u.u64;
}


/**
 *
 */
static void
make_filename(char *buf, size_t len, uint64_t hash, int for_write)
{
  uint8_t dir = hash;
  if(for_write) {
    snprintf(buf, len, "%s/bc2/%02x", showtime_cache_path, dir);
    mkdir(buf, 0777);
  }
  snprintf(buf, len, "%s/bc2/%02x/%016"PRIx64, showtime_cache_path, dir, hash);
}


/**
 *
 */
static void
save_index(void)
{
  char filename[PATH_MAX];
  uint8_t *out;
  int i, j;
  blobcache_item_t *p;
  blobcache_diskitem_t *di;
  size_t siz;
  snprintf(filename, sizeof(filename), "%s/bc2/index.dat", showtime_cache_path);
  
  int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if(fd == -1)
    return;

  hts_mutex_lock(&cache_lock);
  int tot = pool_num(item_pool);

  siz = 4 + tot * sizeof(blobcache_diskitem_t) + 20;
  out = malloc(siz);
  *(uint32_t *)out = BC2_MAGIC;
  j = 0;
  for(i = 0; i < ITEM_HASH_SIZE; i++) {
    for(p = hashvector[i]; p != NULL; p = p->bi_link) {
      di = &((blobcache_diskitem_t *)(out + 4))[j++];
      di->di_key_hash     = p->bi_key_hash;
      di->di_content_hash = p->bi_content_hash;
      di->di_lastaccess   = p->bi_lastaccess;
      di->di_expiry       = p->bi_expiry;
      di->di_modtime      = p->bi_modtime;
      di->di_size         = p->bi_size;
    }
  }
  hts_mutex_unlock(&cache_lock);

  sha1_decl(shactx);
  sha1_init(shactx);
  sha1_update(shactx, out, 4 + tot * sizeof(blobcache_diskitem_t));
  sha1_final(shactx, out + 4 + tot * sizeof(blobcache_diskitem_t));
  
  if(write(fd, out, siz) != siz)
    TRACE(TRACE_INFO, "blobcache", "Unable to store index file %s -- %s",
	  filename, strerror(errno));
  free(out);
  close(fd);
}



/**
 *
 */
static void
load_index(void)
{
  char filename[PATH_MAX];
  uint8_t *in;
  int i;
  blobcache_item_t *p;
  blobcache_diskitem_t *di;
  struct stat st;
  uint8_t digest[20];

  snprintf(filename, sizeof(filename), "%s/bc2/index.dat", showtime_cache_path);
  
  int fd = open(filename, O_RDONLY, 0);
  if(fd == -1)
    return;

  if(fstat(fd, &st) || st.st_size < 24 ||
     ((st.st_size - 24) % sizeof(blobcache_diskitem_t)) != 0) {
    close(fd);
    return;
  }

  int items = (st.st_size - 24) / sizeof(blobcache_diskitem_t);

  in = malloc(st.st_size);
  size_t r = read(fd, in, st.st_size);
  close(fd);
  if(r != st.st_size) {
    free(in);
    return;
  }


  if(*(uint32_t *)in != BC2_MAGIC) {
    free(in);
    return;
  }
       

  sha1_decl(shactx);
  sha1_init(shactx);
  sha1_update(shactx, in, st.st_size - 20);
  sha1_final(shactx, digest);

  if(memcmp(digest, in + st.st_size - 20, 20)) {
    free(in);
    return;
  }

  for(i = 0; i < items; i++) {
    di = &((blobcache_diskitem_t *)(in + 4))[i];
    p = pool_get(item_pool);

    p->bi_key_hash     = di->di_key_hash;
    p->bi_content_hash = di->di_content_hash;
    p->bi_lastaccess   = di->di_lastaccess;
    p->bi_expiry       = di->di_expiry;
    p->bi_modtime      = di->di_modtime;
    p->bi_size         = di->di_size;
    p->bi_link = hashvector[p->bi_key_hash & ITEM_HASH_MASK];
    hashvector[p->bi_key_hash & ITEM_HASH_MASK] = p;
    current_cache_size += p->bi_size;
  }
  free(in);
}



/**
 *
 */
int
blobcache_put(const char *key, const char *stash,
	      const void *data, size_t size, int maxage,
	      const char *etag, time_t mtime)
{
  uint64_t dk = digest_key(key, stash);
  uint64_t dc = digest_content(data, size);
  uint32_t now = time(NULL);
  char filename[PATH_MAX];
  blobcache_item_t *p;

  hts_mutex_lock(&cache_lock);
  for(p = hashvector[dk & ITEM_HASH_MASK]; p != NULL; p = p->bi_link)
    if(p->bi_key_hash == dk)
      break;

  if(p != NULL && p->bi_content_hash == dc && p->bi_size == size) {
    p->bi_modtime = mtime;
    p->bi_expiry = now + maxage;
    p->bi_lastaccess = now;
    hts_mutex_unlock(&cache_lock);
    return 1;
  }

  make_filename(filename, sizeof(filename), dk, 1);
  int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
  if(fd == -1) {
    hts_mutex_unlock(&cache_lock);
    return 0;
  }

  if(write(fd, data, size) != size) {
    unlink(filename);
    hts_mutex_unlock(&cache_lock);
    return 0;
  }
  close(fd);

  if(p == NULL) {
    p = pool_get(item_pool);
    p->bi_key_hash = dk;
    p->bi_size = 0;
  }

  int64_t expiry = (int64_t)maxage + now;

  p->bi_modtime = mtime;
  p->bi_expiry = MAX(INT32_MAX, expiry);
  p->bi_lastaccess = now;
  p->bi_content_hash = dc;
  current_cache_size -= p->bi_size;
  p->bi_size = size;
  current_cache_size += p->bi_size;

  p->bi_link = hashvector[dk & ITEM_HASH_MASK];
  hashvector[dk & ITEM_HASH_MASK] = p;

  if(blobcache_compute_maxsize() < current_cache_size &&
     !callout_isarmed(&blobcache_callout))
    callout_arm(&blobcache_callout, blobcache_do_prune, NULL, 5);

  hts_mutex_unlock(&cache_lock);
  return 0;
}


/**
 *
 */
void *
blobcache_get(const char *key, const char *stash, size_t *sizep, int pad,
	      int *ignore_expiry, char **etagp, time_t *mtimep)
{
  uint64_t dk = digest_key(key, stash);
  blobcache_item_t *p, **q;
  char filename[PATH_MAX];
  struct stat st;
  uint32_t now;

  hts_mutex_lock(&cache_lock);
  for(q = &hashvector[dk & ITEM_HASH_MASK]; (p = *q); q = &p->bi_link)
    if(p->bi_key_hash == dk)
      break;
  
  if(p == NULL) {
    hts_mutex_unlock(&cache_lock);
    return NULL;
  }
  
  now = time(NULL);

  int expired = now > p->bi_expiry;

  if(expired && ignore_expiry == NULL) {
    hts_mutex_unlock(&cache_lock);
    goto bad;
  }

  make_filename(filename, sizeof(filename), p->bi_key_hash, 0);
  int fd = open(filename, O_RDONLY, 0);
  if(fd == -1) {
  bad:
    *q = p->bi_link;
    pool_put(item_pool, p);
    hts_mutex_unlock(&cache_lock);
    return NULL;
  }
  
  if(fstat(fd, &st))
    goto bad;

  if(st.st_size != p->bi_size) {
    unlink(filename);
    goto bad;
  }

  if(mtimep)
    *mtimep = p->bi_modtime;

  p->bi_lastaccess = now;

  hts_mutex_unlock(&cache_lock);

  if(ignore_expiry != NULL)
    *ignore_expiry = expired;

  uint8_t *r = malloc(st.st_size + pad);
  if(read(fd, r, st.st_size) != st.st_size) {
    free(r);
    close(fd);
    return NULL;
  }
  memset(r + st.st_size, 0, pad);
  close(fd);

  *sizep = st.st_size;
  return r;
}





/**
 *
 */
int
blobcache_get_meta(const char *key, const char *stash, 
		   char **etagp, time_t *mtimep)
{
  uint64_t dk = digest_key(key, stash);
  blobcache_item_t *p;
  int r;
  hts_mutex_lock(&cache_lock);
  for(p = hashvector[dk & ITEM_HASH_MASK]; p != NULL; p = p->bi_link)
    if(p->bi_key_hash == dk)
      break;
  
  if(p != NULL) {
    r = 0;

    if(mtimep != NULL)
      *mtimep = p->bi_modtime;

  } else {
    r = -1;
  }

  hts_mutex_unlock(&cache_lock);
  return r;
}


/**
 * Assume we're locked
 */
static blobcache_item_t *
lookup_item(uint64_t dk)
{
  blobcache_item_t *p;
  for(p = hashvector[dk & ITEM_HASH_MASK]; p != NULL; p = p->bi_link)
    if(p->bi_key_hash == dk)
      return p;
  return NULL;
}

/**
 *
 */
static void
prune_stale(void)
{
  DIR *d1, *d2;
  struct dirent *de1, *de2;
  char path[PATH_MAX];
  char path2[PATH_MAX];
  char path3[PATH_MAX];
  uint64_t k;

  snprintf(path, sizeof(path), "%s/bc2", showtime_cache_path);

  if((d1 = opendir(path)) == NULL)
    return;

  while((de1 = readdir(d1)) != NULL) {
    if(de1->d_name[0] != '.') {
      snprintf(path2, sizeof(path2), "%s/bc2/%s",
	       showtime_cache_path, de1->d_name);

      if((d2 = opendir(path2)) != NULL) {
	while((de2 = readdir(d2)) != NULL) {
          if(de2->d_name[0] != '.') {

	    snprintf(path3, sizeof(path3), "%s/bc2/%s/%s",
		     showtime_cache_path, de1->d_name,
		     de2->d_name);

	    if(sscanf(de2->d_name, "%016"PRIx64, &k) != 1 ||
	       lookup_item(k) == NULL) {
	      TRACE(TRACE_DEBUG, "Blobcache", "Removed stale file %s", path3);
	      unlink(path3);
	    }
	  }
	}
	closedir(d2);
      }
      rmdir(path2);
    }
  }
  closedir(d1);
}  




/**
 *
 */
static void
prune_item(blobcache_item_t *p)
{
  char filename[PATH_MAX];
  make_filename(filename, sizeof(filename), p->bi_key_hash, 0);
  unlink(filename);
  pool_put(item_pool, p);
}


/**
 *
 */
static int
accesstimecmp(const void *A, const void *B)
{
  const blobcache_item_t *a = *(const blobcache_item_t **)A;
  const blobcache_item_t *b = *(const blobcache_item_t **)B;
  return a->bi_lastaccess - b->bi_lastaccess;
}


/**
 *
 */
static void
prune_to_size(void)
{
  int i, tot, j = 0;
  uint64_t maxsize = blobcache_compute_maxsize();
  blobcache_item_t *p, **sv;

  hts_mutex_lock(&cache_lock);
  tot = pool_num(item_pool);

  sv = malloc(sizeof(blobcache_item_t *) * tot);
  current_cache_size = 0;
  for(i = 0; i < ITEM_HASH_SIZE; i++) {
    for(p = hashvector[i]; p != NULL; p = p->bi_link) {
      sv[j++] = p;
      current_cache_size += p->bi_size;
    }
    hashvector[i] = NULL;
  }

  assert(j == tot);

  qsort(sv, j, sizeof(blobcache_item_t *), accesstimecmp);
  for(i = 0; i < j; i++) {
    p = sv[i];
    if(current_cache_size < maxsize)
      break;
    current_cache_size -= p->bi_size;
    prune_item(p);
  }

  for(; i < j; i++) {
    p = sv[i];
    p->bi_link = hashvector[p->bi_key_hash & ITEM_HASH_MASK];
    hashvector[p->bi_key_hash & ITEM_HASH_MASK] = p;
  }

  free(sv);
  hts_mutex_unlock(&cache_lock);
  save_index();
}



/**
 *
 */
static void
blobcache_prune_old(void)
{
  DIR *d1, *d2;
  struct dirent *de1, *de2;
  char path[PATH_MAX];
  char path2[PATH_MAX];
  char path3[PATH_MAX];

  snprintf(path, sizeof(path), "%s/blobcache", showtime_cache_path);

  if((d1 = opendir(path)) != NULL) {

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
	      unlink(path3);
	    }
	  }
	  closedir(d2);
	}
	rmdir(path2);
      }
    }
    closedir(d1);
    rmdir(path);
  }

  snprintf(path, sizeof(path), "%s/cachedb/cache.db", showtime_cache_path);
  unlink(path);
  snprintf(path, sizeof(path), "%s/cachedb/cache.db-shm", showtime_cache_path);
  unlink(path);
  snprintf(path, sizeof(path), "%s/cachedb/cache.db-wal", showtime_cache_path);
  unlink(path);
}  


/**
 *
 */
void
blobcache_init(void)
{
  char buf[256];

  blobcache_prune_old();
  snprintf(buf, sizeof(buf), "%s/bc2", showtime_cache_path);
  if(mkdir(buf, 0777) && errno != EEXIST)
    TRACE(TRACE_ERROR, "blobcache", "Unable to create cache dir %s -- %s",
	  buf, strerror(errno));

  hts_mutex_init(&cache_lock);
  item_pool = pool_create("blobcacheitems", sizeof(blobcache_item_t), 0);

  load_index();
  prune_stale();
  prune_to_size();
  TRACE(TRACE_INFO, "blobcache",
	"Initialized: %d items consuming %"PRId64" bytes on disk in %s",
	pool_num(item_pool), current_cache_size, buf);
}


void
blobcache_fini(void)
{
  save_index();
}

/**
 *
 */
static void
blobcache_do_prune(struct callout *c, void *opaque)
{
  prune_to_size();
}
