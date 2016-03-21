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
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


#include "main.h"
#include "arch/atomic.h"
#include "pixmap.h"
#include "misc/minmax.h"
#include "image/jpeg.h"
#include "backend/backend.h"


#define DIV255(x) (((((x)+255)>>8)+(x))>>8)

/**
 *
 */
int
color_is_not_gray(uint32_t rgb)
{
  uint8_t r = rgb;
  uint8_t g = rgb >> 8;
  uint8_t b = rgb >> 16;
  return (r != g) || (g != b);
}




/**
 *
 */
pixmap_t *
pixmap_dup(pixmap_t *pm)
{
  atomic_inc(&pm->pm_refcount);
  return pm;
}


/**
 *
 */
pixmap_t *
pixmap_create(int width, int height, pixmap_type_t type, int margin)
{
  int bpp = bytes_per_pixel(type);
  const int rowalign = PIXMAP_ROW_ALIGN - 1;

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  atomic_set(&pm->pm_refcount, 1);
  pm->pm_width = width + margin*2;
  pm->pm_height = height + margin*2;
  pm->pm_linesize = ((pm->pm_width * bpp) + rowalign) & ~rowalign;
  pm->pm_type = type;
  pm->pm_margin = margin;

  if(pm->pm_linesize > 0) {
    /* swscale can write a bit after the buffer in its optimized algo
       therefore we need to allocate a bit extra 
    */
    pm->pm_data = mymemalign(PIXMAP_ROW_ALIGN,
                             pm->pm_linesize * pm->pm_height + 8);
    if(pm->pm_data == NULL) {
      free(pm);
      return NULL;
    }
    memset(pm->pm_data, 0, pm->pm_linesize * pm->pm_height);
  }

  pm->pm_aspect = (float)width / (float)height;
  return pm;
}


/**
 *
 */
void
pixmap_release(pixmap_t *pm)
{
  if(atomic_dec(&pm->pm_refcount))
    return;

  free(pm->pm_data);
  free(pm);
}


/**
 *
 */
static void
horizontal_gradient_rgb24(pixmap_t *pm, const int *top, const int *bottom)
{
  int y;
  const int h = pm->pm_height - pm->pm_margin * 2;
  const int w = pm->pm_width - pm->pm_margin * 2;
  unsigned int X=123456789, Y=362436069, Z=521288629, T;

  for(y = 0; y < h; y++) {
    uint8_t *d = pm_pixel(pm, 0, y);

    int r = 255 * top[0] + (255 * (bottom[0] - top[0]) * y / h);
    int g = 255 * top[1] + (255 * (bottom[1] - top[1]) * y / h);
    int b = 255 * top[2] + (255 * (bottom[2] - top[2]) * y / h);
    int x;
    for(x = 0; x < w; x++) {
      // Marsaglia's xorshf generator
      X ^= X << 16;
      X ^= X >> 5;
      X ^= X << 1;
      
      T = X;
      X = Y;
      Y = Z;
      Z = T ^ X ^ Y;
      *d++ = (r + (Z & 0xff)) >> 8;
      *d++ = (g + (Z & 0xff)) >> 8;
      *d++ = (b + (Z & 0xff)) >> 8;
    }
  }
}



/**
 *
 */
static void
horizontal_gradient_bgr32(pixmap_t *pm, const int *top, const int *bottom)
{
  int y;
  const int h = pm->pm_height - pm->pm_margin * 2;
  const int w = pm->pm_width - pm->pm_margin * 2;

  unsigned int X=123456789, Y=362436069, Z=521288629, T;

  for(y = 0; y < h; y++) {
    uint32_t *d = pm_pixel(pm, 0, y);

    int r = 255 * top[0] + (255 * (bottom[0] - top[0]) * y / h);
    int g = 255 * top[1] + (255 * (bottom[1] - top[1]) * y / h);
    int b = 255 * top[2] + (255 * (bottom[2] - top[2]) * y / h);
    int x;
    for(x = 0; x < w; x++) {
      // Marsaglia's xorshf generator
      X ^= X << 16;
      X ^= X >> 5;
      X ^= X << 1;
      
      T = X;
      X = Y;
      Y = Z;
      Z = T ^ X ^ Y;
      uint8_t R = (r + (Z & 0xff)) >> 8;
      uint8_t G = (g + (Z & 0xff)) >> 8;
      uint8_t B = (b + (Z & 0xff)) >> 8;
      *d++ = 0xff000000 | B << 16 | G << 8 | R; 
    }
  }
}



