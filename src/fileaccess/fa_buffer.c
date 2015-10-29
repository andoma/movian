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
#include "arch/halloc.h"

#include "main.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "misc/minmax.h"

#define FILE_PARKING 1

#define BF_CHK 0

#define BF_ZONES 8
#define BF_MASK (BF_ZONES - 1)

static HTS_MUTEX_DECL(buffered_global_mutex);

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

  time_t bf_park_time;

  fa_handle_t *bf_src;
  cancellable_t *bf_outbound_cancellable;

  cancellable_t *bf_inbound_cancellable;

#if BF_CHK
  fa_handle_t *bf_chk;
#endif
  void *bf_mem;
  size_t bf_mem_size;
  int bf_min_request;

  int bf_mem_ptr;

  int64_t bf_fpos;

  int bf_replace_ptr;

  int64_t bf_size;

  int bf_flags;

  char *bf_url;

  buffered_zone_t bf_zones[BF_ZONES];


} buffered_file_t;


static buffered_file_t *parked;

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





static void attribute_unused
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
fab_destroy(buffered_file_t *bf)
{
  bf->bf_src->fh_proto->fap_close(bf->bf_src);

  if(bf->bf_mem != NULL)
    hfree(bf->bf_mem, bf->bf_mem_size);
  free(bf->bf_url);
  cancellable_release(bf->bf_outbound_cancellable);
  free(bf);
}



/**
 *
 */
static void
fab_close(fa_handle_t *handle)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  cancellable_unbind(bf->bf_inbound_cancellable, bf);
  bf->bf_inbound_cancellable = NULL;

#ifdef FILE_PARKING
  buffered_file_t *closeme = NULL;
  fa_handle_t *src = bf->bf_src;


  if((src->fh_proto->fap_no_parking != NULL &&
      src->fh_proto->fap_no_parking(src)) ||
     bf->bf_flags & FA_NO_PARKING ||
     cancellable_is_cancelled(bf->bf_outbound_cancellable)) {
    fab_destroy(bf);
    return;
  }

  hts_mutex_lock(&buffered_global_mutex);
  if(parked)
    closeme = parked;

  parked = bf;
  time(&parked->bf_park_time);
  hts_mutex_unlock(&buffered_global_mutex);

  if(closeme)
    fab_destroy(closeme);
#else
  fab_destroy(bf);
#endif
}


/**
 *
 */
