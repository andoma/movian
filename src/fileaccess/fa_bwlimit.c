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
typedef struct bwlimit {
  fa_handle_t h;
  fa_handle_t *s_src;
  int s_bps; // bytes per second
  int64_t s_spill;
} bwlimit_t;


/**
 *
 */
static void
bwlimit_close(fa_handle_t *h)
{
  bwlimit_t *s = (bwlimit_t *)h;
  fa_close(s->s_src);
  free(s);
}


/**
 *
 */
static int64_t
bwlimit_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  bwlimit_t *s = (bwlimit_t *)handle;
  return fa_seek(s->s_src, pos, whence);
}


/**
 *
 */
static int64_t
bwlimit_fsize(fa_handle_t *handle)
{
  bwlimit_t *s = (bwlimit_t *)handle;
  return fa_fsize(s->s_src);
}



/**
 *
 */
static int
bwlimit_read(fa_handle_t *handle, void *buf, size_t size)
{
  bwlimit_t *s = (bwlimit_t *)handle;
  int64_t ts = showtime_get_ts();
  int r = fa_read(s->s_src, buf, size);
  ts = showtime_get_ts() - ts;

  int64_t delay = s->s_spill + r * 1000000LL / s->s_bps - ts;

  if(delay > 0) {
    usleep(delay);
    s->s_spill = 0;
  } else {
    s->s_spill = ts;
  }
  return r;
}


/**
 *
 */
static int
bwlimit_seek_is_fast(fa_handle_t *handle)
{
  bwlimit_t *s = (bwlimit_t *)handle;
  return fa_seek_is_fast(s->s_src);
}


/**
 *
 */
static fa_protocol_t fa_protocol_bwlimit = {
  .fap_name  = "bwlimit",
  .fap_close = bwlimit_close,
  .fap_read  = bwlimit_read,
  .fap_seek  = bwlimit_seek,
  .fap_fsize = bwlimit_fsize,
  .fap_seek_is_fast = bwlimit_seek_is_fast,
};


/**
 *
 */
fa_handle_t *
fa_bwlimit_open(fa_handle_t *fa, int bps)
{
  bwlimit_t *s = calloc(1, sizeof(bwlimit_t));
  s->h.fh_proto = &fa_protocol_bwlimit;
  s->s_bps = bps;
  s->s_src = fa;
  return &s->h;
}