/**
 *
 */
void
pixmap_horizontal_gradient(pixmap_t *pm, const int *top, const int *bottom)
{
  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    horizontal_gradient_rgb24(pm, top, bottom);
    break;
  case PIXMAP_BGR32:
    horizontal_gradient_bgr32(pm, top, bottom);
    break;
  default:
    break;
  }
}


/**
 *
 */
static pixmap_t *
rgb24_to_bgr32(pixmap_t *src)
{
  pixmap_t *dst = pixmap_create(src->pm_width, src->pm_height, PIXMAP_BGR32,
				src->pm_margin);
  int y;
  
  for(y = 0; y < src->pm_height; y++) {
    const uint8_t *s = src->pm_data + y * src->pm_linesize;
    uint32_t *d = (uint32_t *)(dst->pm_data + y * dst->pm_linesize);
    int x;
    for(x = 0; x < src->pm_width; x++) {
      *d++ = 0xff000000 | s[2] << 16 | s[1] << 8 | s[0]; 
      s+= 3;
    }
  }
  return dst;
}



/**
 *
 */
pixmap_t *
pixmap_rounded_corners(pixmap_t *pm, int r, int which)
{
  pixmap_t *tmp;
  switch(pm->pm_type) {

  default:
    return pm;

  case PIXMAP_BGR32:
    break;

  case PIXMAP_RGB24:
    tmp = rgb24_to_bgr32(pm);
    pixmap_release(pm);
    pm = tmp;
    break;
  }


  r = MIN(pm->pm_height / 2, r);

  int r2 = r * r;
  int i;
  uint32_t *dst;
  for(i = 0; i < r; i++) {
    float x = r - sqrtf(r2 - i*i);
    int len = x;
    int alpha = 255 - (x - len) * 255;
    int y = r - i - 1;

    dst = pm_pixel(pm, 0, y);

    if(which & PIXMAP_CORNER_TOPLEFT) {
      memset(dst, 0, len * sizeof(uint32_t));
      dst[len] = (dst[len] & 0x00ffffff) | alpha << 24;
    }

    if(which & PIXMAP_CORNER_TOPRIGHT) {
      dst += pm->pm_width - pm->pm_margin * 2;
      memset(dst - len, 0, len * sizeof(uint32_t));
      dst[-len-1] = (dst[-len-1] & 0x00ffffff) | alpha << 24;
    }


    dst = pm_pixel(pm, 0, pm->pm_height - 1 - y - pm->pm_margin*2);

    if(which & PIXMAP_CORNER_BOTTOMLEFT) {
      memset(dst, 0, len * sizeof(uint32_t));
      dst[len] = (dst[len] & 0x00ffffff) | alpha << 24;
    }

    if(which & PIXMAP_CORNER_BOTTOMRIGHT) {
      dst += pm->pm_width - pm->pm_margin * 2;
      memset(dst - len, 0, len * sizeof(uint32_t));
      dst[-len-1] = (dst[-len-1] & 0x00ffffff) | alpha << 24;
    }
  }
  return pm;
}


#define FIXMUL(a, b) (((a) * (b) + 255) >> 8)
#define FIX3MUL(a, b, c) (((a) * (b) * (c) + 65535) >> 16)



static void
composite_GRAY8_on_IA(uint8_t *dst, const uint8_t *src,
			 int i0, int foo_, int bar_, int a0,
			 int width)
{
  int i, a, pa, y;
  int x;
  for(x = 0; x < width; x++) {

    if(*src) {
      i = dst[0];
      a = dst[1];

      pa = a;
      y = FIXMUL(a0, *src);
      a = y + FIXMUL(a, 255 - y);

      if(a) {
	i = ((FIXMUL(i0, y) + FIX3MUL(i, pa, (255 - y))) * 255) / a;
      } else {
	i = 0;
      }
      dst[0] = i;
      dst[1] = a;
    }
    src++;
    dst += 2;
  }
}



