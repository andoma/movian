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



static const struct {
  uint16_t bit_rate;
  uint16_t frm_size[3];
} ac3_ratesize_tbl[64] =  {
  { 32  ,{64   ,69   ,96   } },
  { 32  ,{64   ,70   ,96   } },
  { 40  ,{80   ,87   ,120  } },
  { 40  ,{80   ,88   ,120  } },
  { 48  ,{96   ,104  ,144  } },
  { 48  ,{96   ,105  ,144  } },
  { 56  ,{112  ,121  ,168  } },
  { 56  ,{112  ,122  ,168  } },
  { 64  ,{128  ,139  ,192  } },
  { 64  ,{128  ,140  ,192  } },
  { 80  ,{160  ,174  ,240  } },
  { 80  ,{160  ,175  ,240  } },
  { 96  ,{192  ,208  ,288  } },
  { 96  ,{192  ,209  ,288  } },
  { 112 ,{224  ,243  ,336  } },
  { 112 ,{224  ,244  ,336  } },
  { 128 ,{256  ,278  ,384  } },
  { 128 ,{256  ,279  ,384  } },
  { 160 ,{320  ,348  ,480  } },
  { 160 ,{320  ,349  ,480  } },
  { 192 ,{384  ,417  ,576  } },
  { 192 ,{384  ,418  ,576  } },
  { 224 ,{448  ,487  ,672  } },
  { 224 ,{448  ,488  ,672  } },
  { 256 ,{512  ,557  ,768  } },
  { 256 ,{512  ,558  ,768  } },
  { 320 ,{640  ,696  ,960  } },
  { 320 ,{640  ,697  ,960  } },
  { 384 ,{768  ,835  ,1152 } },
  { 384 ,{768  ,836  ,1152 } },
  { 448 ,{896  ,975  ,1344 } },
  { 448 ,{896  ,976  ,1344 } },
  { 512 ,{1024 ,1114 ,1536 } },
  { 512 ,{1024 ,1115 ,1536 } },
  { 576 ,{1152 ,1253 ,1728 } },
  { 576 ,{1152 ,1254 ,1728 } },
  { 640 ,{1280 ,1393 ,1920 } },
  { 640 ,{1280 ,1394 ,1920 } }
};



/*
 * AC3 IEC958 encapsulation 
 */

int
iec958_build_ac3frame(uint8_t *src, uint8_t *dst)
{
  int fscod, framesizecod, rate, framesize;

  fscod = (src[4] >> 6) & 0x3;
  
  switch(fscod) {
  case 0:
    rate = 48000;
    break;
  case 1:
    rate = 44100;
    break;
  case 2:
    rate = 32000;
    break;
  default:
    return 0;
  }

  framesizecod = src[4] & 0x3f;
  framesize    = ac3_ratesize_tbl[framesizecod].frm_size[fscod];

  dst[0] = 0x72;  dst[1] = 0xf8;  dst[2] = 0x1f;  dst[3] = 0x4e;
  dst[4] = 0x01; /* AC-3 */
  dst[5] = src[5] & 7;
  framesize *= 2;
  swab(src, &dst[8], framesize);
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
      *dst++ = 0x0b; /* DTS-1 */
      break;
    case 1024:
      *dst++ = 0x0c; /* DTS-2 */
      break;
    case 2048:
      *dst++ = 0x0d; /* DTS-3 */
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
