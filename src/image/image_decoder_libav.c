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

#include "main.h"
#include "arch/atomic.h"
#include "misc/minmax.h"
#include "misc/buf.h"
#include "backend/backend.h"

#include "jpeg.h"
#include "pixmap.h"
#include "image.h"
#include "compiler.h"

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
#include <libavutil/common.h>
#include <libavutil/pixdesc.h>



static pixmap_t *pixmap_rescale_swscale(const AVPicture *pict, int src_pix_fmt,
                                        int src_w, int src_h,
                                        int dst_w, int dst_h,
                                        int with_alpha, int margin);

/**
 *
 */
static pixmap_t *
fulhack(const AVPicture *pict, int src_w, int src_h,
        int dst_w, int dst_h, int with_alpha, int margin)
{
  pixmap_t *pm = pixmap_create(src_w, src_h, PIXMAP_RGB24, 0);
  for(int y = 0; y < src_h; y++) {
    uint8_t *dst = pm_pixel(pm, 0,y);
    uint8_t *src0 = pict->data[0] + pict->linesize[0] * y;
    uint8_t *src1 = pict->data[1] + pict->linesize[1] * y;
    uint8_t *src2 = pict->data[2] + pict->linesize[2] * y;

    for(int x = 0; x < src_w; x++) {
      *dst++ = *src0++;
      *dst++ = *src1++;
      *dst++ = *src2++;
    }
  }

  AVPicture pict2 = {};
  pict2.data[0] = pm_pixel(pm, 0, 0);
  pict2.linesize[0] = pm->pm_linesize;
  pixmap_t *pm2 = pixmap_rescale_swscale(&pict2, AV_PIX_FMT_RGB24,
                                         src_w, src_h, dst_w, dst_h,
                                         with_alpha, margin);
  pixmap_release(pm);
  return pm2;

}

/**
 * Rescaling with libswscale
 */
static pixmap_t *
pixmap_rescale_swscale(const AVPicture *pict, int src_pix_fmt, 
		       int src_w, int src_h,
		       int dst_w, int dst_h,
		       int with_alpha, int margin)
{
  AVPicture pic;
  int dst_pix_fmt;
  struct SwsContext *sws;
  const uint8_t *ptr[4];
  int strides[4];
  pixmap_t *pm;
  int outflags = 0;

  const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(src_pix_fmt);
  if(desc && !(desc->flags & AV_PIX_FMT_FLAG_ALPHA))
    outflags |= PIXMAP_OPAQUE;

  switch(src_pix_fmt) {
  case AV_PIX_FMT_Y400A:
  case AV_PIX_FMT_BGRA:
  case AV_PIX_FMT_RGBA:
  case AV_PIX_FMT_ABGR:
  case AV_PIX_FMT_ARGB:
#ifdef __PPC__
    return NULL;
#endif
    dst_pix_fmt = AV_PIX_FMT_BGR32;
    break;

  case AV_PIX_FMT_YUVA444P:
    return fulhack(pict, src_w, src_h, dst_w, dst_h, with_alpha, margin);

  default:
    dst_pix_fmt = AV_PIX_FMT_BGR32;
    break;
  }

  const int swscale_debug = 1;

  if(swscale_debug)
    TRACE(TRACE_DEBUG, "info", "Converting %d x %d [%s] to %d x %d [%s]",
	  src_w, src_h, av_get_pix_fmt_name(src_pix_fmt),
	  dst_w, dst_h, av_get_pix_fmt_name(dst_pix_fmt));
  
  sws = sws_getContext(src_w, src_h, src_pix_fmt, 
		       dst_w, dst_h, dst_pix_fmt,
		       SWS_LANCZOS | 
		       (swscale_debug ? SWS_PRINT_INFO : 0), NULL, NULL, NULL);
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

  switch(dst_pix_fmt) {
  case AV_PIX_FMT_RGB24:
    pm = pixmap_create(dst_w, dst_h, PIXMAP_RGB24, margin);
    break;

  default:
    pm = pixmap_create(dst_w, dst_h, PIXMAP_BGR32, margin);
    break;
  }

  if(pm == NULL) {
    sws_freeContext(sws);
    return NULL;
  }

  // Set scale destination with respect to margin
  pic.data[0] = pm_pixel(pm, 0, 0);
  pic.linesize[0] = pm->pm_linesize;
  pic.linesize[1] = 0;
  pic.linesize[2] = 0;
  pic.linesize[3] = 0;
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
  pm->pm_flags |= outflags;
  return pm;
}


