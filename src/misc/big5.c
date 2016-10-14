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
#include <inttypes.h>
#include "str.h"
#include "big5.h"

#define BIG5_TABLE_OFFSET 0xa100

static const uint16_t big5table[] = {
#include "big5_table.h"
};


int
big5_convert(const struct charset *cs, char *dst,
             const uint8_t *src, int len, int strict)
{
  int outlen = 0;

  for(int i = 0; i < len; i++) {
    if(*src < 0x80) {
      if(dst != NULL)
        *dst++ = *src;
      outlen++;
      src++;
      continue;
    }

    unsigned int in;

    if(len == 1) {
      in = -1;
      src++;
    } else {
      in = (src[0] << 8) | src[1];
      in -= BIG5_TABLE_OFFSET;
      src += 2;
      i++;
    }

    uint16_t out;

    if(in > sizeof(big5table) / 2) {

      if(strict)
        return -1;

      out = 0xfffd;
    } else {
      out = big5table[in];
      if(out == 0) {
        if(strict)
          return -1;
        else
          out = 0xfffd;
      }
    }

    int ol = utf8_put(dst, out);
    outlen += ol;
    if(dst)
      dst += ol;
  }
  return outlen;
}