static int64_t
fab_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *src = bf->bf_src;
  int64_t np;


  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = bf->bf_fpos + pos;
    break;

  case SEEK_END:
    np = src->fh_proto->fap_seek(src, pos, whence, lazy);
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  int mpos;
  int cs = resolve_zone(bf, np, 1, &mpos);

  if(cs == -1) {
    // If seeked to position is not mapped in our buffers, seek in
    // source to check if it's possible to reach position at all.

    if(src->fh_proto->fap_seek(src, np, SEEK_SET, lazy) != np)
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
    bf->bf_mem = halloc(bf->bf_mem_size);
    if(bf->bf_mem == NULL)
      return -1;
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
    if(rreq >= bf->bf_min_request) {

      if(src->fh_proto->fap_seek(src, bf->bf_fpos, SEEK_SET, 0) != bf->bf_fpos)
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

    if(bf->bf_mem_ptr + bf->bf_min_request > bf->bf_mem_size)
      bf->bf_mem_ptr = 0;
    
    erase_zone(bf, bf->bf_mem_ptr, bf->bf_min_request);

    if(src->fh_proto->fap_seek(src, bf->bf_fpos, SEEK_SET, 0) != bf->bf_fpos)
      return -1;

    int r = src->fh_proto->fap_read(src, bf->bf_mem + bf->bf_mem_ptr,
				    bf->bf_min_request);
    if(r < 1) {
      bf->bf_size = bf->bf_fpos;
      return r < 0 ? r : rval;
    }

    map_zone(bf, bf->bf_mem_ptr, r, bf->bf_fpos);

    if(r != bf->bf_min_request) {
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


#if BF_CHK
static int
fab_read_chk(fa_handle_t *handle, void *buf, size_t size)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  int64_t pre = bf->bf_fpos;
  printf("fab_read(%ld + %zd) <eof = %ld> = ", bf->bf_fpos, size,
	 bf->bf_size);

  int r = fab_read(handle, buf, size);

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
static void
fab_set_read_timeout(fa_handle_t *handle, int ms)
{
  buffered_file_t *bf = (buffered_file_t *)handle;
  fa_handle_t *fh = bf->bf_src;
  if(fh->fh_proto->fap_set_read_timeout != NULL)
    fh->fh_proto->fap_set_read_timeout(fh, ms);
}


/**
 *
 */
static fa_protocol_t fa_protocol_buffered = {
  .fap_name  = "buffer",
  .fap_close = fab_close,
#if BF_CHK
  .fap_read  = fab_read_chk,
#else
  .fap_read  = fab_read,
#endif
  .fap_seek  = fab_seek,
  .fap_fsize = fab_fsize,
  .fap_set_read_timeout = fab_set_read_timeout,
};


/**
 *
 */
static void
fab_cancel(void *aux)
{
  buffered_file_t *bf = aux;
  cancellable_cancel_locked(bf->bf_outbound_cancellable);
}

/**
 *
 */
fa_handle_t *
fa_buffered_open(const char *url, char *errbuf, size_t errsize, int flags,
                 struct fa_open_extra *foe)
{
  buffered_file_t *closeme = NULL;
  fa_handle_t *fh;
  fa_protocol_t *fap;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return NULL;

  if(!(fap->fap_flags & FAP_ALLOW_CACHE)) {
    fh = fap->fap_open(fap, filename, errbuf, errsize, flags, foe);
    fap_release(fap);
    free(filename);
    return fh;
  }

  fh = NULL;
  hts_mutex_lock(&buffered_global_mutex);

  if(parked) {
    // Flush too old parked files
    time_t now;
    time(&now);
    if(now - parked->bf_park_time > 10) {
      closeme = parked;
      parked = NULL;
    }
  }


  if(parked && !strcmp(parked->bf_url, url)) {
    parked->bf_fpos = 0;
    fh = (fa_handle_t *)parked;
    parked = NULL;
  }

  hts_mutex_unlock(&buffered_global_mutex);

  if(closeme != NULL)
    fab_destroy(closeme);

  if(fh != NULL) {
    fap_release(fap);
    free(filename);

    if(foe != NULL && foe->foe_cancellable != NULL) {
      buffered_file_t *bf = (buffered_file_t *)fh;
      assert(bf->bf_inbound_cancellable == NULL);
      bf->bf_inbound_cancellable =
        cancellable_bind(foe->foe_cancellable, fab_cancel, fh);
    }
    return fh;
  }

  int mflags = flags;
  flags &= ~ (FA_BUFFERED_SMALL | FA_BUFFERED_BIG | FA_BUFFERED_NO_PREFETCH);

  fa_open_extra_t new_foe;

  buffered_file_t *bf = calloc(1, sizeof(buffered_file_t));

  bf->bf_outbound_cancellable = cancellable_create();

  if(foe != NULL) {
    if(foe->foe_cancellable != NULL) {
      bf->bf_inbound_cancellable =
        cancellable_bind(foe->foe_cancellable, fab_cancel, bf);
    }

    foe->foe_cancellable = bf->bf_outbound_cancellable;
  } else {
    memset(&new_foe, 0, sizeof(fa_open_extra_t));
    new_foe.foe_cancellable = bf->bf_outbound_cancellable;
    foe = &new_foe;
  }

  fh = fap->fap_open(fap, filename, errbuf, errsize, flags, foe);
  fap_release(fap);
  free(filename);
  if(fh == NULL) {
    cancellable_unbind(bf->bf_inbound_cancellable, bf);
    free(bf);
    return NULL;
  }

  bf->bf_url = strdup(url);
  if(!(mflags & FA_BUFFERED_NO_PREFETCH))
    bf->bf_min_request = mflags & FA_BUFFERED_BIG ? 256 * 1024 : 64 * 1024;
  bf->bf_mem_size = 1024 * 1024;
  bf->bf_flags = flags;

  bf->bf_src = fh;
  bf->bf_size = -1;
  bf->h.fh_proto = &fa_protocol_buffered;
#if BF_CHK
  bf->bf_chk = fa_open_ex(url, NULL, 0, 0, NULL);
#endif
  return &bf->h;
}