static void attribute_unused
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
pixmap_32bit_swizzle(AVPicture *pict, int pix_fmt, int w, int h, int m)
{
#if defined(__BIG_ENDIAN__)
  void (*fn)(uint32_t *dst, const uint32_t *src, int len);
  // go to BGR32 which is ABGR on big endian.
  switch(pix_fmt) {
  case AV_PIX_FMT_ARGB:
    fn = swizzle_xwzy;
    break;
  default:
    return NULL;
  }

  int y;
  pixmap_t *pm = pixmap_create(w, h, PIXMAP_BGR32, m);
  if(pm == NULL)
    return NULL;

  for(y = 0; y < h; y++) {
    fn(pm_pixel(pm, 0, y),
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
  int i;
  int need_format_conv = 0;
  int want_rescale = 0; // Want rescaling cause it looks better
  uint32_t *palette;
  pixmap_type_t fmt = 0;
  pixmap_t *pm;

  assert(pix_fmt != -1);

  switch(pix_fmt) {
  default:
    need_format_conv = 1;
    break;

  case AV_PIX_FMT_RGB24:
    if(im->im_corner_radius)
      need_format_conv = 1;
    else
      fmt = PIXMAP_RGB24;
    break;

  case AV_PIX_FMT_BGR32:
    fmt = PIXMAP_BGR32;
    break;
    
  case AV_PIX_FMT_Y400A:
    if(!im->im_can_mono) {
      need_format_conv = 1;
      break;
    }

    fmt = PIXMAP_IA;
    break;

  case AV_PIX_FMT_GRAY8:
    if(!im->im_can_mono) {
      need_format_conv = 1;
      break;
    }

    fmt = PIXMAP_I;
    break;

  case AV_PIX_FMT_PAL8:
    palette = (uint32_t *)pict->data[1];

    for(i = 0; i < 256; i++) {
      if((palette[i] >> 24) == 0)
	palette[i] = 0;
    }

    need_format_conv = 1;
    break;
  }

  int req_w = req_w0, req_h = req_h0;

  want_rescale = req_w != src_w || req_h != src_h;

  if(want_rescale || need_format_conv) {
    int want_alpha = im->im_corner_radius;

    pm = pixmap_rescale_swscale(pict, pix_fmt, src_w, src_h, req_w, req_h,
				want_alpha, im->im_margin);
    if(pm != NULL)
      return pm;

    if(need_format_conv) {
      pm = pixmap_rescale_swscale(pict, pix_fmt, src_w, src_h, src_w, src_h,
				  want_alpha, im->im_margin);
      if(pm != NULL)
	return pm;

      return pixmap_32bit_swizzle(pict, pix_fmt, src_w, src_h, im->im_margin);
    }
  }

  pm = pixmap_create(src_w, src_h, fmt, im->im_margin);
  if(pm == NULL)
    return NULL;

  uint8_t *dst = pm_pixel(pm, 0,0);
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
void
pixmap_compute_rescale_dim(const image_meta_t *im,
			   int src_width, int src_height,
			   int *dst_width, int *dst_height)
{
  int w;
  int h;
  if(im->im_want_thumb) {
    w = 160;
    h = 160 * src_height / src_width;
  } else {
    w = src_width;
    h = src_height;
  }

  if(im->im_req_width != -1 && im->im_req_height != -1) {
    w = im->im_req_width;
    h = im->im_req_height;

  } else if(im->im_req_width != -1) {
    w = im->im_req_width;
    h = im->im_req_width * src_height / src_width;

  } else if(im->im_req_height != -1) {
    w = im->im_req_height * src_width / src_height;
    h = im->im_req_height;

  }

  if(w > 64 && h > 64) {

    if(im->im_max_width && w > im->im_max_width) {
      h = h * im->im_max_width / w;
      w = im->im_max_width;
    }

    if(im->im_max_height && h > im->im_max_height) {
      w = w * im->im_max_height / h;
      h = im->im_max_height;
    }
  }
  *dst_width  = w;
  *dst_height = h;
}


/**
 *
 */
pixmap_t *
image_decode_libav(image_coded_type_t type,
                   buf_t *buf, const image_meta_t *im,
                   char *errbuf, size_t errlen)
{
  AVCodecContext *ctx;
  AVCodec *codec;
  AVFrame *frame;
  int got_pic, w, h;
  jpeg_meminfo_t mi;
  jpeginfo_t ji = {0};

  switch(type) {
  case IMAGE_PNG:
    codec = avcodec_find_decoder(AV_CODEC_ID_PNG);
    break;
  case IMAGE_JPEG:

    mi.data = buf_data(buf);
    mi.size = buf_size(buf);

    if(jpeg_info(&ji, jpeginfo_mem_reader, &mi,
		 JPEG_INFO_DIMENSIONS,
                 buf_data(buf), buf_size(buf), errbuf, errlen)) {
      return NULL;
    }
    codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    break;
  case IMAGE_GIF:
    codec = avcodec_find_decoder(AV_CODEC_ID_GIF);
    break;
  case IMAGE_BMP:
    codec = avcodec_find_decoder(AV_CODEC_ID_BMP);
    break;
  default:
    codec = NULL;
    break;
  }

  if(codec == NULL) {
    snprintf(errbuf, errlen, "No codec for image format");
    return NULL;
  }

  ctx = avcodec_alloc_context3(codec);

  if(avcodec_open2(ctx, codec, NULL) < 0) {
    av_free(ctx);
    snprintf(errbuf, errlen, "Unable to open codec");
    return NULL;
  }
  
  frame = av_frame_alloc();

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = (void *)buf_data(buf);
  avpkt.size = buf_size(buf);
  int r = avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);

  if(r < 0 || ctx->width == 0 || ctx->height == 0) {
    snprintf(errbuf, errlen, "Unable to decode image of size (%d x %d)",
             ctx->width, ctx->height);
    avcodec_close(ctx);
    av_free(ctx);
    av_frame_free(&frame);
    return NULL;
  }

#if 0
  printf("%d x %d => %d x %d (lowres=%d) req = %d x %d%s%s\n",
	 ji.ji_width, ji.ji_height,
	 ctx->width, ctx->height, lowres,
	 im->im_req_width, im->im_req_height,
	 im->im_want_thumb ? ", want thumb" : "",
	 pm->pm_flags & PIXMAP_THUMBNAIL ? ", is thumb" : "");
#endif

  pixmap_compute_rescale_dim(im, ctx->width, ctx->height, &w, &h);

  pixmap_t *pm;

  pm = pixmap_from_avpic((AVPicture *)frame, 
			 ctx->pix_fmt, ctx->width, ctx->height, w, h, im);

  if(pm != NULL) {
    pm->pm_aspect = (float)w / (float)h;
  } else {
    snprintf(errbuf, errlen, "Out of memory");
  }
  av_frame_free(&frame);

  avcodec_close(ctx);
  av_free(ctx);
  return pm;
}
