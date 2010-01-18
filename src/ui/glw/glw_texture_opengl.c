/*
 *  GL Widgets, Texture loader
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

#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <libswscale/swscale.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_scaler.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_loadable_texture_t *glt)
{
  if(glt->glt_texture != 0) {
    glDeleteTextures(1, &glt->glt_texture);
    glt->glt_texture = 0;
  }
}


/**
 * Free resources created by glw_tex_backend_decode()
 */
void
glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt)
{
  if(glt->glt_bitmap != NULL) {
    munmap(glt->glt_bitmap, glt->glt_bitmap_size);
    glt->glt_bitmap = NULL;
  }
}


/**
 * Invoked on every frame when status == VALID
 */
void
glw_tex_backend_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  void *p;
  int m = gr->gr_be.gbr_primary_texture_mode;

  if(glt->glt_texture != 0)
    return;

  p = glt->glt_bitmap;

  glGenTextures(1, &glt->glt_texture);
  glBindTexture(m, glt->glt_texture);
  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
  glTexImage2D(m, 0, glt->glt_format, 
	       glt->glt_xs, glt->glt_ys,
	       0, glt->glt_ext_format,
	       glt->glt_ext_type, p);
    
  glBindTexture(m, 0);
    
  if(glt->glt_bitmap != NULL) {
    munmap(glt->glt_bitmap, glt->glt_bitmap_size);
    glt->glt_bitmap = NULL;
  }
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

static void texture_load_direct(AVPicture *pict, glw_loadable_texture_t *glt);

static void texture_load_rescale(AVPicture *pict, int src_w, int src_h,
				 glw_loadable_texture_t *glt);

static void texture_load_rescale_swscale(const AVPicture *pict, int pix_fmt, 
					 int src_w, int src_h,
					 glw_loadable_texture_t *glt);

int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVPicture *pict, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w, int req_h)
{
  int r, x, y, i;
  int need_format_conv = 0;
  int need_rescale;
  uint32_t *palette, *u32p;
  uint8_t *map;
  
  switch(pix_fmt) {
  default:
    need_format_conv = 1;
    break;

  case PIX_FMT_RGB24:    
    glt->glt_bpp = 3;
    glt->glt_format = GL_RGB;
    glt->glt_ext_format = GL_RGB;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;

  case PIX_FMT_BGRA:
    glt->glt_bpp = 4;
    glt->glt_format = GL_RGBA;
    glt->glt_ext_format = GL_BGRA;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;
    
  case PIX_FMT_GRAY8:
    glt->glt_bpp = 1;
    glt->glt_format = GL_LUMINANCE;
    glt->glt_ext_format = GL_LUMINANCE;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
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

    r = glw_tex_backend_load(gr, glt, &pict2, PIX_FMT_BGRA, 
			     src_w, src_h, req_w, req_h);

    av_free(pict2.data[0]);
    return r;
  }

  if(!glw_can_tnpo2(gr)) {
    /* We lack non-power-of-two texture support, check if we must rescale.
     * Since the bitmap aspect is already calculated, it will automatically 
     * compensate the rescaling when we render the texture.
     */
    
    if(1 << av_log2(req_w) != req_w)
      req_w = make_powerof2(req_w);

    if(1 << av_log2(req_h) != req_h)
      req_h = make_powerof2(req_h);
  }

  need_rescale = req_w != src_w || req_h != src_h;

  glt->glt_xs = req_w;
  glt->glt_ys = req_h;

  if(need_rescale && !need_format_conv) {
    texture_load_rescale(pict, src_w, src_h, glt);
  } else if(need_rescale || need_format_conv) {
    texture_load_rescale_swscale(pict, pix_fmt, src_w, src_h, glt);
  } else {
    texture_load_direct(pict, glt);
  }
  return 0;
}

/**
 * Direct upload (no rescale or format conversion needed since the source
 * format matches one the OpenGL supports by itself)
 */
