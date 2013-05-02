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

#include <libavutil/aes.h>

#include "arch/halloc.h"

#include "showtime.h"
#include "fileaccess.h"
#include "fa_proto.h"

#define MAX_BUFFER_BLOCKS 150
#define BLOCKSIZE 16

/**
 *
 */
typedef struct aes_fh {
  fa_handle_t h;
  fa_handle_t *src;
  uint8_t iv[16];
  struct AVAES *aes;
  
  uint8_t  inbuffer[BLOCKSIZE*MAX_BUFFER_BLOCKS];
  uint8_t outbuffer[BLOCKSIZE*MAX_BUFFER_BLOCKS];
  uint8_t *outptr;
  int inlen, inlen_used, outlen, eof;

} aes_fh_t;


/**
 *
 */
static void
aes_close(fa_handle_t *h)
{
  aes_fh_t *a = (aes_fh_t *)h;
  a->src->fh_proto->fap_close(a->src);
  free(a);
}


/**
 *
 */
static int64_t
aescbc_seek(fa_handle_t *handle, int64_t pos, int whence)
{
  TRACE(TRACE_ERROR, "AES", "Seeking in CBC not possible");
  return -1;
}


/**
 *
 */
static int64_t
aescbc_fsize(fa_handle_t *handle)
{
  return -1;
}



/**
 *
 */
static int
aescbc_read(fa_handle_t *handle, void *buf, size_t size)
{
  int blocks;
  aes_fh_t *a = (aes_fh_t *)handle;

  while(1) {
      
    if(a->outlen > 0) {
      size = MIN(size, a->outlen);
      memcpy(buf, a->outptr, size);
      a->outptr += size;
      a->outlen -= size;
      return size;
    }

    while(a->inlen - a->inlen_used < 2 * BLOCKSIZE) {
      int n = a->src->fh_proto->fap_read(a->src, a->inbuffer + a->inlen,
					 sizeof(a->inbuffer) - a->inlen);
      if(n <= 0) {
	a->eof = 1;
	break;
      }
      a->inlen += n;
    }

    if((blocks = (a->inlen - a->inlen_used) / BLOCKSIZE) == 0)
      return -1;

    if(!a->eof)
      blocks--;

    av_aes_crypt(a->aes, a->outbuffer, a->inbuffer + a->inlen_used, blocks,
                 a->iv, 1);

    a->outlen = BLOCKSIZE * blocks;
    a->outptr = a->outbuffer;
    a->inlen_used += BLOCKSIZE * blocks;

    if(a->inlen_used >= sizeof(a->inbuffer) / 2) {
      memmove(a->inbuffer, a->inbuffer + a->inlen_used,
	      a->inlen - a->inlen_used);
      a->inlen -= a->inlen_used;
      a->inlen_used = 0;
    }
    if(a->eof)
      a->outlen -= a->outbuffer[a->outlen - 1];
  }
}


/**
 *
 */
static int
aescbc_seek_is_fast(fa_handle_t *handle)
{
  return 0;
}


/**
 *
 */
static fa_protocol_t fa_protocol_aescbc = {
  .fap_name  = "aescbc",
  .fap_close = aes_close,
  .fap_read  = aescbc_read,
  .fap_seek  = aescbc_seek,
  .fap_fsize = aescbc_fsize,
  .fap_seek_is_fast = aescbc_seek_is_fast,
};


/**
 *
 */
fa_handle_t *
fa_aescbc_open(fa_handle_t *fa, const uint8_t *iv, const uint8_t *key)
{
  aes_fh_t *a = calloc(1, sizeof(aes_fh_t));
  a->h.fh_proto = &fa_protocol_aescbc;
  a->src = fa;
  memcpy(a->iv,  iv,  16);

  a->aes = av_aes_alloc();
  av_aes_init(a->aes, key, 128, 1);
  return &a->h;
}
