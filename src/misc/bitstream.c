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
#include <inttypes.h>
#include "bitstream.h"


static int
bs_eof(const bitstream_t *bs)
{
  return bs->bytes_offset >= bs->bytes_length;
}

static unsigned int
read_bits(bitstream_t *bs, int num)
{
  int r = 0;

  while(num > 0) {
    if(bs->bytes_offset >= bs->bytes_length)
      return 0;
    
    if(bs->remain == 0) {
      bs->tmp = bs->rdata[bs->bytes_offset];
      bs->bytes_offset++;

      if(bs->rbsp && bs->bytes_offset >= 2 &&
	 bs->bytes_offset < bs->bytes_length) {
	if(bs->rdata[bs->bytes_offset - 2] == 0 &&
	   bs->rdata[bs->bytes_offset - 1] == 0 &&
	   bs->rdata[bs->bytes_offset    ] == 3) {
	  bs->bytes_offset++;
	}
      }
      bs->remain = 8;
    }

    num--;
    bs->remain--;
    if(bs->tmp & (1 << bs->remain))
      r |= 1 << num;
  }
  return r;
}

static unsigned int
read_bits1(bitstream_t *bs)
{
  return read_bits(bs, 1);
}


static void
skip_bits(bitstream_t *bs, int num)
{
  read_bits(bs, num);
}


static unsigned int
read_golomb_ue(bitstream_t *bs)
{
  int b, lzb = -1;
  for(b = 0; !b && !bs_eof(bs); lzb++) {
    b = read_bits1(bs);
  }

  return (1 << lzb) - 1 + read_bits(bs, lzb);
}


static signed int
read_golomb_se(bitstream_t *bs)
{
  int v, pos;
  v = read_golomb_ue(bs);
  if(v == 0)
    return 0;

  pos = v & 1;
  v = (v + 1) >> 1;
  return pos ? v : -v;
}


static int
bits_left(struct bitstream *bs)
{
  return bs->remain + (bs->bytes_length - bs->bytes_offset) * 8;
}


void
init_rbits(bitstream_t *bs, const uint8_t *data, int length, int rbsp)
{
  bs->rdata = data;
  bs->bytes_offset = 0;
  bs->bytes_length = length;
  bs->remain = 0;
  bs->rbsp = rbsp;

  bs->skip_bits      = skip_bits;
  bs->read_bits      = read_bits;
  bs->read_bits1     = read_bits1;
  bs->read_golomb_ue = read_golomb_ue;
  bs->read_golomb_se = read_golomb_se;
  bs->bits_left      = bits_left;
}