static void
texture_load_direct(AVPicture *pict, glw_loadable_texture_t *glt)
{
  uint8_t *src, *dst;
  int w = glt->glt_xs;
  int h = glt->glt_ys;

  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;

  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  src = pict->data[0];
  dst = glt->glt_bitmap;
  w *= glt->glt_bpp;

  if(pict->linesize[0] != w) {
    while(h--) {
      memcpy(dst, src, w);
      src += pict->linesize[0];
      dst += w;
    }
  } else {
    /* XXX: Not optimal, we could avoid this copy by stealing memory
       from lavc */
    memcpy(dst, src, h * w);
  }
}

/**
 * Rescaling with internal libglw scaler
 */
static void
texture_load_rescale(AVPicture *pict, int src_w, int src_h,
		     glw_loadable_texture_t *glt)
{
  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size,
			 PROT_READ | PROT_WRITE, 
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  glw_bitmap_rescale(pict->data[0], src_w, src_h, pict->linesize[0],
		     glt->glt_bitmap, glt->glt_xs, glt->glt_ys, 
		     glt->glt_xs * glt->glt_bpp, glt->glt_bpp);
}


/**
 * Rescaling with FFmpeg's swscaler
 */
static void
texture_load_rescale_swscale(const AVPicture *pict, int pix_fmt, 
			     int src_w, int src_h,
			     glw_loadable_texture_t *glt)
{
  AVPicture pic;
  struct SwsContext *sws;
  const uint8_t *ptr[4];
  int strides[4];
  int w = glt->glt_xs;
  int h = glt->glt_ys;

  ptr[0] = pict->data[0];
  ptr[1] = pict->data[1];
  ptr[2] = pict->data[2];
  ptr[3] = pict->data[3];

  strides[0] = pict->linesize[0];
  strides[1] = pict->linesize[1];
  strides[2] = pict->linesize[2];
  strides[3] = pict->linesize[3];

  sws = sws_getContext(src_w, src_h, pix_fmt, 
		       w, h, PIX_FMT_RGB24,
		       SWS_BICUBIC, NULL, NULL, NULL);
  if(sws == NULL)
    return;

  glt->glt_bpp = 3;
  glt->glt_format = GL_RGB;
  glt->glt_ext_format = GL_RGB;
  glt->glt_ext_type = GL_UNSIGNED_BYTE;

  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;
  
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
  pic.data[0] = glt->glt_bitmap;
  pic.linesize[0] = w * glt->glt_bpp;
  
  sws_scale(sws, ptr, strides, 0, src_h,
	    pic.data, pic.linesize);
  
  sws_freeContext(sws);
}

/**
 *
 */
void
glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex, 
	       const void *src, int fmt, int width, int height)
{
  int format;
  int ext_format;
  int ext_type;
  int m = gr->gr_be.gbr_primary_texture_mode;

  if(*tex == 0) {
    glGenTextures(1, tex);
    glBindTexture(m, *tex);
    glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(m, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(m, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
  } else {
    glBindTexture(m, *tex);
  }
  
  switch(fmt) {
  case GLW_TEXTURE_FORMAT_I8A8:
    format     = GL_LUMINANCE4_ALPHA4;
    ext_format = GL_LUMINANCE_ALPHA;
    ext_type   = GL_UNSIGNED_BYTE;
    break;
  case GLW_TEXTURE_FORMAT_I8:
    format     = GL_ALPHA8;
    ext_format = GL_ALPHA;
    ext_type   = GL_UNSIGNED_BYTE;
    break;
  default:
    return;
  }

  glTexImage2D(m, 0, format, width, height, 0, ext_format, ext_type, src);
}


/**
 *
 */
void
glw_tex_destroy(glw_backend_texture_t *tex)
{
  if(*tex != 0) {
    glDeleteTextures(1, tex);
    *tex = 0;
  }
}
