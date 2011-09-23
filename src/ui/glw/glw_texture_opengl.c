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

#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <libswscale/swscale.h>

#include "glw.h"
#include "glw_texture.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_root_t *gr, 
				      glw_loadable_texture_t *glt)
{
  if(glt->glt_texture.tex != 0) {
    glDeleteTextures(1, &glt->glt_texture.tex);
    glt->glt_texture.tex = 0;
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

  if(glt->glt_texture.tex != 0)
    return;

  p = glt->glt_bitmap;

  glGenTextures(1, &glt->glt_texture.tex);
  glBindTexture(m, glt->glt_texture.tex);


  switch(glt->glt_format) {
  case GL_RGB:
    glt->glt_texture.type = GLW_TEXTURE_TYPE_NO_ALPHA;
    break;

  default:
    glt->glt_texture.type = GLW_TEXTURE_TYPE_NORMAL;
    break;
  }

  glt->glt_texture.width  = glt->glt_xs;
  glt->glt_texture.height = glt->glt_ys;

  glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

  int wrapmode = glt->glt_flags & GLW_TEX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(m, GL_TEXTURE_WRAP_S, wrapmode);
  glTexParameteri(m, GL_TEXTURE_WRAP_T, wrapmode);

  if(glt->glt_tex_width && glt->glt_tex_height) {

    glTexImage2D(m, 0, glt->glt_format, glt->glt_tex_width, glt->glt_tex_height,
		 0, glt->glt_ext_format, glt->glt_ext_type, NULL);

    glTexSubImage2D(m, 0, 0, 0, 
		    glt->glt_xs, glt->glt_ys, 
		    glt->glt_ext_format, glt->glt_ext_type,
		    p);

    glt->glt_s = (float)glt->glt_xs / (float)glt->glt_tex_width;
    glt->glt_t = (float)glt->glt_ys / (float)glt->glt_tex_height;

  } else {
    glt->glt_s = 1;
    glt->glt_t = 1;

    glTexImage2D(m, 0, glt->glt_format, 
		 glt->glt_xs, glt->glt_ys,
		 0, glt->glt_ext_format,
		 glt->glt_ext_type, p);
  }


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

static void texture_load_direct(AVPicture *pict, glw_loadable_texture_t *glt,
				int bpp);

static int texture_load_rescale_swscale(const AVPicture *pict, int pix_fmt, 
					int src_w, int src_h,
					int dst_w, int dst_h,
					glw_loadable_texture_t *glt);

int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVPicture *pict, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w0, int req_h0)
{
  int r, x, y, i;
  int need_format_conv = 0;
  int want_rescale = 0; // Want rescaling cause it looks better
  int must_rescale = 0; // Must rescale cause we cant display it otherwise
  uint32_t *palette, *u32p;
  uint8_t *map;
  int bpp = 0;

  switch(pix_fmt) {
  default:
    need_format_conv = 1;
    break;

  case PIX_FMT_RGB24:    
    bpp = 3;
    glt->glt_format = GL_RGB;
    glt->glt_ext_format = GL_RGB;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;

  case PIX_FMT_BGRA:
    bpp = 4;
    glt->glt_format = GL_RGBA;
    glt->glt_ext_format = GL_BGRA;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;

  case PIX_FMT_RGBA:
    bpp = 4;
    glt->glt_format = GL_RGBA;
    glt->glt_ext_format = GL_RGBA;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;
    
  case PIX_FMT_Y400A:
    bpp = 2;
    glt->glt_format = GL_LUMINANCE_ALPHA;
    glt->glt_ext_format = GL_LUMINANCE_ALPHA;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;
    break;

  case PIX_FMT_GRAY8:
    bpp = 1;
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
			     src_w, src_h, req_w0, req_h0);

    av_free(pict2.data[0]);
    return r;
  }

  int req_w = req_w0, req_h = req_h0;

  if(!glw_can_tnpo2(gr)) {
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
    if(!texture_load_rescale_swscale(pict, pix_fmt, src_w, src_h,
				     req_w, req_h, glt))
      return 0;
    
    if(need_format_conv) {
      return texture_load_rescale_swscale(pict, pix_fmt, src_w, src_h,
					  src_w, src_h, glt);
    }

    if(must_rescale) {
      glt->glt_tex_width  = 1 << (av_log2(src_w - 1) + 1);
      glt->glt_tex_height = 1 << (av_log2(src_h - 1) + 1);
    }
  }
  
  glt->glt_xs = src_w;
  glt->glt_ys = src_h;

  texture_load_direct(pict, glt, bpp);
  return 0;
}

