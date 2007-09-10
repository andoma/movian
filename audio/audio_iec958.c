/*
 *  IEC958 conversion code
 *  Copyright (C) 2007 Andreas Öman
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

#define _XOPEN_SOURCE
#include <unistd.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_iec958.h"

/*
 * AC3 IEC958 encapsulation 
 */

int
iec958_build_ac3frame(uint8_t *src, size_t framesize, uint8_t *dst)
{
  dst[0] = 0x72;  dst[1] = 0xf8;  dst[2] = 0x1f;  dst[3] = 0x4e;
  dst[4] = IEC958_PAYLOAD_AC3;
  swab(src, dst + 8, framesize);
  memset(dst + 8 + framesize, 0,  IEC958_AC3_FRAME_SIZE - framesize - 8);
  framesize *= 8;
  dst[6] = framesize;
  dst[7] = framesize >> 8;
  return IEC958_AC3_FRAME_SIZE / 4; /* 2 channels, 16 bit / ch */
}


/*
 * DTS IEC958 encapsulation 
 */


static int 
dts_decode_header(uint8_t *src, int *rate, int *nblks)
{
  int ftype, surp, fsize, amode;
  uint32_t sync;

  sync = (src[0] << 24) | (src[1] << 16) | (src[2] << 8) | src[3];

  if(sync != 0x7ffe8001)
    return -1;

  ftype = src[4] >> 7;
  surp = ((src[4] >> 2) + 1) & 0x1f;
  *nblks = ((src[4] & 0x01) << 6  | (src[5] >> 2)) + 1;
  fsize  = ((src[5] & 0x03) << 12 | (src[6] << 4) | (src[7] >> 4)) + 1;
  amode  = ( src[7] & 0x0f) << 2  | (src[8] >> 6);
  *rate  = ( src[8] & 0x03) << 3  | ((src[9] >> 5) & 0x07);
    
  if(ftype != 1 || fsize > 8192 || fsize < 96)
    return -1;
    
  if(*nblks != 8 && *nblks != 16 && *nblks != 32 && *nblks != 64 &&
     *nblks != 128 && ftype == 1)
    return -1;
  
  return fsize;
}

int
iec958_build_dtsframe(uint8_t *src, size_t srclen, uint8_t *dst)
{
  int nblks, fsize, rate, burst_len, nr_samples;
  uint8_t *dst0 = dst;

  while(srclen > 0) {
    if((fsize = dts_decode_header(src, &rate, &nblks)) < 0)
      return 0;

    burst_len  = fsize * 8;
    nr_samples = nblks * 32;

    *dst++ = 0x72; *dst++ = 0xf8; *dst++ = 0x1f; *dst++ = 0x4e;

    switch(nr_samples) {
    case 512:
      *dst++ = IEC958_PAYLOAD_DTS_1;
      break;
    case 1024:
      *dst++ = IEC958_PAYLOAD_DTS_2;
      break;
    case 2048:
      *dst++ = IEC958_PAYLOAD_DTS_3;
      break;
    default:
      return 0;
    }

    *dst++ = 0;
    *dst++ = burst_len;
    *dst++ = burst_len >> 8;
    
    swab(src, dst, fsize);
    if(fsize & 1)
      dst[fsize] = src[fsize];
    
    memset(dst + fsize, 0, nr_samples * 4 - fsize);
    
    dst    += nr_samples * 4 - 8;
    srclen -= fsize;
    src    += fsize;
  }
  return (dst - dst0) / 4;
}
