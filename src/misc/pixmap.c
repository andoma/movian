/*
 *  Pixmaps - Helpers for transfer of images between modules
 *  Copyright (C) 2009 Andreas Öman
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
#include <sys/param.h>
#include <arch/atomic.h>
#include "pixmap.h"


/**
 * Maybe use libavutil instead
 */
static int 
bytes_per_pixel(int fmt)
{
  switch(fmt) {
  case PIX_FMT_BGR32:
    return 4;

  case PIX_FMT_RGB24:
    return 3;

  case PIX_FMT_Y400A:
    return 2;
    
  case PIX_FMT_GRAY8:
    return 1;

  default:
    return 0;
  }
}


/**
 *
 */
pixmap_t *
pixmap_dup(pixmap_t *pm)
{
  atomic_add(&pm->pm_refcount, 1);
  return pm;
}


/**
 *
 */
pixmap_t *
pixmap_alloc_coded(const void *data, size_t size, enum CodecID codec)
{
  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_size = size;

  pm->pm_width = -1;
  pm->pm_height = -1;

  pm->pm_data = malloc(size + FF_INPUT_BUFFER_PADDING_SIZE);
  if(data != NULL)
    memcpy(pm->pm_data, data, size);

  memset(pm->pm_data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);

  pm->pm_codec = codec;
  return pm;
}


/**
 *
 */
pixmap_t *
pixmap_create(int width, int height, enum PixelFormat pixfmt, int rowalign)
{
  int bpp = bytes_per_pixel(pixfmt);
  if(bpp == 0 || rowalign < 1)
    return NULL;

  rowalign--;
  

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_linesize = ((pm->pm_width * bpp) + rowalign) & ~rowalign;
  pm->pm_pixfmt = pixfmt;
  pm->pm_codec = CODEC_ID_NONE;
  pm->pm_data = calloc(1, pm->pm_linesize * pm->pm_height);
  return pm;
}



/**
 *
 */
void
pixmap_release(pixmap_t *pm)
{
  if(atomic_add(&pm->pm_refcount, -1) > 1)
    return;
  
  if(pm->pm_codec == CODEC_ID_NONE) {
    free(pm->pm_pixels);
    free(pm->pm_charpos);
  } else {
    free(pm->pm_data);
  }
  free(pm);
}

/**
 *
 */
static pixmap_t *
pixmap_clone(const pixmap_t *src, int clear)
{
  pixmap_t *dst = calloc(1, sizeof(pixmap_t));

  dst->pm_refcount = 1;
  dst->pm_width = src->pm_width;
  dst->pm_linesize = src->pm_linesize;
  dst->pm_height = src->pm_height;
  dst->pm_codec = CODEC_ID_NONE;
  dst->pm_pixfmt = src->pm_pixfmt;

  if(clear)
    dst->pm_pixels = calloc(1, dst->pm_linesize * dst->pm_height);
  else
    dst->pm_pixels = malloc(dst->pm_linesize * dst->pm_height);

  return dst;
}


/**
 *
 */
static int
convolute_pixel_slow(const uint8_t *src, 
		     int x, int y, int xstep, int ystep,
		     int width, int height, const int *kernel)
{
  int v = 0;

  if(y > 0) {
    if(x > 0)
      v += src[-xstep - ystep] * kernel[0];
    v += src[       - ystep] * kernel[1];
    if(x < width - 1)
      v += src[ xstep - ystep] * kernel[2];
  }

  if(x > 0)
    v += src[-xstep] * kernel[3];
  v += src[0] * kernel[4];
  if(x < width - 1)
    v += src[ xstep] * kernel[5];

  if(y < height - 1) {
    if(x > 0)
      v += src[-xstep + ystep] * kernel[6];
    v += src[         ystep] * kernel[7];
    if(x < width - 1)
      v += src[ xstep + ystep] * kernel[8];
  }
  v = MAX(MIN(v, 255), 0);
  return v;
}

#define BLUR_KERNEL        {1,1,1, 1,1,1, 1,1,1}
#define EMBOSS_KERNEL      {-2, -1, 0, -1, 1, 1, 0, 1, 2}
#define EDGE_DETECT_KERNEL {0,1,0,1,-4,1,0,1,0}

static const int kernels[][9] = {
  [PIXMAP_BLUR] = BLUR_KERNEL,
  [PIXMAP_EMBOSS] = EMBOSS_KERNEL,
  [PIXMAP_EDGE_DETECT] = EDGE_DETECT_KERNEL,
};


/**
 *
 */
