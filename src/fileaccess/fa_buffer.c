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
#include <arch/halloc.h>

#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"

#define BF_CHK 0

#define BF_MIN_READ   (64 * 1024)
#define BF_CACHE_SIZE (256 * 1024)

#define BF_ZONES 8
#define BF_MASK (BF_ZONES - 1)

typedef struct buffered_zone {
  int64_t bz_fpos;
  int bz_mpos;
  int bz_size;
} buffered_zone_t;

/**
 *
 */
typedef struct buffered_file {
  fa_handle_t h;

  fa_handle_t *bf_src;
#if BF_CHK
  fa_handle_t *bf_chk;
#endif
  void *bf_mem;
  size_t bf_mem_size;

  int bf_mem_ptr;

  int64_t bf_fpos;

  int bf_replace_ptr;

  int64_t bf_size;

  buffered_zone_t bf_zones[BF_ZONES];

} buffered_file_t;




#ifdef DEBUG
/**
 *
 */
static int
intersect(int a, int as, int b, int bs)
{
  return b < a + as && a < b + bs;
}
#endif


/**
 *
 */
static void
erase_zone(buffered_file_t *bf, int mpos, int size)
{
  int i;
  size_t s0;
  for(i = 0; i < BF_ZONES; i++) {
    buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0)
      continue;

    if(mpos < bz->bz_mpos + bz->bz_size && bz->bz_mpos < mpos + size) {
      
      if(mpos == bz->bz_mpos) {
	s0 = MIN(size, bz->bz_size);
	bz->bz_fpos += s0;
	bz->bz_mpos += s0;
	bz->bz_size -= s0;
	mpos += s0;
	size -= s0;
      } else {
	bz->bz_size = 0;
      }
    }
  }
}




#ifdef DEBUG
static void
dump_zones(const char *prefix, buffered_file_t *bf)
{
  int i;
  printf("---- %s -----------------\n", prefix);
  for(i = 0; i < BF_ZONES; i++) {
    buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0)
      continue;
    printf("#%d  (%d +%d) => %"PRId64"\n",
	   i, bz->bz_mpos, bz->bz_size, bz->bz_fpos);
  }
}
#endif


/**
 *
 */
static void
map_zone(buffered_file_t *bf, int mpos, int size, int64_t fpos)
{
  int i;
  int j = BF_ZONES;

#ifdef DEBUG
  for(i = 0; i < BF_ZONES; i++) {
    const buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0)
      continue;
    
    if(intersect(bz->bz_mpos, bz->bz_size, mpos, size)) {
      printf("FAIL. Mapping of %d+%d overlaps with zone %d\n",
	     mpos, size, i);
      dump_zones("FAIL", bf);
      abort();
    }
  }
#endif

  for(i = 0; i < BF_ZONES; i++) {
    buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0) {
      j = MIN(i, j);
      continue;
    }

    if(bz->bz_fpos + bz->bz_size == fpos &&
       bz->bz_mpos + bz->bz_size == mpos) {
      // extend up
      bz->bz_size += size;
      return;
    }
  }

  if(j == BF_ZONES)
    j = bf->bf_replace_ptr = (bf->bf_replace_ptr + 1) & BF_MASK;

  buffered_zone_t *bz = &bf->bf_zones[j];
  bz->bz_fpos = fpos;
  bz->bz_mpos = mpos;
  bz->bz_size = size;
}


/**
 *
 */
static int
resolve_zone(const buffered_file_t *bf, int64_t fpos, int size, int *mpos)
{
  int i;

  for(i = 0; i < BF_ZONES; i++) {
    const buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0)
      continue;
    
    if(fpos >= bz->bz_fpos && fpos < bz->bz_fpos + bz->bz_size) {
      int d = fpos - bz->bz_fpos;
      *mpos = bz->bz_mpos + d;
      return MIN(size, bz->bz_size - d);
    }
  }
  return -1;
}


/**
 *
 */