/**
 * Direct upload (no rescale or format conversion needed since the source
 * format matches one the OpenGL supports by itself)
 */
static void
texture_load_direct(AVPicture *pict, glw_loadable_texture_t *glt, int bpp)
{
  uint8_t *src, *dst;
  int w = glt->glt_xs;
  int h = glt->glt_ys;

  assert(bpp > 0);

  glt->glt_bitmap_size = bpp * glt->glt_xs * glt->glt_ys;

  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  src = pict->data[0];
  dst = glt->glt_bitmap;
  w *= bpp;

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
 * Rescaling with FFmpeg's swscaler
 */
static int
texture_load_rescale_swscale(const AVPicture *pict, int src_pix_fmt, 
			     int src_w, int src_h,
			     int dst_w, int dst_h,
			     glw_loadable_texture_t *glt)
{
  AVPicture pic;
  int dst_pix_fmt;
  struct SwsContext *sws;
  const uint8_t *ptr[4];
  int strides[4];
  int bpp;


  switch(src_pix_fmt) {
  case PIX_FMT_Y400A:
  case PIX_FMT_BGRA:
  case PIX_FMT_RGBA:
    dst_pix_fmt = PIX_FMT_RGBA;
    break;
  default:
    dst_pix_fmt = PIX_FMT_RGB24;
    break;
  }

  sws = sws_getContext(src_w, src_h, src_pix_fmt, 
		       dst_w, dst_h, dst_pix_fmt,
		       SWS_LANCZOS, NULL, NULL, NULL);
  if(sws == NULL)
    return -1;

  glt->glt_xs = dst_w;
  glt->glt_ys = dst_h;

  ptr[0] = pict->data[0];
  ptr[1] = pict->data[1];
  ptr[2] = pict->data[2];
  ptr[3] = pict->data[3];

  strides[0] = pict->linesize[0];
  strides[1] = pict->linesize[1];
  strides[2] = pict->linesize[2];
  strides[3] = pict->linesize[3];

  switch(src_pix_fmt) {
  case PIX_FMT_Y400A:
  case PIX_FMT_BGRA:
  case PIX_FMT_RGBA:
    bpp = 4;
    glt->glt_format = GL_RGBA;
    glt->glt_ext_format = GL_RGBA;
    break;

  default:
    bpp = 3;
    glt->glt_format = GL_RGB;
    glt->glt_ext_format = GL_RGB;
    break;
  }

  glt->glt_ext_type = GL_UNSIGNED_BYTE;


  glt->glt_bitmap_size = bpp * glt->glt_xs * glt->glt_ys;
  
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
  pic.data[0] = glt->glt_bitmap;
  pic.linesize[0] = dst_w * bpp;
  
  sws_scale(sws, ptr, strides, 0, src_h,
	    pic.data, pic.linesize);
  
  sws_freeContext(sws);
  return 0;
}

/**
 *
 */
void
glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex, 
	       const void *src, int fmt, int width, int height, int flags)
{
  int format;
  int m = gr->gr_be.gbr_primary_texture_mode;

  if(tex->tex == 0) {
    glGenTextures(1, &tex->tex);
    glBindTexture(m, tex->tex);
    glTexParameteri(m, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(m, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    int m2 = flags & GLW_TEX_REPEAT ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(m, GL_TEXTURE_WRAP_S, m2);
    glTexParameteri(m, GL_TEXTURE_WRAP_T, m2);
  } else {
    glBindTexture(m, tex->tex);
  }
  
  switch(fmt) {
  case GLW_TEXTURE_FORMAT_BGR32:
    format     = GL_RGBA;
    tex->type  = GLW_TEXTURE_TYPE_NORMAL;
    break;

  case GLW_TEXTURE_FORMAT_RGB:
    format     = GL_RGB;
    tex->type  = GLW_TEXTURE_TYPE_NO_ALPHA;
    break;

  case GLW_TEXTURE_FORMAT_I8A8:
    format     = GL_LUMINANCE_ALPHA;
    tex->type  = GLW_TEXTURE_TYPE_NORMAL;
    break;

  default:
    return;
  }

  tex->width = width;
  tex->height = height;

  glTexImage2D(m, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, src);
}


/**
 *
 */
void
glw_tex_destroy(glw_root_t *gr, glw_backend_texture_t *tex)
{
  if(tex->tex != 0) {
    glDeleteTextures(1, &tex->tex);
    tex->tex = 0;
  }
}