static inline int
convolute_pixel_fast(const uint8_t *src, int xstep, int ystep,
		     const int *kernel)
{
  int v = 0;

  v += src[-xstep - ystep] * kernel[0];
  v += src[       - ystep] * kernel[1];
  v += src[ xstep - ystep] * kernel[2];
  v += src[-xstep]         * kernel[3];
  v += src[0]              * kernel[4];
  v += src[ xstep]         * kernel[5];
  v += src[-xstep + ystep] * kernel[6];
  v += src[         ystep] * kernel[7];
  v += src[ xstep + ystep] * kernel[8];
  v = MAX(MIN(v, 255), 0);
  return v;
}


static int
convolute_pixel_BLUR(const uint8_t *src, int xstep, int ystep)
{
  return convolute_pixel_fast(src, xstep, ystep, (const int[])BLUR_KERNEL);
}

static int
convolute_pixel_EMBOSS(const uint8_t *src, int xstep, int ystep)
{
  return convolute_pixel_fast(src, xstep, ystep, (const int[])EMBOSS_KERNEL);
}

static int
convolute_pixel_EDGE_DETECT(const uint8_t *src, int xstep, int ystep)
{
  return convolute_pixel_fast(src, xstep, ystep,
			      (const int[])EDGE_DETECT_KERNEL);
}


typedef int (kfn_t)(const uint8_t *src, int xstep, int ystep);

static kfn_t *kernelfuncs[] = {
  [PIXMAP_BLUR] = convolute_pixel_BLUR,
  [PIXMAP_EMBOSS] = convolute_pixel_EMBOSS,
  [PIXMAP_EDGE_DETECT] = convolute_pixel_EDGE_DETECT,
};


/**
 *
 */
static void
convolute_pixels(uint8_t *dst, const uint8_t *src, 
		 int w, int h, int channels, int linesize, const int *k,
		 kfn_t *kfn)
{
  int x, y, c;
  uint8_t *d;
  const uint8_t *s;

  for(y = 0; y < 1; y++) {
    d = dst;
    s = src;

    for(x = 0; x < w; x++)
      for(c = 0; c < channels; c++)
	*d++ = convolute_pixel_slow(s++, x, y, channels, linesize, w, h, k);
      
    dst += linesize;
    src += linesize;
  }
  
  
  for(; y < h - 1; y++) {
    d = dst;
    s = src;

    for(c = 0; c < channels; c++)
      *d++ = convolute_pixel_slow(s++, 0, y, channels, linesize, w, h, k);

    switch(channels) {
    case 1:
      for(x = 1; x < w - 1; x++)
	*d++ = kfn(s++, 1, linesize);
      break;

    case 2:
      for(x = 1; x < w - 1; x++) {
	*d++ = kfn(s++, 2, linesize);
	*d++ = kfn(s++, 2, linesize);
      }
      break;

    case 3:
      for(x = 1; x < w - 1; x++) {
	*d++ = kfn(s++, 3, linesize);
	*d++ = kfn(s++, 3, linesize);
	*d++ = kfn(s++, 3, linesize);
      }
      break;

    case 4:
      for(x = 1; x < w - 1; x++) {
	*d++ = kfn(s++, 4, linesize);
	*d++ = kfn(s++, 4, linesize);
	*d++ = kfn(s++, 4, linesize);
	*d++ = kfn(s++, 4, linesize);
      }
      break;

    }

    for(c = 0; c < channels; c++)
      *d++ = convolute_pixel_slow(s++, x, y, channels, linesize, w, h, k);

    dst += linesize;
    src += linesize;
  }

  for(; y < h; y++) {
    d = dst;
    s = src;

    for(x = 0; x < w; x++)
      for(c = 0; c < channels; c++)
	*d++ = convolute_pixel_slow(s++, x, y, channels, linesize, w, h, k);

    dst += linesize;
    src += linesize;
  }
}


/**
 *
 */
pixmap_t *
pixmap_convolution_filter(const pixmap_t *src, int kernel)
{
  const int *k = kernels[kernel];
  kfn_t *kfn = kernelfuncs[kernel];

  if(src->pm_codec != CODEC_ID_NONE)
    return NULL;




  pixmap_t *dst = pixmap_clone(src, 0);


  switch(src->pm_pixfmt) {
  case PIX_FMT_GRAY8:
    convolute_pixels(dst->pm_pixels, src->pm_pixels,
		     dst->pm_width, dst->pm_height, 1, dst->pm_linesize,
		     k, kfn);
    break;

  case PIX_FMT_Y400A:
    convolute_pixels(dst->pm_pixels, src->pm_pixels,
		     dst->pm_width, dst->pm_height, 2, dst->pm_linesize,
		     k, kfn);
    break;

  default:
    pixmap_release(dst);
    return NULL;
  }
  return dst;
}