static int
need_to_fill(const buffered_file_t *bf, int64_t fpos, size_t rd)
{
  int i;
  int64_t d = INT64_MAX;
  for(i = 0; i < BF_ZONES; i++) {
    const buffered_zone_t *bz = &bf->bf_zones[i];
    if(bz->bz_size == 0)
      continue;
    
    if(bz->bz_fpos >= fpos)
      d = MIN(d, bz->bz_fpos - fpos);
  }
  return MIN(rd, d);
}




/**
 *
 */
static void
fab_close(fa_handle_t *handle)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *src = bf->bf_src;
  src->fh_proto->fap_close(src);

  if(bf->bf_mem != NULL)
    hfree(bf->bf_mem, bf->bf_mem_size);
  free(bf);
}


/**
 *
 */
static int64_t
fab_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *src = bf->bf_src;
  uint64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = bf->bf_fpos + pos;
    break;

  case SEEK_END:
    if(bf->bf_size == -1) {
      bf->bf_size = src->fh_proto->fap_fsize(src);
      if(bf->bf_size == -1)
	return -1;
    }
    np = bf->bf_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0) {
    return -1;
  }

  bf->bf_fpos = np;
  return np;
}


/**
 *
 */
static int64_t
fab_fsize(fa_handle_t *handle)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  if(bf->bf_size != -1)
    return bf->bf_size;

  fa_handle_t *src = bf->bf_src;
  bf->bf_size = src->fh_proto->fap_fsize(src);
  return bf->bf_size;
}


/**
 *
 */
static void
store_in_cache(buffered_file_t *bf, const void *buf, size_t size)
{
  if(size > bf->bf_mem_size)
    return;

  size = MIN(size, bf->bf_mem_size);
  size_t s1 = size;
  size_t s2 = 0;

  if(bf->bf_mem_ptr + s1 > bf->bf_mem_size) {
    s1 = bf->bf_mem_size - bf->bf_mem_ptr;
    s2 = size - s1;
  }

  erase_zone(bf, bf->bf_mem_ptr, s1);

  map_zone(bf, bf->bf_mem_ptr, s1, bf->bf_fpos);
  memcpy(bf->bf_mem + bf->bf_mem_ptr, buf, s1);

  bf->bf_mem_ptr += s1;
  assert(bf->bf_mem_ptr <= bf->bf_mem_size);

  if(bf->bf_mem_ptr == bf->bf_mem_size)
    bf->bf_mem_ptr = 0;

  if(s2 > 0) {
    erase_zone(bf, bf->bf_mem_ptr, s2);

    map_zone(bf, bf->bf_mem_ptr, s2, bf->bf_fpos + s1);
    memcpy(bf->bf_mem + bf->bf_mem_ptr, buf + s1, s2);

    bf->bf_mem_ptr += s2;
  }
}



/**
 *
 */
