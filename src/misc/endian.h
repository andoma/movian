/*
 *  Showtime Mediacenter
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


static inline uint16_t
myswap16(uint16_t val)
{
  return ((val >> 8) & 0xff) | ((val << 8) & 0xff00);
}

#if defined(__BIG_ENDIAN__)

#define htole_64(v) __builtin_bswap64(v)
#define htole_32(v) __builtin_bswap32(v)
#define htole_16(v) myswap16(v)
#define letoh_16(v) myswap16(v)
#define letoh_32(v) __builtin_bswap32(v)
#define letoh_64(v) __builtin_bswap64(v)

#define htobe_64(v) (v)
#define htobe_32(v) (v)
#define htobe_16(v) (v)
#define betoh_16(v) (v)
#define betoh_32(v) (v)
#define betoh_64(v) (v)

#elif defined(__LITTLE_ENDIAN__) || (__BYTE_ORDER == __LITTLE_ENDIAN)

#define htole_64(v) (v)
#define htole_32(v) (v)
#define htole_16(v) (v)
#define letoh_16(v) (v)
#define letoh_32(v) (v)
#define letoh_64(v) (v)

#define htobe_64(v) __builtin_bswap64(v)
#define htobe_32(v) __builtin_bswap32(v)
#define htobe_16(v) myswap16(v)
#define betoh_16(v) myswap16(v)
#define betoh_32(v) __builtin_bswap32(v)
#define betoh_64(v) __builtin_bswap64(v)

#else

#error Dont know endian

#endif