static void
composite_GRAY8_on_IA_full_alpha(uint8_t *dst, const uint8_t *src,
				    int i0, int b0_, int g0_, int a0_,
				    int width)
{
  int i, a, pa, y;
  int x;
  for(x = 0; x < width; x++) {

    if(*src == 255) {
      dst[0] = i0;
      dst[1] = 255;
    } else if(*src) {
      i = dst[0];
      a = dst[1];

      pa = a;
      y = *src;
      a = y + FIXMUL(a, 255 - y);

      if(a) {
	i = ((FIXMUL(i0, y) + FIX3MUL(i, pa, (255 - y))) * 255) / a;
      } else {
	i = 0;
      }
      dst[0] = i;
      dst[1] = a;
    }
    src++;
    dst += 2;
  }
}



#if 0

static void
composite_GRAY8_on_BGR32(uint8_t *dst_, const uint8_t *src,
			 int r0, int g0, int b0, int a0,
			 int width)
{
  int x;
  uint32_t *dst = (uint32_t *)dst_;
  int a, r, g, b, pa, y;
  uint32_t u32;

  for(x = 0; x < width; x++) {
    
    u32 = *dst;

    r = u32 & 0xff;
    g = (u32 >> 8) & 0xff;
    b = (u32 >> 16) & 0xff;
    a = (u32 >> 24) & 0xff;

    pa = a;
    y = FIXMUL(a0, *src);
    a = y + FIXMUL(a, 255 - y);

    if(a) {
      r = ((FIXMUL(r0, y) + FIX3MUL(r, pa, (255 - y))) * 255) / a;
      g = ((FIXMUL(g0, y) + FIX3MUL(g, pa, (255 - y))) * 255) / a;
      b = ((FIXMUL(b0, y) + FIX3MUL(b, pa, (255 - y))) * 255) / a;
    } else {
      r = g = b = 0;
    }
    u32 = a << 24 | b << 16 | g << 8 | r;
    *dst = u32;
    src++;
    dst++;
  }
}


#else



static void
composite_GRAY8_on_BGR32(uint8_t *dst_, const uint8_t *src,
			 int CR, int CG, int CB, int CA,
			 int width)
{
  int x;
  uint32_t *dst = (uint32_t *)dst_;
  uint32_t u32;

  for(x = 0; x < width; x++) {

    int SA = DIV255(*src * CA);
    int SR = CR;
    int SG = CG;
    int SB = CB;

    u32 = *dst;
    
    int DR =  u32        & 0xff;
    int DG = (u32 >> 8)  & 0xff;
    int DB = (u32 >> 16) & 0xff;
    int DA = (u32 >> 24) & 0xff;

    int FA = SA + DIV255((255 - SA) * DA);

    if(FA == 0) {
      SA = 0;
      u32 = 0;
    } else {
      if(FA != 255)
	SA = SA * 255 / FA;

      DA = 255 - SA;
      
      DB = DIV255(SB * SA + DB * DA);
      DG = DIV255(SG * SA + DG * DA);
      DR = DIV255(SR * SA + DR * DA);
      
      u32 = FA << 24 | DB << 16 | DG << 8 | DR;
    }
    *dst = u32;

    src++;
    dst++;
  }
}
#endif


/**
 *
 */
