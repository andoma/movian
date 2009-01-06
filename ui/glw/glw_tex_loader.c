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

#include "config.h"

#include <assert.h>
#include <stdlib.h>

#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#include "glw.h"
#include "glw_bitmap.h"
#include "glw_scaler.h"

#include "fileaccess/fa_imageloader.h"
#include "showtime.h"

static int maxwidth = 2048;
static int maxheight = 1024;

static int image_decode(glw_root_t *gr, glw_texture_t *ht);

static void glw_tex_deref_locked(glw_root_t *gr, glw_texture_t *gt);
static void glw_tex_set_state_locked(glw_texture_t *gt, int state);


void
glw_tex_is_active(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  LIST_REMOVE(gt, gt_flush_link);
  LIST_INSERT_HEAD(&gr->gr_tex_active_list, gt, gt_flush_link);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

static void
glw_tex_free_gl_resources(glw_texture_t *gt)
{
  if(gt->gt_texture != 0) {
    glDeleteTextures(1, &gt->gt_texture);
    gt->gt_texture = 0;
  }
}

void
glw_tex_autoflush(glw_root_t *gr)
{
  glw_texture_t *gt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while((gt = LIST_FIRST(&gr->gr_tex_flush_list)) != NULL) {
    assert(gt->gt_filename != NULL);
    LIST_REMOVE(gt, gt_flush_link);
    glw_tex_free_gl_resources(gt);
    gt->gt_state = GT_STATE_INACTIVE;
  }

  LIST_MOVE(&gr->gr_tex_flush_list, &gr->gr_tex_active_list, gt_flush_link);
  LIST_INIT(&gr->gr_tex_active_list);

  hts_mutex_unlock(&gr->gr_tex_mutex);
}




static void *
image_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_texture_t *gt;
  int r;

  hts_mutex_lock(&gr->gr_tex_mutex);

  while(1) {
    
    while(1) {

      if((gt = TAILQ_FIRST(&gr->gr_tex_load_queue[0])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[0], gt, gt_work_link);
	break;
      }
      if((gt = TAILQ_FIRST(&gr->gr_tex_load_queue[1])) != NULL) {
	TAILQ_REMOVE(&gr->gr_tex_load_queue[1], gt, gt_work_link);
	break;
      }
      hts_cond_wait(&gr->gr_tex_load_cond, &gr->gr_tex_mutex);
    }

    if(gt->gt_refcnt > 1) {
      hts_mutex_unlock(&gr->gr_tex_mutex);
      r = image_decode(gr, gt);
      hts_mutex_lock(&gr->gr_tex_mutex);
    } else {
      r = 0;
    }
    
    glw_tex_set_state_locked(gt, r < 0 ? GT_STATE_ERROR : GT_STATE_VALID);

    LIST_INSERT_HEAD(&gr->gr_tex_active_list, gt, gt_flush_link);

    glw_tex_deref_locked(gr, gt);
  }
}


void
glw_image_init(glw_root_t *gr)
{
  int i;
  hts_thread_t imageptid;
  extern int concurrency;

  hts_mutex_init(&gr->gr_tex_mutex);
  hts_cond_init(&gr->gr_tex_load_cond);

  TAILQ_INIT(&gr->gr_tex_rel_queue);
  TAILQ_INIT(&gr->gr_tex_load_queue[0]);
  TAILQ_INIT(&gr->gr_tex_load_queue[1]);

  /* Start multiple workers for decoding images */
  for(i = 0; i < concurrency; i++)
    hts_thread_create(&imageptid, image_thread, gr);
}

/**
 * Flush all loaded textures, must be done on the gl thread context
 */
