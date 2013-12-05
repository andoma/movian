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

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <malloc.h>

#include <libswscale/swscale.h>

#include "glw.h"
#include "glw_texture.h"

#include "showtime.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_loadable_texture_t *glt)
{
  if(glt->glt_texture.mem != NULL) {
    free(glt->glt_texture.mem);
    glt->glt_texture.mem = NULL;
  }
}


/**
 * Free resources created by glw_tex_backend_decode()
 */
void
glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt)
{
}


/**
 * Invoked on every frame when status == VALID
 */
void
glw_tex_backend_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{
}


/**
 * Convert ARGB to GX texture format
 */
void *
gx_convert_argb(const uint8_t *src, int linesize, 
		unsigned int w, unsigned int h)
{
  unsigned int size = ((w + 3) & ~3) * ((h + 3) & ~3) * 4;
  int y, x, i;
  const uint8_t *s;
  uint8_t *dst, *d;

  d = dst = memalign(32, size);

  for(y = 0; y < h; y += 4) {
    for(x = 0; x < w; x += 4) {
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 4;
	*d++ = s[0];  *d++ = s[1];
	*d++ = s[4];  *d++ = s[5];
	*d++ = s[8];  *d++ = s[9];
	*d++ = s[12]; *d++ = s[13];
      }
      
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 4;
	*d++ = s[2];  *d++ = s[3];
	*d++ = s[6];  *d++ = s[7];
	*d++ = s[10]; *d++ = s[11];
	*d++ = s[14]; *d++ = s[15];
      }
    }
  }  

  DCFlushRange(dst, size);
  return dst;
}


/**
 * Convert RGB to GX texture format
 */
static void *
convert_rgb(const uint8_t *src, int linesize, unsigned int w, unsigned int h)
{
  unsigned int size = ((w + 3) & ~3) * ((h + 3) & ~3) * 4;
  int y, x, i;
  const uint8_t *s;
  uint8_t *dst, *d;

  d = dst = memalign(32, size);

  for(y = 0; y < h; y += 4) {
    for(x = 0; x < w; x += 4) {
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 3;
	*d++ = 0xff; *d++ = s[0];
	*d++ = 0xff; *d++ = s[3];
	*d++ = 0xff; *d++ = s[6];
	*d++ = 0xff; *d++ = s[9];
      }
      
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 3;
	*d++ = s[1];  *d++ = s[2];
	*d++ = s[4];  *d++ = s[5];
	*d++ = s[7];  *d++ = s[8];
	*d++ = s[10]; *d++ = s[11];
      }
    }
  }  

  DCFlushRange(dst, size);
  return dst;
}

/**
 * Convert RGBA to GX texture format
 */
static void *
convert_rgba(const uint8_t *src, int linesize, unsigned int w, unsigned int h)
{
  unsigned int size = ((w + 3) & ~3) * ((h + 3) & ~3) * 4;
  int y, x, i;
  const uint8_t *s;
  uint8_t *dst, *d;

  d = dst = memalign(32, size);

  for(y = 0; y < h; y += 4) {
    for(x = 0; x < w; x += 4) {
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 4;
	*d++ = s[0];  *d++ = s[1];
	*d++ = s[4];  *d++ = s[5];
	*d++ = s[8];  *d++ = s[9];
	*d++ = s[12]; *d++ = s[13];
      }
      
      for(i = 0; i < 4; i++) {
	s = src + linesize * (y + i) + x * 4;
	*d++ = s[2];  *d++ = s[3];
	*d++ = s[6];  *d++ = s[7];
	*d++ = s[10]; *d++ = s[11];
	*d++ = s[14]; *d++ = s[15];
      }
    }
  }  

  DCFlushRange(dst, size);
  return dst;
}



/**
 * Convert I8A8 (16 bit) to I4A4 (8 bit) GX texture format. 
 */
static void *
convert_i8a8(const uint8_t *src, int linesize, unsigned int w, unsigned int h)
{
  unsigned int size = ((w + 7) & ~7) * ((h + 3) & ~3);
  int y, x, r;
  const uint8_t *s;
  uint8_t *dst, *d;

  d = dst = memalign(32, size);

  for(y = 0; y < h; y += 4) {
    for(x = 0; x < w; x += 8) {
      for(r = 0; r < 4; r++) {

	s = src + linesize * (y + r) + x * 2;

	*d++ = (s[0x1] & 0xf0) | (s[0x0] >> 4);
	*d++ = (s[0x3] & 0xf0) | (s[0x2] >> 4);
	*d++ = (s[0x5] & 0xf0) | (s[0x4] >> 4);
	*d++ = (s[0x7] & 0xf0) | (s[0x6] >> 4);
	*d++ = (s[0x9] & 0xf0) | (s[0x8] >> 4);
	*d++ = (s[0xb] & 0xf0) | (s[0xa] >> 4);
	*d++ = (s[0xd] & 0xf0) | (s[0xc] >> 4);
	*d++ = (s[0xf] & 0xf0) | (s[0xe] >> 4);
      }
    }
  }
  DCFlushRange(dst, size);
  return dst;
}


/**
 * Convert I8 (8 bit luma) to I4 (4 bit) GX texture format. 
 */
