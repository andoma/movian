/*
 *  Inflate a .gz file in memory
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <zlib.h>

#include "gz.h"
#include "showtime.h"

/**
 *
 */
int
gz_check(const char *in, size_t inlen)
{
  if(inlen < 10 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 0x08)
    return 0;
  return 1;
}


/**
 *
 */
void *
gz_inflate(char *in, size_t inlen, size_t *outlenptr,
	   char *errbuf, size_t errlen)
{
  z_stream z = {0};
  unsigned char *out;
  size_t outlen;
  int r;

  if(!gz_check(in, inlen)) {
    snprintf(errbuf, errlen, "Invalid header");
    return NULL;
  }

  if(in[3] != 0) {
    snprintf(errbuf, errlen, "Header extensions is not supported");
    return NULL;
  }

  in += 10;
  inlen -= 10;

  z.next_in = (void *)in;
  z.avail_in = inlen;

  if(inflateInit2(&z, -MAX_WBITS) != Z_OK) {
    snprintf(errbuf, errlen, "Inflate init failed");
    return NULL;
  }

  outlen = inlen * 2;
  out = mymalloc(outlen + 1);
  if(out == NULL) {
    snprintf(errbuf, errlen, "Out of memory");
    inflateEnd(&z);
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
      return NULL;
    }
  }

  out[z.total_out] = 0;
  *outlenptr = z.total_out;
  inflateEnd(&z);
  return out;
}