void
pixmap_composite(pixmap_t *dst, const pixmap_t *src,
		 int xdisp, int ydisp, int rgba)
{
  int y, wy;
  uint8_t *d0;
  const uint8_t *s0;
  void (*fn)(uint8_t *dst, const uint8_t *src,
	     int red, int green, int blue, int alpha,
	     int width);

  int readstep = 0;
  int writestep = 0;
  uint8_t r = rgba;
  uint8_t g = rgba >> 8;
  uint8_t b = rgba >> 16;
  uint8_t a = rgba >> 24;

  if(src->pm_type == PIXMAP_I && dst->pm_type == PIXMAP_IA && 
     a == 255)
    fn = composite_GRAY8_on_IA_full_alpha;
  else if(src->pm_type == PIXMAP_I && dst->pm_type == PIXMAP_IA)
    fn = composite_GRAY8_on_IA;
  else if(src->pm_type == PIXMAP_I && dst->pm_type == PIXMAP_BGR32)
    fn = composite_GRAY8_on_BGR32;
  else
    return;
  
  readstep  = bytes_per_pixel(src->pm_type);
  writestep = bytes_per_pixel(dst->pm_type);

  s0 = src->pm_data;
  d0 = dst->pm_data;

  int xx = src->pm_width;

  if(xdisp < 0) {
    // Painting left of dst image
    s0 += readstep * -xdisp;
    xx += xdisp;
    xdisp = 0;
	
  } else if(xdisp > 0) {
    d0 += writestep * xdisp;
  }
      
  if(xx + xdisp > dst->pm_width) {
    xx = dst->pm_width - xdisp;
  }
  

  for(y = 0; y < src->pm_height; y++) {
    wy = y + ydisp;
    if(wy >= 0 && wy < dst->pm_height)
      fn(d0 + wy * dst->pm_linesize, s0 + y * src->pm_linesize, r, g, b, a, xx);
  }
}



static void
box_blur_line_2chan(uint8_t *d, const uint32_t *a, const uint32_t *b,
		    int width, int boxw, int m)
{
  int x;
  unsigned int v;
  for(x = 0; x < boxw; x++) {
    const int x1 = 2 * MIN(x + boxw, width - 1);
    const int x2 = 0;

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
  }

  for(; x < width - boxw; x++) {
    const int x1 = 2 * (x + boxw);
    const int x2 = 2 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
  }

  for(; x < width; x++) {
    const int x1 = 2 * (width - 1);
    const int x2 = 2 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
  }
}



static void
box_blur_line_4chan(uint8_t *d, const uint32_t *a, const uint32_t *b,
		    int width, int boxw, int m)
{
  int x;
  unsigned int v;
  for(x = 0; x < boxw; x++) {
    const int x1 = 4 * MIN(x + boxw, width - 1);
    const int x2 = 0;

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
    v = b[x1 + 2] + a[x2 + 2] - b[x2 + 2] - a[x1 + 2];
    *d++ = (v * m) >> 16;
    v = b[x1 + 3] + a[x2 + 3] - b[x2 + 3] - a[x1 + 3];
    *d++ = (v * m) >> 16;
  }

  for(; x < width - boxw; x++) {
    const int x1 = 4 * (x + boxw);
    const int x2 = 4 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
    v = b[x1 + 2] + a[x2 + 2] - b[x2 + 2] - a[x1 + 2];
    *d++ = (v * m) >> 16;
    v = b[x1 + 3] + a[x2 + 3] - b[x2 + 3] - a[x1 + 3];
    *d++ = (v * m) >> 16;
  }

  for(; x < width; x++) {
    const int x1 = 4 * (width - 1);
    const int x2 = 4 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    *d++ = (v * m) >> 16;
    v = b[x1 + 1] + a[x2 + 1] - b[x2 + 1] - a[x1 + 1];
    *d++ = (v * m) >> 16;
    v = b[x1 + 2] + a[x2 + 2] - b[x2 + 2] - a[x1 + 2];
    *d++ = (v * m) >> 16;
    v = b[x1 + 3] + a[x2 + 3] - b[x2 + 3] - a[x1 + 3];
    *d++ = (v * m) >> 16;
  }
}


/**
 *
 */
