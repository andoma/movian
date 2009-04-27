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
#include "glw_image.h"
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
glw_tex_backend_layout(glw_loadable_texture_t *glt)
{
  void *p;
  
  if(glt->glt_texture != 0)
    return;

  p = glt->glt_bitmap;

  glGenTextures(1, &glt->glt_texture);
  glBindTexture(GL_TEXTURE_2D, glt->glt_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    
  glTexImage2D(GL_TEXTURE_2D, 0, glt->glt_format, 
	       glt->glt_xs, glt->glt_ys,
	       0, glt->glt_ext_format,
	       glt->glt_ext_type, p);
    
  glBindTexture(GL_TEXTURE_2D, 0);
    
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

static int maxwidth = 2048;
static int maxheight = 1024;

static void texture_load_direct(AVFrame *frame, glw_loadable_texture_t *glt);

static void texture_load_rescale(AVFrame *frame, int src_w, int src_h,
				 glw_loadable_texture_t *glt);

static void texture_load_rescale_swscale(AVFrame *frame, int pix_fmt, 
					 int src_w, int src_h,
					 glw_loadable_texture_t *glt);

int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVFrame *frame, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w, int req_h)
{
  int r, x, y, i;
  AVFrame *frame2;
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

  case PIX_FMT_RGB32:
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

    map = frame->data[1];
    for(i = 0; i < 4*256; i+=4) {
      if(map[i + 3] == 0) {
	map[i + 0] = 0;
	map[i + 1] = 0;
	map[i + 2] = 0;
      }
    }

    map = frame->data[0];
    palette = (uint32_t *)frame->data[1];

    frame2 = avcodec_alloc_frame();
    frame2->data[0] = av_malloc(src_w * src_h * 4);
    frame2->linesize[0] = src_w * 4;

    u32p = (void *)frame2->data[0];

    for(y = 0; y < src_h; y++) {
      for(x = 0; x < src_w; x++) {
	*u32p++ = palette[map[x]];
      }
      map += frame->linesize[0];
    }

    r = glw_tex_backend_load(gr, glt, frame2, PIX_FMT_RGB32, 
			     src_w, src_h, req_w, req_h);

    av_free(frame2);
    return r;
  }

  /* Enforce a maximum size to limit texture transfer size */

  if(req_w > maxwidth) {
    req_h = req_h * maxwidth / req_w;
    req_w = maxwidth;
  }

  if(req_h > maxheight) {
    req_w = req_w * maxheight / req_h;
    req_h = maxheight;
  }

  glt->glt_aspect = (float)req_w / (float)req_h;

  if(!(gr->gr_be.gbr_sysfeatures & GLW_OPENGL_TNPO2)) {
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
    texture_load_rescale(frame, src_w, src_h, glt);
  } else if(need_rescale || need_format_conv) {
    texture_load_rescale_swscale(frame, pix_fmt, src_w, src_h, glt);
  } else {
    texture_load_direct(frame, glt);
  }
  return 0;
}

/**
 * Direct upload (no rescale or format conversion needed since the source
 * format matches one the OpenGL supports by itself)
 */
static void
texture_load_direct(AVFrame *frame, glw_loadable_texture_t *glt)
{
  uint8_t *src, *dst;
  int w = glt->glt_xs;
  int h = glt->glt_ys;

  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;

  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  src = frame->data[0];
  dst = glt->glt_bitmap;
  w *= glt->glt_bpp;

  if(frame->linesize[0] != w) {
    while(h--) {
      memcpy(dst, src, w);
      src += frame->linesize[0];
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
texture_load_rescale(AVFrame *frame, int src_w, int src_h,
		     glw_loadable_texture_t *glt)
{
  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size,
			 PROT_READ | PROT_WRITE, 
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  glw_bitmap_rescale(frame->data[0], src_w, src_h, frame->linesize[0],
		     glt->glt_bitmap, glt->glt_xs, glt->glt_ys, 
		     glt->glt_xs * glt->glt_bpp, glt->glt_bpp);
}


/**
 * Rescaling with FFmpeg's swscaler
 */
static void
texture_load_rescale_swscale(AVFrame *frame, int pix_fmt, int src_w, int src_h,
			     glw_loadable_texture_t *glt)
{
  AVPicture pic;
  struct SwsContext *sws;
  int w = glt->glt_xs;
  int h = glt->glt_ys;

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
  
  sws_scale(sws, frame->data, frame->linesize, 0, src_h,
	    pic.data, pic.linesize);
  
  sws_freeContext(sws);
}

/**
 *
 */
void
glw_tex_upload(glw_backend_texture_t *tex, const void *src, int fmt,
	       int width, int height)
{
  int format;
  int ext_format;
  int ext_type;

  if(*tex == 0) {
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_2D, *tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
  } else {
    glBindTexture(GL_TEXTURE_2D, *tex);
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

  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
	       ext_format, ext_type, src);
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
