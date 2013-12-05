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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <zlib.h>

#include "gz.h"
#include "showtime.h"
#include "misc/buf.h"

/**
 *
 */
int
gz_check(const buf_t *b)
{
  const uint8_t *in = buf_c8(b);
  if(b->b_size < 10 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 0x08)
    return 0;
  return 1;
}


/**
 *
 */
buf_t *
gz_inflate(buf_t *bin, char *errbuf, size_t errlen)
{
  z_stream z = {0};
  unsigned char *out;
  size_t outlen;
  int r;

  if(!gz_check(bin)) {
    snprintf(errbuf, errlen, "Invalid header");
    buf_release(bin);
    return NULL;
  }

  const uint8_t *in = buf_c8(bin);
  size_t inlen = bin->b_size;

  if(in[3] != 0) {
    snprintf(errbuf, errlen, "Header extensions is not supported");
    buf_release(bin);
    return NULL;
  }

  in += 10;
  inlen -= 10;

  z.next_in = (void *)in;
  z.avail_in = inlen;

  if(inflateInit2(&z, -MAX_WBITS) != Z_OK) {
    snprintf(errbuf, errlen, "Inflate init failed");
    buf_release(bin);
    return NULL;
  }

  outlen = inlen * 2;
  out = mymalloc(outlen + 1);
  if(out == NULL) {
    snprintf(errbuf, errlen, "Out of memory");
    inflateEnd(&z);
    buf_release(bin);
    return NULL;
  }

  while(1) {
    
    if(outlen - z.total_out == 0) {
      outlen *= 2;
      out = realloc(out, outlen+1);
    }

    z.next_out  = out    + z.total_out;
    z.avail_out = outlen - z.total_out;

    r = inflate(&z, 0);
    if(r == Z_STREAM_END)
      break;

    if(r != Z_OK) {
      snprintf(errbuf, errlen, "inflate: %s", z.msg);
      inflateEnd(&z);
      free(out);
      buf_release(bin);
      return NULL;
    }
  }

  out[z.total_out] = 0;
  inflateEnd(&z);
  buf_release(bin);
  return buf_create_and_adopt(z.total_out, out, &free);
}