static int
fab_read(fa_handle_t *handle, void *buf, size_t size)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *src = bf->bf_src;

  if(bf->bf_mem == NULL) {
    bf->bf_mem_size = BF_CACHE_SIZE;
    bf->bf_mem = halloc(bf->bf_mem_size);
  }

  if(bf->bf_size != -1 && bf->bf_fpos + size > bf->bf_size)
    size = bf->bf_size - bf->bf_fpos;

  size_t rval = 0;
  while(size > 0) {
    int mpos = -1;
    int cs = resolve_zone(bf, bf->bf_fpos, size, &mpos);
    if(cs > 0) {
      // Cache hit
      memcpy(buf, bf->bf_mem + mpos, cs);
      rval += cs;
      buf += cs;
      bf->bf_fpos += cs;
      size -= cs;
      continue;
    }

    int rreq = need_to_fill(bf, bf->bf_fpos, size);
    if(rreq >= BF_MIN_READ) {

      if(src->fh_proto->fap_seek(src, bf->bf_fpos, SEEK_SET) != bf->bf_fpos)
	return -1;

      int r = src->fh_proto->fap_read(src, buf, rreq);
      if(r > 0) {
	store_in_cache(bf, buf, r);
	rval += r;
	buf += r;
	bf->bf_fpos += r;
	size -= r;
      }
      if(r != rreq) {
	bf->bf_size = bf->bf_fpos;
	return r < 0 ? r : rval;
      }
      continue;
    }

    if(bf->bf_mem_ptr + BF_MIN_READ > bf->bf_mem_size)
      bf->bf_mem_ptr = 0;
    
    erase_zone(bf, bf->bf_mem_ptr, BF_MIN_READ);

    if(src->fh_proto->fap_seek(src, bf->bf_fpos, SEEK_SET) != bf->bf_fpos)
      return -1;

    int r = src->fh_proto->fap_read(src, bf->bf_mem + bf->bf_mem_ptr,
				    BF_MIN_READ);
    if(r < 1) {
      bf->bf_size = bf->bf_fpos;
      return r < 0 ? r : rval;
    }

    map_zone(bf, bf->bf_mem_ptr, r, bf->bf_fpos);

    if(r != BF_MIN_READ) {
      // EOF
      bf->bf_size = bf->bf_fpos + r;

      int r2 = MIN(size, r);
      memcpy(buf, bf->bf_mem + bf->bf_mem_ptr, r2);
      bf->bf_mem_ptr += r;
      rval += r2;

      bf->bf_fpos += r2;

      return rval;
    } else {
      bf->bf_mem_ptr += r;
    }

  }
  return rval;
}


#if BK_CHK
static int
fab_read(fa_handle_t *handle, void *buf, size_t size)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  int64_t pre = bf->bf_fpos;
  printf("fab_read(%ld + %zd) <eof = %ld> = ", bf->bf_fpos, size,
	 bf->bf_size);

  int r = fab_read0(handle, buf, size);

  printf("%d <now at %ld  d=%ld>\n", r, bf->bf_fpos, bf->bf_fpos - pre);

  unsigned char *m1 = malloc(size);
  unsigned char *m2 = buf;

  if(fa_seek(bf->bf_chk, pre, SEEK_SET) != pre) {
    printf("Seek to %ld failed\n", pre);
    abort();
  }
  int r2 = fa_read(bf->bf_chk, m1, size);

  printf("  r=%d r2=%d\n", r, r2);

  assert(r == r2);

  int i;
  for(i = 0; i < r; i++) {
    if(m1[i] != m2[i]) {
      printf("Mismatch at byte %d %02x != %02x\n", i, m1[i], m2[i]);
      abort();
    }
  }
  free(m1);

  return r;
}
#endif


/**
 *
 */
static int 
fab_seek_is_fast(fa_handle_t *handle)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *fh = bf->bf_src;
  if(fh->fh_proto->fap_seek_is_fast != NULL)
    return fh->fh_proto->fap_seek_is_fast(fh);
  return 1;
}


/**
 *
 */
static fa_protocol_t fa_protocol_buffered = {
  .fap_name  = "buffer",
  .fap_close = fab_close,
  .fap_read  = fab_read,
  .fap_seek  = fab_seek,
  .fap_fsize = fab_fsize,
  .fap_seek_is_fast = fab_seek_is_fast,
};


/**
 *
 */
fa_handle_t *
fa_buffered_open(const char *url, char *errbuf, size_t errsize, int flags,
		 struct prop *stats)
{
  fa_handle_t *fh = fa_open_ex(url, errbuf, errsize, flags, stats);
  if(fh == NULL)
    return NULL;

  if(!(fh->fh_proto->fap_flags & FAP_ALLOW_CACHE))
    return fh;

  buffered_file_t *bf = calloc(1, sizeof(buffered_file_t));
  bf->bf_src = fh;
  bf->bf_size = -1;
  bf->h.fh_proto = &fa_protocol_buffered;
#if BF_CHK
  bf->bf_chk = fa_open_ex(url, NULL, 0, 0, NULL);
#endif
  return &bf->h;
}