/**
 *
 */
static void
multiply_alpha_PIX_FMT_Y400A(uint8_t *dst, const uint8_t *src, 
			     int w, int h, int linesize)
{
  int x, y;
  const uint8_t *s;
  uint8_t *d;

  for(y = 0; y < h; y++) {
    s = src;
    d = dst;
    for(x = 0; x < w; x++) {
      *d++ = s[0] * s[1];
      *d++ = s[1];
      s+= 2;
    }
    dst += linesize;
    src += linesize;
  }
}


/**
 *
 */
pixmap_t *
pixmap_multiply_alpha(const pixmap_t *src)
{
  if(src->pm_codec != CODEC_ID_NONE)
    return NULL;

  pixmap_t *dst = pixmap_clone(src, 0);

  switch(src->pm_pixfmt) {
  case PIX_FMT_Y400A:
    multiply_alpha_PIX_FMT_Y400A(dst->pm_pixels, src->pm_pixels,
				 dst->pm_width, dst->pm_height,
				 dst->pm_linesize);
    break;

  default:
    pixmap_release(dst);
    return NULL;
  }
  return dst;
}


/**
 *
 */
static void
extract_channel(uint8_t *dst, const uint8_t *src, 
		int w, int h, int xstep, int src_linesize, int dst_linesize)
{
  int x, y;
  const uint8_t *s;
  uint8_t *d;

  for(y = 0; y < h; y++) {
    s = src;
    d = dst;
    for(x = 0; x < w; x++) {
      *d++ = *s;
      s += xstep;
    }
    dst += dst_linesize;
    src += src_linesize;
  }
}


/**
 *
 */
pixmap_t *
pixmap_extract_channel(const pixmap_t *src, unsigned int channel)
{
  if(src->pm_codec != CODEC_ID_NONE)
    return NULL;

  pixmap_t *dst = calloc(1, sizeof(pixmap_t));
  dst->pm_refcount = 1;
  dst->pm_linesize = dst->pm_width = src->pm_width;
  dst->pm_height = src->pm_height;
  dst->pm_codec = CODEC_ID_NONE;
  dst->pm_pixfmt = PIX_FMT_GRAY8;

  dst->pm_pixels = malloc(dst->pm_linesize * dst->pm_height);

  switch(src->pm_pixfmt) {
  case PIX_FMT_Y400A:
    channel = MIN(channel, 1);
    extract_channel(dst->pm_pixels, src->pm_pixels + channel,
		    dst->pm_width, dst->pm_height,
		    2, src->pm_linesize, dst->pm_linesize);
    break;

  default:
    pixmap_release(dst);
    return NULL;
  }

  return dst;
}

#define FIXMUL(a, b) (((a) * (b) + 255) >> 8)
#define FIX3MUL(a, b, c) (((a) * (b) * (c) + 65535) >> 16)



static void
composite_GRAY8_on_Y400A(uint8_t *dst, const uint8_t *src,
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

//#define DIV255(x) ((x) / 255)
//#define DIV255(x) ((x) >> 8)
#define DIV255(x) (((((x)+255)>>8)+(x))>>8)


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

  if(src->pm_codec != CODEC_ID_NONE)
    return;

  if(src->pm_pixfmt == PIX_FMT_GRAY8 && dst->pm_pixfmt == PIX_FMT_Y400A)
    fn = composite_GRAY8_on_Y400A;
  else if(src->pm_pixfmt == PIX_FMT_GRAY8 && dst->pm_pixfmt == PIX_FMT_BGR32)
    fn = composite_GRAY8_on_BGR32;
  else
    return;
  
  readstep  = bytes_per_pixel(src->pm_pixfmt);
  writestep = bytes_per_pixel(dst->pm_pixfmt);

  s0 = src->pm_pixels;
  d0 = dst->pm_pixels;

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
  pixmap_t *dst = pixmap_create(2048, 2048, PIX_FMT_BGR32);
  pixmap_t *src = pixmap_create(2048, 2048, PIX_FMT_GRAY8);

  memset(src->pm_pixels, 0xff, src->pm_linesize * src->pm_height);

  int64_t a = get_ts();
  pixmap_composite(dst, src, 0, 0, 0xffffffff);
  printf("Compositing in %dµs\n",(int)( get_ts() - a));
  printf("dst pixel(0,0) = 0x%x\n", dst->pm_pixels[0]);
  return 0;
}

#endif
