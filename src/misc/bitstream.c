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
/*
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
#include <inttypes.h>
#include "bitstream.h"


static void
skip_bits(bitstream_t *bs, int num)
{
  bs->offset += num;
}

static int
bs_eof(const bitstream_t *bs)
{
  return bs->offset >= bs->len;
}

static unsigned int
read_bits(bitstream_t *bs, int num)
{
  int r = 0;

  while(num > 0) {
    if(bs->offset >= bs->len)
      return 0;

    num--;

    if(bs->rdata[bs->offset / 8] & (1 << (7 - (bs->offset & 7))))
      r |= 1 << num;

    bs->offset++;
  }
  return r;
}

static unsigned int
read_bits1(bitstream_t *bs)
{
  return read_bits(bs, 1);
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




void
init_rbits(bitstream_t *bs, const uint8_t *data, int length)
{
  bs->rdata = data;
  bs->offset = 0;
  bs->len = length * 8;

  bs->skip_bits      = skip_bits;
  bs->read_bits      = read_bits;
  bs->read_bits1     = read_bits1;
  bs->read_golomb_ue = read_golomb_ue;
  bs->read_golomb_se = read_golomb_se;
}