void
glw_tex_flush_all(glw_root_t *gr)
{
  glw_texture_t *gt;
  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(gt, &gr->gr_tex_list, gt_global_link) {
    if(gt->gt_state != GT_STATE_VALID)
      continue;
    LIST_REMOVE(gt, gt_flush_link);
    glw_tex_free_gl_resources(gt);
    gt->gt_state = GT_STATE_INACTIVE;
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
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

static void texture_load_direct(AVFrame *frame, glw_texture_t *gt);

static void texture_load_rescale(AVCodecContext *ctx, AVFrame *frame,
				 glw_texture_t *gt);

static void texture_load_rescale_swscale(AVCodecContext *ctx, AVFrame *frame,
					 glw_texture_t *gt, int pix_fmt);


static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

static int
image_decode(glw_root_t *gr, glw_texture_t *gt)
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

  if(gt->gt_filename == NULL)
    return -1;

  memset(&ctrl, 0, sizeof(ctrl));
  ctrl.url = gt->gt_filename;
  
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
    gt->gt_bpp = 3;
    gt->gt_format = GL_RGB;
    gt->gt_ext_format = GL_RGB;
    gt->gt_ext_type = GL_UNSIGNED_BYTE;
    break;

  case PIX_FMT_RGBA32:
    gt->gt_bpp = 4;
    gt->gt_format = GL_RGBA;
    gt->gt_ext_format = GL_BGRA;
    gt->gt_ext_type = GL_UNSIGNED_BYTE;
    break;
    
  case PIX_FMT_GRAY8:
    gt->gt_bpp = 1;
    gt->gt_format = GL_LUMINANCE;
    gt->gt_ext_format = GL_LUMINANCE;
    gt->gt_ext_type = GL_UNSIGNED_BYTE;
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

    gt->gt_bpp = 4;
    gt->gt_format = GL_RGBA;
    gt->gt_ext_format = GL_BGRA;
    gt->gt_ext_type = GL_UNSIGNED_BYTE;

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

  gt->gt_aspect = (float)w / (float)h;

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

  gt->gt_xs = w;
  gt->gt_ys = h;

  frame = frame_B ?: frame_A;

  if(need_rescale && !need_format_conv) {
    texture_load_rescale(ctx, frame, gt);
  } else
    if(need_rescale || need_format_conv) {
    texture_load_rescale_swscale(ctx, frame, gt, pix_fmt);

  } else {
    texture_load_direct(frame, gt);
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
texture_load_direct(AVFrame *frame, glw_texture_t *gt)
{
  uint8_t *src, *dst;
  int w = gt->gt_xs;
  int h = gt->gt_ys;

  gt->gt_bitmap_size = gt->gt_bpp * gt->gt_xs * gt->gt_ys;

  gt->gt_bitmap = mmap(NULL, gt->gt_bitmap_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  src = frame->data[0];
  dst = gt->gt_bitmap;
  w *= gt->gt_bpp;

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
		     glw_texture_t *gt)
{
  gt->gt_bitmap_size = gt->gt_bpp * gt->gt_xs * gt->gt_ys;
  gt->gt_bitmap = mmap(NULL, gt->gt_bitmap_size,
		       PROT_READ | PROT_WRITE, 
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  glw_bitmap_rescale(frame->data[0], ctx->width, ctx->height, 
		     frame->linesize[0],
		     gt->gt_bitmap, gt->gt_xs, gt->gt_ys, 
		     gt->gt_xs * gt->gt_bpp, gt->gt_bpp);
}


/**
 * Rescaling with FFmpeg's swscaler
 */
static void
texture_load_rescale_swscale(AVCodecContext *ctx, AVFrame *frame,
			     glw_texture_t *gt, int pix_fmt)
{
  AVPicture pic;
  struct SwsContext *sws;
  int w = gt->gt_xs;
  int h = gt->gt_ys;

  sws = sws_getContext(ctx->width, ctx->height, pix_fmt, 
		       w, h, PIX_FMT_RGB24,
		       SWS_BICUBIC, NULL, NULL, NULL);
  gt->gt_bpp = 3;
  gt->gt_format = GL_RGB;
  gt->gt_ext_format = GL_RGB;
  gt->gt_ext_type = GL_UNSIGNED_BYTE;

  gt->gt_bitmap_size = gt->gt_bpp * gt->gt_xs * gt->gt_ys;
  
  gt->gt_bitmap = mmap(NULL, gt->gt_bitmap_size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
  pic.data[0] = gt->gt_bitmap;
  pic.linesize[0] = w * gt->gt_bpp;
  
  if(sws_scale(sws, frame->data, frame->linesize, 0, 0,
	       pic.data, pic.linesize) < 0) {
    fprintf(stderr, "%s: scaling failed\n", gt->gt_filename);
  }
  
  sws_freeContext(sws);
}


/*****************************************************************************
 *
 *
 *
 */
void
glw_texture_purge(glw_root_t *gr)
{
  glw_texture_t *gt; 
  hts_mutex_lock(&gr->gr_tex_mutex);

  while((gt = TAILQ_FIRST(&gr->gr_tex_rel_queue)) != NULL) {
    TAILQ_REMOVE(&gr->gr_tex_rel_queue, gt, gt_work_link);

    glw_tex_free_gl_resources(gt);
    free(gt);
  }
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


static void
glw_tex_deref_locked(glw_root_t *gr, glw_texture_t *gt)
{
  gt->gt_refcnt--;

  if(gt->gt_refcnt > 0)
    return;
  
  if(gt->gt_filename != NULL) {
    if(gt->gt_state == GT_STATE_VALID || gt->gt_state == GT_STATE_ERROR)
      LIST_REMOVE(gt, gt_flush_link);

    LIST_REMOVE(gt, gt_global_link);
    free((void *)gt->gt_filename);
  }
  
  if(gt->gt_bitmap != NULL)
    munmap(gt->gt_bitmap, gt->gt_bitmap_size);

  TAILQ_INSERT_TAIL(&gr->gr_tex_rel_queue, gt, gt_work_link);
}

void
glw_tex_deref(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  glw_tex_deref_locked(gr, gt);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}


static void 
glw_tex_set_state_locked(glw_texture_t *gt, int state)
{
  gt->gt_state = state;
  hts_cond_signal(&gt->gt_cond);
}



glw_texture_t *
glw_tex_create(glw_root_t *gr, const char *filename)
{
  glw_texture_t *gt;

  hts_mutex_lock(&gr->gr_tex_mutex);

  LIST_FOREACH(gt, &gr->gr_tex_list, gt_global_link)
    if(!strcmp(gt->gt_filename, filename))
      break;

  if(gt == NULL) {
    gt = calloc(1, sizeof(glw_texture_t));
    gt->gt_filename = strdup(filename);
    LIST_INSERT_HEAD(&gr->gr_tex_list, gt, gt_global_link);
    gt->gt_state = GT_STATE_INACTIVE;
  }

  gt->gt_refcnt++;

  hts_mutex_unlock(&gr->gr_tex_mutex);
  return gt;
}


static void
gl_tex_req_load(glw_root_t *gr, glw_texture_t *gt)
{
  hts_mutex_lock(&gr->gr_tex_mutex);
  gt->gt_refcnt++;

  if(!strncmp(gt->gt_filename, "thumb://", 8)) {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[1], gt, gt_work_link);
  } else {
    TAILQ_INSERT_TAIL(&gr->gr_tex_load_queue[0], gt, gt_work_link);
  }

  gt->gt_state = GT_STATE_LOADING;

  hts_cond_signal(&gr->gr_tex_load_cond);
  hts_mutex_unlock(&gr->gr_tex_mutex);
}

void
glw_tex_layout(glw_root_t *gr, glw_texture_t *gt)
{
  void *p;

  switch(gt->gt_state) {
  case GT_STATE_INACTIVE:
    gl_tex_req_load(gr, gt);
    return;
    
  case GT_STATE_LOADING:
    return;

  case GT_STATE_VALID:

    if(gt->gt_texture == 0) {
      p = gt->gt_bitmap;

      glGenTextures(1, &gt->gt_texture);
      glBindTexture(GL_TEXTURE_2D, gt->gt_texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

      glTexImage2D(GL_TEXTURE_2D, 0, gt->gt_format, 
		   gt->gt_xs, gt->gt_ys,
		   0, gt->gt_ext_format,
		   gt->gt_ext_type, p);

      glBindTexture(GL_TEXTURE_2D, 0);

      if(gt->gt_bitmap != NULL) {
	munmap(gt->gt_bitmap, gt->gt_bitmap_size);
	gt->gt_bitmap = NULL;
      }
    }

    /* FALLTHRU */

  case GT_STATE_ERROR:
    glw_tex_is_active(gr, gt);
    break;
  }
}
