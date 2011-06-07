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
#include "showtime.h"


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
pixmap_create(int width, int height, enum PixelFormat pixfmt)
{
  int bpp = bytes_per_pixel(pixfmt);
  if(bpp == 0)
    return NULL;

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_linesize = pm->pm_width * bpp;
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


static void
composite_Y400A_on_Y400A(uint8_t *dst, const uint8_t *src,
			 int red, int green, int blue, int alpha,
			 int width)
{
  int i, a;
  int x;
  for(x = 0; x < width; x++) {
    i = *src++;
    a = *src++;

    i = FIXMUL(red,   i);
    a = FIXMUL(alpha, a);

    dst[0] = FIXMUL(255 - a, dst[0]) + i;
    dst[1] = FIXMUL(255 - a, dst[1]) + a;
    dst += 2;
  }
}



static void
composite_Y400A_on_BGR32(uint8_t *dst, const uint8_t *src,
			 int red, int green, int blue, int alpha,
			 int width)
{
  int r, g, b, a, i;
  int r0, g0, b0, a0;
  int x;
  uint32_t *dst32 = (uint32_t *)dst;
  uint32_t v;

  for(x = 0; x < width; x++) {
    i = *src++;
    a = *src++;

    a = FIXMUL(alpha, a);
    r = FIXMUL(red,   a);
    g = FIXMUL(green, a);
    b = FIXMUL(blue,  a);

    v = *dst32;
    a0 = (v >> 24) & 0xff;
    b0 = (v >> 16) & 0xff;
    g0 = (v >>  8) & 0xff;
    r0 = v & 0xff;

    a0 = FIXMUL(255 - a, a0) + a;
    r0 = FIXMUL(255 - a, r0) + r;
    g0 = FIXMUL(255 - a, g0) + g;
    b0 = FIXMUL(255 - a, b0) + b;

    *dst32++ = a0 << 24 | b0 << 16 | g0 << 8 | r0;
  }
}


static void
composite_GRAY8_on_Y400A(uint8_t *dst, const uint8_t *src,
			 int red, int green, int blue, int alpha,
			 int width)
{
  int i, a;
  int x;
  for(x = 0; x < width; x++) {
    a = *src++;

    i = FIXMUL(red,   a);
    a = FIXMUL(alpha, a);

    dst[0] = FIXMUL(255 - a, dst[0]) + i;
    dst[1] = FIXMUL(255 - a, dst[1]) + a;
    dst += 2;
  }
}



static void
composite_GRAY8_on_BGR32(uint8_t *dst, const uint8_t *src,
			 int red, int green, int blue, int alpha,
			 int width)
{
  int r, g, b, a;
  int r0, g0, b0, a0;
  int x;
  uint32_t *dst32 = (uint32_t *)dst;
  uint32_t v;

  for(x = 0; x < width; x++) {
    a = *src++;

    a = FIXMUL(alpha, a);
    r = FIXMUL(red,   a);
    g = FIXMUL(green, a);
    b = FIXMUL(blue,  a);

    v = *dst32;
    a0 = (v >> 24) & 0xff;
    b0 = (v >> 16) & 0xff;
    g0 = (v >>  8) & 0xff;
    r0 = v & 0xff;

    a0 = FIXMUL(255 - a, a0) + a;
    r0 = FIXMUL(255 - a, r0) + r;
    g0 = FIXMUL(255 - a, g0) + g;
    b0 = FIXMUL(255 - a, b0) + b;

    *dst32++ = a0 << 24 | b0 << 16 | g0 << 8 | r0;
  }
}





/**
 *
 */
void
pixmap_composite(pixmap_t *dst, const pixmap_t *src,
		 int xdisp, int ydisp,
		 int r, int g, int b, int a)
{
  int y, wy;
  uint8_t *d0;
  const uint8_t *s0;
  void (*fn)(uint8_t *dst, const uint8_t *src,
	     int red, int green, int blue, int alpha,
	     int width);

  int readstep = 0;
  int writestep = 0;

  if(src->pm_codec != CODEC_ID_NONE)
    return;

  if(src->pm_pixfmt == PIX_FMT_Y400A && dst->pm_pixfmt == PIX_FMT_Y400A)
    fn = composite_Y400A_on_Y400A;
  else if(src->pm_pixfmt == PIX_FMT_Y400A && dst->pm_pixfmt == PIX_FMT_BGR32)
    fn = composite_Y400A_on_BGR32;
  else if(src->pm_pixfmt == PIX_FMT_GRAY8 && dst->pm_pixfmt == PIX_FMT_Y400A)
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