static void *
convert_i8_to_i4(const uint8_t *src, int linesize, 
		 unsigned int w, unsigned int h)
{
  unsigned int size = ((w + 7) & ~7) * ((h + 7) & ~7) / 2;
  int y, x, r;
  const uint8_t *s;
  uint8_t *dst, *d;

  d = dst = memalign(32, size);

  for(y = 0; y < h; y += 8) {
    for(x = 0; x < w; x += 8) {
      for(r = 0; r < 8; r++) {

	s = src + linesize * (y + r) + x;

	*d++ = (s[0x0] & 0xf0) | (s[0x1] >> 4);
	*d++ = (s[0x2] & 0xf0) | (s[0x3] >> 4);
	*d++ = (s[0x4] & 0xf0) | (s[0x5] >> 4);
	*d++ = (s[0x6] & 0xf0) | (s[0x7] >> 4);
      }
    }
  }
  DCFlushRange(dst, size);
  return dst;
}


/**
 *
 */
static int
convert_with_swscale(glw_loadable_texture_t *glt, AVPicture *pict, 
		     int src_pix_fmt, 
		     int src_w, int src_h, int dst_w, int dst_h)
{
  int dst_pix_fmt;
  struct SwsContext *sws;
  AVPicture dst;
  uint8_t *texels;

  const uint8_t *ptr[4];
  int strides[4];
  int bpp;

  if(src_pix_fmt == PIX_FMT_BGRA) {
    dst_pix_fmt = PIX_FMT_RGBA;
    bpp = 4;
  } else {
    dst_pix_fmt = PIX_FMT_RGB24;
    bpp = 3;
  }

  ptr[0] = pict->data[0];
  ptr[1] = pict->data[1];
  ptr[2] = pict->data[2];
  ptr[3] = pict->data[3];

  strides[0] = pict->linesize[0];
  strides[1] = pict->linesize[1];
  strides[2] = pict->linesize[2];
  strides[3] = pict->linesize[3];

  sws = sws_getContext(src_w, src_h, src_pix_fmt, 
		       dst_w, dst_h, dst_pix_fmt,
		       SWS_LANCZOS, NULL, NULL, NULL);
  if(sws == NULL)
    return 1;

  memset(&dst, 0, sizeof(dst));
  dst.data[0] = malloc(dst_w * dst_h * bpp);
  dst.linesize[0] = bpp * dst_w;
  sws_scale(sws, ptr, strides, 0, src_h, dst.data, dst.linesize);  

  if(bpp == 4)
    texels = convert_rgba(dst.data[0], dst.linesize[0], dst_w, dst_h);
  else
    texels = convert_rgb(dst.data[0], dst.linesize[0], dst_w, dst_h);

  glt->glt_xs = dst_w;
  glt->glt_ys = dst_h;

  glt->glt_texture.mem = texels;
  
  GX_InitTexObj(&glt->glt_texture.obj, texels, dst_w, dst_h,
		GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);

  free(dst.data[0]);
  sws_freeContext(sws);
  return 0;
}



/**
 *
 */
int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVPicture *pict, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w, int req_h)
{
  uint8_t *texels = NULL;
  int fmt = 0;
  int need_rescale = req_w != src_w || req_h != src_h;

  switch(pix_fmt) {

  case PIX_FMT_ARGB:
    if(need_rescale)
      break;
    texels = gx_convert_argb(pict->data[0], pict->linesize[0], src_w, src_h);
    fmt = GX_TF_RGBA8;
    break;

  case PIX_FMT_RGB24:
    if(need_rescale)
      break;
    texels = convert_rgb(pict->data[0], pict->linesize[0], src_w, src_h);
    fmt = GX_TF_RGBA8;
    break;

  case PIX_FMT_Y400A:
    texels = convert_i8a8(pict->data[0], pict->linesize[0], src_w, src_h);
    fmt = GX_TF_IA4;
    break;
  }

  if(texels == NULL)
    return convert_with_swscale(glt, pict, pix_fmt, src_w, src_h, req_w, req_h);

  glt->glt_xs = src_w;
  glt->glt_ys = src_h;

  glt->glt_texture.mem = texels;
  
  int wrapmode = glt->glt_flags & GLW_TEX_REPEAT ? GX_REPEAT : GX_CLAMP;

  GX_InitTexObj(&glt->glt_texture.obj, texels, src_w, src_h, 
		fmt, wrapmode, wrapmode, GX_FALSE);
  return 0;
}



/**
 *
 */
void
glw_tex_upload(const glw_root_t *gr, glw_backend_texture_t *tex, 
	       const void *src, int fmt, int width, int height, int flags)
{
  int format;
  uint8_t *texels;

  if(tex->mem != NULL)
    free(tex->mem);
  
  switch(fmt) {
  case GLW_TEXTURE_FORMAT_I8A8:
    format = GX_TF_IA4;
    texels = convert_i8a8(src, width * 2, width, height);
    break;

  case GLW_TEXTURE_FORMAT_I8:
    format = GX_TF_I4;
    texels = convert_i8_to_i4(src, width, width, height);
    break;

  case GLW_TEXTURE_FORMAT_RGBA:
    format = GX_TF_RGBA8;
    texels = convert_rgba(src, width * 4, width, height);
    break;

  default:
    tex->mem = NULL;
    return;
  }

  tex->mem = texels;

  int wrapmode = flags & GLW_TEX_REPEAT ? GX_REPEAT : GX_CLAMP;

  GX_InitTexObj(&tex->obj, texels, width, height,
		format, wrapmode, wrapmode, GX_FALSE);
}


/**
 *
 */
void
glw_tex_destroy(glw_backend_texture_t *tex)
{
  if(tex->mem != NULL) {
    free(tex->mem);
    tex->mem = NULL;
  }
}