void
pixmap_box_blur(pixmap_t *pm, int boxw, int boxh)
{
  unsigned int *tmp, *t;
  int x, y, i;
  const uint8_t *s;
  const int w = pm->pm_width;
  const int h = pm->pm_height;
  const int ls = pm->pm_linesize;
  const int z = bytes_per_pixel(pm->pm_type);

  boxw = MIN(boxw, w);

  void (*fn)(uint8_t *dst, const uint32_t *a, const uint32_t *b, int width,
	     int boxw, int m);

  switch(z) {
  case 2:
    fn = box_blur_line_2chan;
    break;
  case 4:
    fn = box_blur_line_4chan;
    break;


  default:
    return;
  }



  tmp = mymalloc(ls * h * sizeof(unsigned int));
  if(tmp == NULL)
    return;

  s = pm->pm_data;
  t = tmp;

  for(i = 0; i < z; i++)
    *t++ = *s++;

  for(x = 0; x < (w-1)*z; x++) {
    t[0] = *s++ + t[-z];
    t++;
  }

  for(y = 1; y < h; y++) {

    s = pm->pm_data + y * ls;
    t = tmp + y * ls;

    for(i = 0; i < z; i++) {
      t[0] = *s++ + t[-ls];
      t++;
    }

    for(x = 0; x < (w-1)*z; x++) {
      t[0] = *s++ + t[-z] + t[-ls] - t[-ls - z];
      t++;
    }
  }

  int m = 65536 / ((boxw * 2 + 1) * (boxh * 2 + 1));



  for(y = 0; y < h; y++) {
    uint8_t *d = pm->pm_data + y * ls;

    const unsigned int *a = tmp + ls * MAX(0, y - boxh);
    const unsigned int *b = tmp + ls * MIN(h - 1, y + boxh);
    fn(d, a, b, w, boxw, m);
  }

  free(tmp);
}



/**
 *
 */
static uint32_t
mix_bgr32(uint32_t src, uint32_t dst)
{
  int SR =  src        & 0xff;
  int SG = (src >> 8)  & 0xff;
  int SB = (src >> 16) & 0xff;
  int SA = (src >> 24) & 0xff;
  
  int DR =  dst        & 0xff;
  int DG = (dst >> 8)  & 0xff;
  int DB = (dst >> 16) & 0xff;
  int DA = (dst >> 24) & 0xff;
  
  int FA = SA + DIV255((255 - SA) * DA);

  if(FA == 0) {
    dst = 0;
  } else {
    if(FA != 255)
      SA = SA * 255 / FA;
    
    DA = 255 - SA;
    
    DB = DIV255(SB * SA + DB * DA);
    DG = DIV255(SG * SA + DG * DA);
    DR = DIV255(SR * SA + DR * DA);
    
    dst = FA << 24 | DB << 16 | DG << 8 | DR;
  }
  return dst;
}



/**
 *
 */
