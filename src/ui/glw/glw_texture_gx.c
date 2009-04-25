/*
 *  GL Widgets, Texture loader
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

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <malloc.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_scaler.h"

#include "showtime.h"
#include "fileaccess/fa_imageloader.h"

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
glw_tex_backend_layout(glw_loadable_texture_t *glt)
{
}


/**
 * Convert ARGB to GX texture format
 */
static void *
convert_argb(const uint8_t *src, int linesize, unsigned int w, unsigned int h)
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
 *
 */
int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVFrame *frame, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w, int req_h)
{
  uint8_t *texels;
  int fmt;

  switch(pix_fmt) {

  case PIX_FMT_ARGB:
    texels = convert_argb(frame->data[0], frame->linesize[0], req_w, req_h);
    fmt = GX_TF_RGBA8;
    break;

  case PIX_FMT_RGB24:
    texels = convert_rgb(frame->data[0], frame->linesize[0], req_w, req_h);
    fmt = GX_TF_RGBA8;
    break;

  default:
    TRACE(TRACE_ERROR, "glw", "GX unsupported pixel format %s",
	  avcodec_get_pix_fmt_name(pix_fmt));
    return 1;
  }

  glt->glt_aspect = (float)req_w / (float)req_h;
  glt->glt_xs = req_w;
  glt->glt_ys = req_h;

  glt->glt_texture.mem = texels;
  
  GX_InitTexObj(&glt->glt_texture.obj, texels, req_w, req_h, 
		fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
  return 0;
}



/**
 *
 */
void
glw_tex_upload(glw_backend_texture_t *tex, const void *src, int fmt,
	       int width, int height)
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

  default:
    tex->mem = NULL;
    return;
  }

  tex->mem = texels;
  GX_InitTexObj(&tex->obj, texels, width, height,
		format, GX_CLAMP, GX_CLAMP, GX_FALSE);
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
