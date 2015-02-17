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
#pragma once
#include <string.h>
#include <stdint.h>

static __inline void wr64_be(uint8_t *ptr, uint64_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  memcpy(ptr, &val, 8);
}


static __inline void wr32_be(uint8_t *ptr, uint32_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  memcpy(ptr, &val, 4);
}


static __inline void wr16_be(uint8_t *ptr, uint16_t val)
{
#if !defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  memcpy(ptr, &val, 2);
}



static __inline uint64_t rd64_be(const uint8_t *ptr)
{
  uint64_t val;
  memcpy(&val, ptr, 8);
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  return val;
}


static __inline uint32_t rd32_be(const uint8_t *ptr)
{
  uint32_t val;
  memcpy(&val, ptr, 4);
#if !defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  return val;
}



static __inline uint16_t rd16_be(const uint8_t *ptr)
{
  uint16_t val;
  memcpy(&val, ptr, 2);
#if !defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  return val;
}




static __inline void wr64_le(uint8_t *ptr, uint64_t val)
{
#if defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  memcpy(ptr, &val, 8);
}


static __inline void wr32_le(uint8_t *ptr, uint32_t val)
{
#if defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  memcpy(ptr, &val, 4);
}


static __inline void wr16_le(uint8_t *ptr, uint16_t val)
{
#if defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  memcpy(ptr, &val, 2);
}



static __inline uint64_t rd64_le(const uint8_t *ptr)
{
  uint64_t val;
  memcpy(&val, ptr, 8);
#if defined(__BIG_ENDIAN__)
  val = __builtin_bswap64(val);
#endif
  return val;
}


static __inline uint32_t rd32_le(const uint8_t *ptr)
{
  uint32_t val;
  memcpy(&val, ptr, 4);
#if defined(__BIG_ENDIAN__)
  val = __builtin_bswap32(val);
#endif
  return val;
}



static __inline uint16_t rd16_le(const uint8_t *ptr)
{
  uint16_t val;
  memcpy(&val, ptr, 2);
#if defined(__BIG_ENDIAN__)
  val = ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
#endif
  return val;
}
