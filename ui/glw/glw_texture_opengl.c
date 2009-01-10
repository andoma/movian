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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "glw.h"
#include "glw_image.h"
#include "glw_scaler.h"

#include "showtime.h"
#include "fileaccess/fa_imageloader.h"

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
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
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


typedef void *SwsFilter;

extern struct SwsContext *sws_getContext(int srcW, int srcH, int srcFormat,
					 int dstW, int dstH, int dstFormat,
					 int flags,
					 SwsFilter *srcFilter,
					 SwsFilter *dstFilter,
					 double *param);
#define SWS_BICUBIC           4

int sws_scale(struct SwsContext *context, uint8_t* src[], int srcStride[],
	      int srcSliceY,
              int srcSliceH, uint8_t* dst[], int dstStride[]);

void sws_freeContext(struct SwsContext *swsContext);

static void texture_load_direct(AVFrame *frame, glw_loadable_texture_t *glt);

static void texture_load_rescale(AVCodecContext *ctx, AVFrame *frame,
				 glw_loadable_texture_t *glt);

static void texture_load_rescale_swscale(AVCodecContext *ctx, AVFrame *frame,
					 glw_loadable_texture_t *glt, int pix_fmt);

int
glw_tex_backend_decode(glw_root_t *gr, glw_loadable_texture_t *glt)
{
  int r, w, h, got_pic, pix_fmt, x, y, i;
  AVCodecContext *ctx;
  AVCodec *codec;
  AVFrame *frame, *frame_A, *frame_B = NULL;
  fa_image_load_ctrl_t ctrl;
  int need_format_conv = 0;
  int need_rescale;
  uint32_t *palette, *u32p;
  uint8_t *map;

  if(glt->glt_filename == NULL)
    return -1;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.url = glt->glt_filename;
  
  if(fa_imageloader(&ctrl))
    return -1;

  fflock();

  ctx = avcodec_alloc_context();
  codec = avcodec_find_decoder(ctrl.codecid);
  
  if(avcodec_open(ctx, codec) < 0) {
    avcodec_close(ctx);
    ffunlock();
    av_free(ctx);
    free(ctrl.data);
    return -1;
  }
  
  ffunlock();

  frame_A = avcodec_alloc_frame();

  r = avcodec_decode_video(ctx, frame_A, &got_pic, ctrl.data, ctrl.datasize);

  free(ctrl.data);

  if(ctrl.want_thumb && ctrl.got_thumb) {
    w = 160;
    h = 160 * ctx->height / ctx->width;
  } else {
    w = ctx->width;
    h = ctx->height;
  }

  pix_fmt = ctx->pix_fmt;
  
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

  case PIX_FMT_RGBA32:
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

    map = frame_A->data[1];
    for(i = 0; i < 4*256; i+=4) {
      if(map[i + 3] == 0) {
	map[i + 0] = 0;
	map[i + 1] = 0;
	map[i + 2] = 0;
      }
    }

    map = frame_A->data[0];
    palette = (uint32_t *)frame_A->data[1];

    frame_B = avcodec_alloc_frame();
    frame_B->data[0] = av_malloc(ctx->width * ctx->height * 4);
    frame_B->linesize[0] = ctx->width * 4;

    u32p = (void *)frame_B->data[0];

    for(y = 0; y < ctx->height; y++) {
      for(x = 0; x < ctx->width; x++) {
	*u32p++ = palette[map[x]];
      }
      map += frame_A->linesize[0];
    }
    
    pix_fmt = PIX_FMT_RGBA32;

    glt->glt_bpp = 4;
    glt->glt_format = GL_RGBA;
    glt->glt_ext_format = GL_BGRA;
    glt->glt_ext_type = GL_UNSIGNED_BYTE;

    break;
  }

  /* Enforce a maximum size to limit texture transfer size */

  if(w > maxwidth) {
    h = h * maxwidth / w;
    w = maxwidth;
  }

  if(h > maxheight) {
    w = w * maxheight / h;
    h = maxheight;
  }

  glt->glt_aspect = (float)w / (float)h;

  if(!(gr->gr_be.gbr_sysfeatures & GLW_OPENGL_TNPO2)) {
    /* We lack non-power-of-two texture support, check if we must rescale.
     * Since the bitmap aspect is already calculated, it will automatically 
     * compensate the rescaling when we render the texture.
     */
    
    if(1 << av_log2(w) != w)
      w = make_powerof2(w);

    if(1 << av_log2(h) != h)
      h = make_powerof2(h);
  }

  need_rescale = w != ctx->width || h != ctx->height;

  glt->glt_xs = w;
  glt->glt_ys = h;

  frame = frame_B ?: frame_A;

  if(need_rescale && !need_format_conv) {
    texture_load_rescale(ctx, frame, glt);
  } else
    if(need_rescale || need_format_conv) {
    texture_load_rescale_swscale(ctx, frame, glt, pix_fmt);

  } else {
    texture_load_direct(frame, glt);
  }

  fflock();
  avcodec_close(ctx);
  ffunlock();
  av_free(ctx);

  av_free(frame_A);
  if(frame_B != NULL) {
    av_free(frame_B->data[0]);
    av_free(frame_B);
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
texture_load_rescale(AVCodecContext *ctx, AVFrame *frame,
		     glw_loadable_texture_t *glt)
{
  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size,
		       PROT_READ | PROT_WRITE, 
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  glw_bitmap_rescale(frame->data[0], ctx->width, ctx->height, 
		     frame->linesize[0],
		     glt->glt_bitmap, glt->glt_xs, glt->glt_ys, 
		     glt->glt_xs * glt->glt_bpp, glt->glt_bpp);
}


/**
 * Rescaling with FFmpeg's swscaler
 */
static void
texture_load_rescale_swscale(AVCodecContext *ctx, AVFrame *frame,
			     glw_loadable_texture_t *glt, int pix_fmt)
{
  AVPicture pic;
  struct SwsContext *sws;
  int w = glt->glt_xs;
  int h = glt->glt_ys;

  sws = sws_getContext(ctx->width, ctx->height, pix_fmt, 
		       w, h, PIX_FMT_RGB24,
		       SWS_BICUBIC, NULL, NULL, NULL);
  glt->glt_bpp = 3;
  glt->glt_format = GL_RGB;
  glt->glt_ext_format = GL_RGB;
  glt->glt_ext_type = GL_UNSIGNED_BYTE;

  glt->glt_bitmap_size = glt->glt_bpp * glt->glt_xs * glt->glt_ys;
  
  glt->glt_bitmap = mmap(NULL, glt->glt_bitmap_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
  pic.data[0] = glt->glt_bitmap;
  pic.linesize[0] = w * glt->glt_bpp;
  
  if(sws_scale(sws, frame->data, frame->linesize, 0, 0,
	       pic.data, pic.linesize) < 0) {
    fprintf(stderr, "%s: scaling failed\n", glt->glt_filename);
  }
  
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
  if(*tex != 0)
    glDeleteTextures(1, tex);
}
