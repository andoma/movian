/*
 *  Expose part of a file as a new file
 *  Copyright (C) 2013 Andreas Öman
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
#include "arch/halloc.h"

#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"

/**
 *
 */
typedef struct slice {
  fa_handle_t h;
  fa_handle_t *s_src;
  int64_t s_offset;
  int64_t s_size;
  int64_t s_fpos;
} slice_t;


/**
 *
 */
static void
slice_close(fa_handle_t *h)
{
  slice_t *s = (slice_t *)h;
  s->s_src->fh_proto->fap_close(s->s_src);
  free(s);
}


/**
 *
 */
static int64_t
slice_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  slice_t *s = (slice_t *)handle;
  int64_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = s->s_fpos + pos;
    break;

  case SEEK_END:
    np = s->s_size + pos;
    break;

  default:
    return -1;
  }

  if(np < 0)
    return -1;

  s->s_fpos = np;
  return np;
}


/**
 *
 */
static int64_t
slice_fsize(fa_handle_t *handle)
{
  slice_t *s = (slice_t *)handle;
  return s->s_size;
}



/**
 *
 */
static int
slice_read(fa_handle_t *handle, void *buf, size_t size)
{
  slice_t *s = (slice_t *)handle;

  if(s->s_fpos + size > s->s_size)
    size = s->s_size - s->s_fpos;

  if(size <= 0)
    return 0;

  int64_t p = s->s_fpos + s->s_offset;
  if(fa_seek(s->s_src, p, SEEK_SET) != p)
    return -1;

  int r = fa_read(s->s_src, buf, size);

  if(r >= 0)
    s->s_fpos += r;
  return r;
}


/**
 *
 */
static int
slice_seek_is_fast(fa_handle_t *handle)
{
  slice_t *s = (slice_t *)handle;
  fa_handle_t *fh = s->s_src;
  if(fh->fh_proto->fap_seek_is_fast != NULL)
    return fh->fh_proto->fap_seek_is_fast(fh);
  return 1;
}


/**
 *
 */
static fa_protocol_t fa_protocol_slice = {
  .fap_name  = "slice",
  .fap_close = slice_close,
  .fap_read  = slice_read,
  .fap_seek  = slice_seek,
  .fap_fsize = slice_fsize,
  .fap_seek_is_fast = slice_seek_is_fast,
};


/**
 *
 */
fa_handle_t *
fa_slice_open(fa_handle_t *fa, int64_t offset, int64_t size)
{
  slice_t *s = calloc(1, sizeof(slice_t));
  s->h.fh_proto = &fa_protocol_slice;
  s->s_src = fa;
  s->s_offset = offset;
  s->s_size = size;
  return &s->h;
}
