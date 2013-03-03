/*
 *  h264 annex.b helpers
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

#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "h264_annexb.h"

/**
 *
 */
static void
h264_to_annexb_inplace(uint8_t *b, size_t fsize)
{
  uint8_t *p = b;
  while(p < b + fsize) {
    if(p[0])
      break; // Avoid overflows with this simple check
    int len = (p[1] << 16) + (p[2] << 8) + p[3];
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 1;
    p += len + 4;
  }
}


/**
 *
 */
static int
h264_to_annexb_buffered(uint8_t *dst, const uint8_t *b, size_t fsize, int lsize)
{
  const uint8_t *p = b;
  int i;
  int ol = 0;
  while(p < b + fsize) {

    int len = 0;
    for(i = 0; i < lsize; i++)
      len = len << 8 | *p++;

    if(dst) {
      dst[0] = 0;
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = 1;
      memcpy(dst + 4, p, len);
      dst += 4 + len;
    }
    ol += 4 + len;
    p += len;
  }
  return ol;
}


/**
 *
 */
int
h264_to_annexb(h264_annexb_ctx_t *ctx, uint8_t **datap, size_t *sizep)
{
  int l;

  switch(ctx->lsize) {
  case 4:
    h264_to_annexb_inplace(*datap, *sizep);
  case 0:
    

    //    submit_au(vdd, &au, mb->mb_data, mb->mb_size, mb->mb_skip == 1, vd);
    break;
  case 3:
  case 2:
  case 1:
    l = h264_to_annexb_buffered(NULL, *datap, *sizep, ctx->lsize);
    if(l > ctx->tmpbufsize) {
      ctx->tmpbuf = realloc(ctx->tmpbuf, l);
      ctx->tmpbufsize = l;
    }
    if(ctx->tmpbuf == NULL)
      return -1;
    h264_to_annexb_buffered(ctx->tmpbuf, *datap, *sizep, ctx->lsize);
    *datap = ctx->tmpbuf;
    *sizep = l;
    break;
  }
  
  //    submit_au(vdd, &au, vdd->tmpbuf, l, mb->mb_skip == 1, vd);
 
 return 0;
}


/**
 *
 */
static void
append_extradata(h264_annexb_ctx_t *ctx, const uint8_t *data, int len)
{
  ctx->extradata = realloc(ctx->extradata, ctx->extradata_size + len);
  memcpy(ctx->extradata + ctx->extradata_size, data, len);
  ctx->extradata_size += len;
}

/**
 *
 */
void
h264_to_annexb_init(h264_annexb_ctx_t *ctx, const uint8_t *data, int len)
{
  int i, n, s;

  uint8_t buf[4] = {0,0,0,1};

  if(len < 7 || data[0] != 1)
    return;

  int lsize = (data[4] & 0x3) + 1;
  if(lsize == 3)
    return;

  n = data[5] & 0x1f;
  data += 6;
  len -= 6;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    append_extradata(ctx, buf, 4);
    append_extradata(ctx, data + 2, s - 2);
    data += s;
    len -= s;
  }
  
  if(len < 1)
    return;
  n = *data++;
  len--;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    append_extradata(ctx, buf, 4);
    append_extradata(ctx, data + 2, s - 2);
    data += s;
    len -= s;
  }

  ctx->lsize = lsize;
}
