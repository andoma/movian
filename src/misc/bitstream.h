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

#pragma once

#include <stdint.h>

typedef struct bitstream {
  union {
    const uint8_t *rdata;
    void *opaque;
  };

  void (*skip_bits)(struct bitstream *bs, int num);
  unsigned int (*read_bits)(struct bitstream *bs, int num);
  unsigned int (*read_bits1)(struct bitstream *bs);
  unsigned int (*read_golomb_ue)(struct bitstream *bs);
  signed int (*read_golomb_se)(struct bitstream *bs);
  int (*bits_left)(struct bitstream *bs);

  int bytes_length;
  int bytes_offset;
  int remain;
  uint8_t tmp;
  uint8_t rbsp;
} bitstream_t;

void init_rbits(bitstream_t *bs, const uint8_t *data, int size, int rbsp);

