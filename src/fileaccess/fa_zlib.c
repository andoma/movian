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

#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fileaccess.h"
#include "fa_zlib.h"
#include "showtime.h"


typedef struct fa_inflator {
  fa_handle_t h;

  z_stream fi_zstream;
  
  fa_handle_t *fi_src_handle;
  const fa_protocol_t *fi_src_fap;

  int64_t fi_unc_size;  // Uncompressed size
  int64_t fi_pos;       // Current file position

  int64_t fi_bufstart;  // fi_buf starts at this position
  size_t fi_bufsize;
  uint8_t *fi_buf;

  uint8_t *fi_load_buf;

  int fi_load_size;

} fa_inflator_t;

#define DECODESIZE 32768


/**
 *
 */
fa_handle_t *
fa_inflate_init(const fa_protocol_t *src_fap, fa_handle_t *handle,
		int64_t unc_size)
{
  fa_inflator_t *fi = calloc(1, sizeof(fa_inflator_t));

  fi->fi_src_fap    = src_fap;
  fi->fi_src_handle = handle;
  fi->fi_unc_size   = unc_size;

  if(inflateInit2(&fi->fi_zstream, -MAX_WBITS) != Z_OK) {
    free(fi);
    return NULL;
  }
  
  fi->fi_load_size = 32768;
  fi->fi_buf       = malloc(DECODESIZE);
  return &fi->h;
}

/**
 *
 */
static void
inflate_close(fa_handle_t *handle)
{
  fa_inflator_t *fi = (fa_inflator_t *)handle;

  fi->fi_src_fap->fap_close(fi->fi_src_handle);
  inflateEnd(&fi->fi_zstream);
  free(fi->fi_buf);
  free(fi->fi_load_buf);
  free(fi);
}



/**
 *
 */
static int
inflate_read(fa_handle_t *handle, void *buf, size_t size)
{
  fa_inflator_t *fi = (fa_inflator_t *)handle;
  int total_read = 0;
  int n, c, r, stream_end = 0;

  while(size > 0) {

    if(fi->fi_pos < fi->fi_bufstart) {
      /* Rewind stream from start */

      inflateEnd(&fi->fi_zstream);

      fi->fi_bufstart = 0;
      fi->fi_bufsize  = 0;

      memset(&fi->fi_zstream, 0, sizeof(z_stream));

      inflateInit2(&fi->fi_zstream, -MAX_WBITS);

      fi->fi_src_fap->fap_seek(fi->fi_src_handle, 0, SEEK_SET);
    }

    n = fi->fi_pos - fi->fi_bufstart;  // Offset in decompressed buffer

    if(n >= 0 && n < fi->fi_bufsize) {

      c = MIN(fi->fi_bufsize - n, size);

      memcpy(buf + total_read, fi->fi_buf + n, c);

      size -= c;
      total_read += c;
      fi->fi_pos += c;
      continue;
    }
    
    if(stream_end)
      break;

    fi->fi_bufstart += fi->fi_bufsize;
    fi->fi_zstream.next_out  = fi->fi_buf;
    fi->fi_zstream.avail_out = DECODESIZE;
    
    while(fi->fi_zstream.avail_out > 0) {

      if(fi->fi_zstream.avail_in == 0) {

	if(fi->fi_load_size < 128 * 1024)
	  fi->fi_load_size *= 2;

	fi->fi_load_buf = realloc(fi->fi_load_buf, fi->fi_load_size);

	r = fi->fi_src_fap->fap_read(fi->fi_src_handle, 
				     fi->fi_load_buf, fi->fi_load_size);
	if(r < 0)
	  r = 0;
	fi->fi_zstream.avail_in = r;
	fi->fi_zstream.next_in  = fi->fi_load_buf;
      }

      r = inflate(&fi->fi_zstream, 0);

      if(r == Z_STREAM_END) {
	stream_end = 1;
	break;
      }

      if(r != Z_OK)
	return -1;
    }
    fi->fi_bufsize = DECODESIZE - fi->fi_zstream.avail_out;
  }
  return total_read;
}


/**
 * Seek in file
 */
static int64_t
inflate_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  fa_inflator_t *fi = (fa_inflator_t *)handle;
  off_t np;

  switch(whence) {
  case SEEK_SET:
    np = pos;
    break;

  case SEEK_CUR:
    np = fi->fi_pos + pos;
    break;

  case SEEK_END:
    np = fi->fi_unc_size + pos;
    break;
  default:
    return -1;
  }

  if(np < 0)
    return -1;

  fi->fi_pos = np;
  return np;
}


/**
 *
 */
static int64_t
inflate_fsize(fa_handle_t *handle)
{
  fa_inflator_t *fi = (fa_inflator_t *)handle;
  return fi->fi_unc_size;
}



fa_protocol_t fa_protocol_inflate = {
  .fap_name = "inflate",
  .fap_close = inflate_close,
  .fap_read  = inflate_read,
  .fap_seek  = inflate_seek,
  .fap_fsize = inflate_fsize,
};