static void
drop_shadow_rgba(uint8_t *D, const uint32_t *a, const uint32_t *b,
                 int width, int boxw, int m)
{
  uint32_t *d = (uint32_t *)D;

  int x;
  unsigned int v;
  int s;
  for(x = 0; x < boxw; x++) {
    const int x1 = MIN(x + boxw, width - 1);
    const int x2 = 0;

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;

    *d = mix_bgr32(*d, s << 24);
    d++;
  }

  for(; x < width - boxw; x++) {
    const int x1 = (x + boxw);
    const int x2 = (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;
    *d = mix_bgr32(*d, s << 24);
    d++;
  }

  for(; x < width; x++) {
    const int x1 = (width - 1);
    const int x2 = (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;
    *d = mix_bgr32(*d, s << 24);
    d++;
  }
}


/**
 *
 */
static void
mix_ia(uint8_t *src, uint8_t *dst, int DR, int DA)
{
  int SR = src[0];
  int SA = src[1];
  
  //  int DR = dst[0];
  //  int DA = dst[1];
  
  int FA = SA + DIV255((255 - SA) * DA);

  if(FA == 0) {
    dst[0] = 0;
    dst[1] = 0;
  } else {
    if(FA != 255)
      SA = SA * 255 / FA;
    
    DA = 255 - SA;
    
    DR = DIV255(SR * SA + DR * DA);
    dst[0] = DR;
    dst[1] = FA;
  }
}

/**
 *
 */
static void
drop_shadow_ia(uint8_t *d, const uint32_t *a, const uint32_t *b,
               int width, int boxw, int m)
{

  int x;
  unsigned int v;
  int s;
  for(x = 0; x < boxw; x++) {
    const int x1 = 2 * MIN(x + boxw, width - 1);
    const int x2 = 0;

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;

    mix_ia(d, d, 0, s);
    d+=2;
  }

  for(; x < width - boxw; x++) {
    const int x1 = 2 * (x + boxw);
    const int x2 = 2 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;
    mix_ia(d, d, 0, s);
    d+=2;
  }

  for(; x < width; x++) {
    const int x1 = 2 * (width - 1);
    const int x2 = 2 * (x - boxw);

    v = b[x1 + 0] + a[x2 + 0] - b[x2 + 0] - a[x1 + 0];
    s = (v * m) >> 16;
    mix_ia(d, d, 0, s);
    d+=2;
  }
}


/**
 *
 */
void
pixmap_drop_shadow(pixmap_t *pm, int boxw, int boxh)
{
  const uint8_t *s;
  unsigned int *tmp, *t;
  int ach;   // Alpha channel
  int z;
  int w = pm->pm_width;
  int h = pm->pm_height;
  int ls = pm->pm_linesize;
  int x, y;

  assert(boxw > 0);
  assert(boxh > 0);

  boxw = MIN(boxw, w);

  void (*fn)(uint8_t *dst, const uint32_t *a, const uint32_t *b, int width,
	     int boxw, int m);

  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    ach = 3;
    z = 4;
    fn = drop_shadow_rgba;
    break;

  case PIXMAP_IA:
    ach = 1;
    z = 2;
    fn = drop_shadow_ia;
    break;

  default:
    return;
  }

  tmp = mymalloc(pm->pm_width * pm->pm_height * sizeof(unsigned int));
  if(tmp == NULL)
    return;

  s = pm->pm_data + ach;
  t = tmp;

  for(y = 0; y < boxh; y++)
    for(x = 0; x < w; x++)
      *t++ = 0;
    
  for(; y < h; y++) {

    s = pm->pm_data + (y - boxh) * ls + ach;
    for(x = 0; x < boxw; x++)
      *t++ = 0;
    
    for(; x < w; x++) {
      t[0] = *s + t[-1] + t[-w] - t[-w - 1];
      s += z;
      t++;
    }
  }
  
  int m = 65536 / ((boxw * 2 + 1) * (boxh * 2 + 1));

  for(y = 0; y < h; y++) {
    uint8_t *d = pm->pm_data + y * ls;

    const unsigned int *a = tmp + pm->pm_width * MAX(0, y - boxh);
    const unsigned int *b = tmp + pm->pm_width * MIN(h - 1, y + boxh);
    fn(d, a, b, w, boxw, m);
  }
  free(tmp);
}

#if 0
/**
 *
 */
void
pixmap_intensity_analysis(pixmap_t *pm)
{
  int sum = 0;

  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    for(int y = 0; y < pm->pm_height; y++) {
      const uint8_t *src = pm_pixel(pm, 0, y);
      int linesum = 0;
      for(int x = 0; x < pm->pm_width; x++) {
        unsigned int v = src[0] + src[1] + src[2];
        linesum += v;
        src+=3;
      }
      sum += linesum / (pm->pm_width * 3);
    }
    break;

  case PIXMAP_BGR32:
    for(int y = 0; y < pm->pm_height; y++) {
      const uint32_t *src = pm_pixel(pm, 0, y);
      int linesum = 0;
      for(int x = 0; x < pm->pm_width; x++) {
        unsigned int u32 = *src++;
        unsigned int r = u32 & 0xff;
        unsigned int g = (u32 >> 8) & 0xff;
        unsigned int b = (u32 >> 16) & 0xff;
        unsigned int v = r + g + b;
        linesum += v;
      }
      sum += linesum / (pm->pm_width * 3);
    }
    break;

  default:
    printf("Cant do intensity analysis for pixfmt %d\n", pm->pm_type);
  }
  sum /= pm->pm_height;
  pm->pm_intensity = pow(sum / 256.0f, 1 / 3.0f);
}
#endif


