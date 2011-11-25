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

#include <stdio.h>
#include <sys/param.h>
#include <string.h>
#include <stdlib.h>

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "showtime.h"
#include "arch/atomic.h"
#include "pixmap.h"


/**
 *
 */
static int 
bytes_per_pixel(pixmap_type_t fmt)
{
  switch(fmt) {
  case PIXMAP_BGR32:
    return 4;

  case PIXMAP_RGB24:
    return 3;

  case PIXMAP_IA:
    return 2;
    
  case PIXMAP_I:
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
pixmap_alloc_coded(const void *data, size_t size, pixmap_type_t type)
{
  int pad = 32;
  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_size = size;

  pm->pm_width = -1;
  pm->pm_height = -1;

  pm->pm_data = malloc(size + pad);
  if(data != NULL)
    memcpy(pm->pm_data, data, size);

  memset(pm->pm_data + size, 0, pad);

  pm->pm_type = type;
  return pm;
}


/**
 *
 */
pixmap_t *
pixmap_create(int width, int height, pixmap_type_t type, int rowalign)
{
  int bpp = bytes_per_pixel(type);
  if(bpp == 0 || rowalign < 1)
    return NULL;

  rowalign--;
  

  pixmap_t *pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_linesize = ((pm->pm_width * bpp) + rowalign) & ~rowalign;
  pm->pm_type = type;
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

  if(!pixmap_is_coded(pm)) {
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
  dst->pm_type = src->pm_type;

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

  if(pixmap_is_coded(src))
    return NULL;

  pixmap_t *dst = pixmap_clone(src, 0);


  switch(src->pm_type) {
  case PIXMAP_I:
    convolute_pixels(dst->pm_pixels, src->pm_pixels,
		     dst->pm_width, dst->pm_height, 1, dst->pm_linesize,
		     k, kfn);
    break;

  case PIXMAP_IA:
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
multiply_alpha_PIX_FMT_IA(uint8_t *dst, const uint8_t *src, 
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
  if(pixmap_is_coded(src))
    return NULL;

  pixmap_t *dst = pixmap_clone(src, 0);

  switch(src->pm_type) {
  case PIXMAP_IA:
    multiply_alpha_PIX_FMT_IA(dst->pm_pixels, src->pm_pixels,
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
  if(pixmap_is_coded(src))
    return NULL;

  pixmap_t *dst = calloc(1, sizeof(pixmap_t));
  dst->pm_refcount = 1;
  dst->pm_linesize = dst->pm_width = src->pm_width;
  dst->pm_height = src->pm_height;
  dst->pm_type = PIXMAP_I;

  dst->pm_pixels = malloc(dst->pm_linesize * dst->pm_height);

  switch(src->pm_type) {
  case PIXMAP_IA:
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




static unsigned int
blur_read(const unsigned int *t, int w, int h, int x, int y, int stride,
	  int channel, int bpp)
{
	if (x<0) x=0; else if (x>=w) x=w-1;
	if (y<0) y=0; else if (y>=h) y=h-1;
	return t[x*bpp+y*stride+channel];
}


/**
 *
 */
void
pixmap_box_blur(pixmap_t *pm, int boxw, int boxh)
{
  unsigned int *tmp, *t;
  int x, y, i, v;
  const uint8_t *s;
  const int w = pm->pm_width;
  const int h = pm->pm_height;
  const int ls = pm->pm_linesize;
  const int z = bytes_per_pixel(pm->pm_type);

  tmp = malloc(ls * h * sizeof(unsigned int));

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
    for(x = 0; x < w; x++) {
      for(i = 0; i < z; i++) {
	v = blur_read(tmp, w, h, x+boxw, y+boxh, ls, i, z)
	  + blur_read(tmp, w, h, x-boxw, y-boxh, ls, i, z)
	  - blur_read(tmp, w, h, x-boxw, y+boxh, ls, i, z)
	  - blur_read(tmp, w, h, x+boxw, y-boxh, ls, i, z);
	
	*d++ = (v * m) >> 16;
      }
    }
  }
  free(tmp);
}




/**
 * Round v to nearest power of two
 */
static int
make_powerof2(int v)
{
  int m;
  m = ((1 << (av_log2(v))) + (1 << (av_log2(v) + 1))) / 2;
  return 1 << (av_log2(v) + (v > m));
}


/**
 * Rescaling with FFmpeg's swscaler
 */
static pixmap_t *
pixmap_rescale_swscale(const AVPicture *pict, int src_pix_fmt, 
		       int src_w, int src_h,
		       int dst_w, int dst_h)
{
  AVPicture pic;
  int dst_pix_fmt;
  struct SwsContext *sws;
  const uint8_t *ptr[4];
  int strides[4];
  pixmap_t *pm;

  switch(src_pix_fmt) {
  case PIX_FMT_Y400A:
  case PIX_FMT_BGRA:
  case PIX_FMT_RGBA:
  case PIX_FMT_ABGR:
  case PIX_FMT_ARGB:
#ifdef __PPC__
    return NULL;
#endif
    dst_pix_fmt = PIX_FMT_BGR32;
    break;

  default:
    dst_pix_fmt = PIX_FMT_RGB24;
    break;
  }

  sws = sws_getContext(src_w, src_h, src_pix_fmt, 
		       dst_w, dst_h, dst_pix_fmt,
		       SWS_LANCZOS, NULL, NULL, NULL);
  if(sws == NULL)
    return NULL;

  ptr[0] = pict->data[0];
  ptr[1] = pict->data[1];
  ptr[2] = pict->data[2];
  ptr[3] = pict->data[3];

  strides[0] = pict->linesize[0];
  strides[1] = pict->linesize[1];
  strides[2] = pict->linesize[2];
  strides[3] = pict->linesize[3];

  int align = 1;
#ifdef __PPC__
  align = 16;
#endif


  switch(dst_pix_fmt) {
  case PIX_FMT_RGB24:
    pm = pixmap_create(dst_w, dst_h, PIXMAP_RGB24, align);
    break;

  default:
    pm = pixmap_create(dst_w, dst_h, PIXMAP_BGR32, align);
    break;
  }

  pic.data[0] = pm->pm_data;
  pic.linesize[0] = pm->pm_linesize;
  
  sws_scale(sws, ptr, strides, 0, src_h, pic.data, pic.linesize);
#if 0  
  if(pm->pm_type == PIXMAP_BGR32) {
    uint32_t *dst = pm->pm_data;
    int i;

    for(i = 0; i < pm->pm_linesize * pm->pm_height; i+= 4) {
      *dst |= 0xff000000;
      dst++;
    }

  }
#endif

  sws_freeContext(sws);
  return pm;
}


static void   __attribute__((unused))

swizzle_xwzy(uint32_t *dst, const uint32_t *src, int len)
{
  int i;
  uint32_t u32;
  for(i = 0; i < len; i++) {
    u32 = *src++;
    *dst++ = (u32 & 0xff00ff00) | (u32 & 0xff) << 16 | (u32 & 0xff0000) >> 16;
  }
}

static pixmap_t *
pixmap_32bit_swizzle(AVPicture *pict, int pix_fmt, int w, int h)
{
#if defined(__BIG_ENDIAN__)
  void (*fn)(uint32_t *dst, const uint32_t *src, int len);
  // go to BGR32 which is ABGR on big endian.
  switch(pix_fmt) {
  case PIX_FMT_ARGB:
    fn = swizzle_xwzy;
    break;
  default:
    return NULL;
  }

  int y;
  pixmap_t *pm = pixmap_create(w, h, PIXMAP_BGR32, 1);

  for(y = 0; y < h; y++) {
    fn((uint32_t *)(pm->pm_data + y * pm->pm_linesize),
       (uint32_t *)(pict->data[0] + y * pict->linesize[0]),
       w);
  }
  return pm;
#else
  return NULL;
#endif
}



/**
 *
 */
static pixmap_t *
pixmap_from_avpic(AVPicture *pict, int pix_fmt, 
		  int src_w, int src_h,
		  int req_w0, int req_h0,
		  const image_meta_t *im)
{
  int x, y, i;
  int need_format_conv = 0;
  int want_rescale = 0; // Want rescaling cause it looks better
  int must_rescale = 0; // Must rescale cause we cant display it otherwise
  uint32_t *palette, *u32p;
  uint8_t *map;
  pixmap_type_t fmt = 0;
  pixmap_t *pm;

  switch(pix_fmt) {
  default:
    need_format_conv = 1;
    break;

  case PIX_FMT_RGB24:
    fmt = PIXMAP_RGB24;
    break;

  case PIX_FMT_BGR32:
    fmt = PIXMAP_BGR32;
    break;
    
  case PIX_FMT_Y400A:
    if(!im->im_can_mono) {
      need_format_conv = 1;
      break;
    }

    fmt = PIXMAP_IA;
    break;

  case PIX_FMT_GRAY8:
    if(!im->im_can_mono) {
      need_format_conv = 1;
      break;
    }

    fmt = PIXMAP_I;
    break;

  case PIX_FMT_PAL8:
    /* FFmpeg can not convert palette alpha values so we need to
       do this ourselfs */
    
    /* It seems that some png implementation leavs the color set even
       if alpha is set to zero. This resluts in ugly aliasing effects
       when scaling image in opengl, so if alpha == 0, clear RGB */

    map = pict->data[1];
    for(i = 0; i < 4*256; i+=4) {
      if(map[i + 3] == 0) {
	map[i + 0] = 0;
	map[i + 1] = 0;
	map[i + 2] = 0;
      }
    }

    map = pict->data[0];
    palette = (uint32_t *)pict->data[1];

    AVPicture pict2;
    
    memset(&pict2, 0, sizeof(pict2));
    
    pict2.data[0] = av_malloc(src_w * src_h * 4);
    pict2.linesize[0] = src_w * 4;

    u32p = (void *)pict2.data[0];

    for(y = 0; y < src_h; y++) {
      for(x = 0; x < src_w; x++) {
	*u32p++ = palette[map[x]];
      }
      map += pict->linesize[0];
    }

    pm = pixmap_from_avpic(&pict2, PIX_FMT_BGRA, 
			   src_w, src_h, req_w0, req_h0, im);

    av_free(pict2.data[0]);
    return pm;
  }

  int req_w = req_w0, req_h = req_h0;

  if(im->im_pot) {
    /* We lack non-power-of-two texture support, check if we must rescale.
     * Since the bitmap aspect is already calculated, it will automatically 
     * compensate the rescaling when we render the texture.
     */
    
    if(1 << av_log2(req_w0) != req_w0)
      req_w = make_powerof2(req_w0);

    if(1 << av_log2(req_h0) != req_h0)
      req_h = make_powerof2(req_h0);

    must_rescale = req_w != src_w || req_h != src_h;
  } else {
    want_rescale = req_w != src_w || req_h != src_h;
  }


  if(must_rescale || want_rescale || need_format_conv) {
    pm = pixmap_rescale_swscale(pict, pix_fmt, src_w, src_h, req_w, req_h);
    
    if(pm != NULL)
      return pm;

    if(need_format_conv) {
      pm = pixmap_rescale_swscale(pict, pix_fmt, src_w, src_h, src_w, src_h);
      if(pm != NULL)
	return pm;

      return pixmap_32bit_swizzle(pict, pix_fmt, src_w, src_h);
    }
  }

  pm = pixmap_create(src_w, src_h, fmt, 1);

  uint8_t *dst = pm->pm_data;
  uint8_t *src = pict->data[0];
  int h = src_h;

  if(pict->linesize[0] != pm->pm_linesize) {
    while(h--) {
      memcpy(dst, src, pm->pm_linesize);
      src += pict->linesize[0];
      dst +=  pm->pm_linesize;
    }
  } else {
    memcpy(dst, src, pm->pm_linesize * src_h);
  }
  return pm;
}


/**
 *
 */
pixmap_t *
pixmap_decode(pixmap_t *pm, const image_meta_t *im,
	      char *errbuf, size_t errlen)
{
  AVCodecContext *ctx;
  AVCodec *codec;
  AVFrame *frame;
  int r, got_pic, w, h;
  int orientation = pm->pm_orientation;

  pm->pm_aspect = (float)pm->pm_width / (float)pm->pm_height;

  if(!pixmap_is_coded(pm))
    return pm;

  switch(pm->pm_type) {
  case PIXMAP_PNG:
    codec = avcodec_find_decoder(CODEC_ID_PNG);
    break;
  case PIXMAP_JPEG:
    codec = avcodec_find_decoder(CODEC_ID_MJPEG);
    break;
  case PIXMAP_GIF:
    codec = avcodec_find_decoder(CODEC_ID_GIF);
    break;
  default:
    codec = NULL;
    break;
  }

  if(codec == NULL) {
    pixmap_release(pm);
    snprintf(errbuf, errlen, "No codec for image format");
    return NULL;
  }
  ctx = avcodec_alloc_context();
  ctx->codec_id   = codec->id;
  ctx->codec_type = codec->type;

  if(avcodec_open(ctx, codec) < 0) {
    av_free(ctx);
    pixmap_release(pm);
    snprintf(errbuf, errlen, "Unable to open codec");
    return NULL;
  }
  
  frame = avcodec_alloc_frame();

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = pm->pm_data;
  avpkt.size = pm->pm_size;

  r = avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);

  if(ctx->width == 0 || ctx->height == 0) {
    av_free(ctx);
    pixmap_release(pm);
    avcodec_close(ctx);
    av_free(frame);
    snprintf(errbuf, errlen, "Invalid picture dimensions");
    return NULL;
  }

  if(im->im_want_thumb && pm->pm_flags & PIXMAP_THUMBNAIL) {
    w = 160;
    h = 160 * ctx->height / ctx->width;
  } else {
    w = ctx->width;
    h = ctx->height;
  }

  if(im->im_req_width != -1 && im->im_req_height != -1) {
    w = im->im_req_width;
    h = im->im_req_height;

  } else if(im->im_req_width != -1) {
    w = im->im_req_width;
    h = im->im_req_width * ctx->height / ctx->width;

  } else if(im->im_req_height != -1) {
    w = im->im_req_height * ctx->width / ctx->height;
    h = im->im_req_height;

  } else if(w > 64 && h > 64) {

    if(im->im_max_width && w > im->im_max_width) {
      h = h * im->im_max_width / w;
      w = im->im_max_width;
    }

    if(im->im_max_height && h > im->im_max_height) {
      w = w * im->im_max_height / h;
      h = im->im_max_height;
    }
  }

  pixmap_release(pm);

  pm = pixmap_from_avpic((AVPicture *)frame, 
			 ctx->pix_fmt, ctx->width, ctx->height, w, h, im);

  if(pm != NULL) {
    pm->pm_orientation = orientation;
    // Compute correct aspect ratio based on orientation
    if(pm->pm_orientation < LAYOUT_ORIENTATION_TRANSPOSE) {
      pm->pm_aspect = (float)w / (float)h;
    } else {
      pm->pm_aspect = (float)h / (float)w;
    }
  }
  av_free(frame);

  avcodec_close(ctx);
  av_free(ctx);
  return pm;
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