/**
 *
 */
void
pixmap_intensity_analysis(pixmap_t *pm)
{
  int bin[256] = {0};

  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    for(int y = 0; y < pm->pm_height; y++) {
      const uint8_t *src = pm_pixel(pm, 0, y);
      for(int x = 0; x < pm->pm_width; x++) {
        unsigned int v = (src[0] + src[1] + src[2]);
        bin[v / 3]++;
      }
    }
    break;

  case PIXMAP_BGR32:
    for(int y = 0; y < pm->pm_height; y++) {
      const uint32_t *src = pm_pixel(pm, 0, y);
      for(int x = 0; x < pm->pm_width; x++) {
        unsigned int u32 = *src++;
        unsigned int r = u32 & 0xff;
        unsigned int g = (u32 >> 8) & 0xff;
        unsigned int b = (u32 >> 16) & 0xff;
        unsigned int v = r + g + b;
        bin[v / 3]++;
      }
    }
    break;

  default:
    printf("Cant do intensity analysis for pixfmt %d\n", pm->pm_type);
  }

  int pixels = pm->pm_width * pm->pm_height;
  int limit = pixels * 0.95;
  int i;
  for(i = 255; i >= 0; i--) {
    pixels -= bin[i];
    if(pixels < limit)
      break;
  }
  pm->pm_intensity = i / 255.0f;
}






// gcc -O3 src/misc/pixmap.c -o /tmp/pixmap -Isrc -DLOCAL_MAIN

#ifdef LOCAL_MAIN

static int64_t
get_ts(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (int64_t)tv.tv_sec * 1000000LL + tv.tv_usec;
}

int
main(int argc, char **argv)
{
  pixmap_t *dst = pixmap_create(2048, 2048, PIXMAP_BGR32);
  pixmap_t *src = pixmap_create(2048, 2048, PIXMAP_GRAY8);

  memset(src->pm_pixels, 0xff, src->pm_linesize * src->pm_height);

  int64_t a = get_ts();
  pixmap_composite(dst, src, 0, 0, 0xffffffff);
  printf("Compositing in %dµs\n",(int)( get_ts() - a));
  printf("dst pixel(0,0) = 0x%x\n", dst->pm_pixels[0]);
  return 0;
}

#endif


/**
 *
 */
static image_t *
be_pixmap_loader(const char *url, const image_meta_t *im,
                 struct fa_resolver *far, char *errbuf, size_t errlen,
                 int *cache_control, cancellable_t *c)
{
  image_t *img;
  int w = im->im_req_width, h = im->im_req_height;
  const char *s;
  if((s = mystrbegins(url, "pixmap:gradient:")) != NULL) {
    if(w == -1)
      w = 128;
    if(h == -1)
      h = 128;
    int t[4] = {0,0,0,255};
    int b[4] = {0,0,0,255};
    if(sscanf(s, "%d,%d,%d:%d,%d,%d",
	      &t[0], &t[1], &t[2], &b[0], &b[1], &b[2]) != 6) {
      snprintf(errbuf, errlen, "Invalid RGB codes");
      return NULL;
    }

    pixmap_t *pm = pixmap_create(w, h, PIXMAP_BGR32, im->im_margin);
    pixmap_horizontal_gradient(pm, t, b);
    img = image_create_from_pixmap(pm);
    pixmap_release(pm);
    img->im_flags |= IMAGE_ADAPTED;

  } else {
    snprintf(errbuf, errlen, "Invalid URL");
    return NULL;
  }
  return img;
}


/**
 *
 */
static int
be_pixmap_canhandle(const char *url)
{
  if(!strncmp(url, "pixmap:", strlen("pixmap:")))
    return 1;
  return 0;
}

/**
 *
 */
static backend_t be_pixmap = {
  .be_canhandle   = be_pixmap_canhandle,
  .be_imageloader = be_pixmap_loader,
};

BE_REGISTER(pixmap);
