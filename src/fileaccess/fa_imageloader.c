/*
 *  Image loader, used by libglw
 *  Copyright (C) 2008 Andreas Öman
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libavutil/imgutils.h>

#include "showtime.h"
#include "fileaccess.h"
#include "fa_imageloader.h"
#include "fa_libav.h"
#include "misc/pixmap.h"
#include "misc/jpeg.h"

static const uint8_t pngsig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
static const uint8_t gif89sig[6] = {'G', 'I', 'F', '8', '9', 'a'};
static const uint8_t gif87sig[6] = {'G', 'I', 'F', '8', '7', 'a'};

static hts_mutex_t image_from_video_mutex;

static pixmap_t *fa_image_from_video(const char *url);

/**
 *
 */
typedef struct meminfo {
  const uint8_t *data;
  size_t size;
} meminfo_t;


/**
 *
 */
void
fa_imageloader_init(void)
{
  hts_mutex_init(&image_from_video_mutex);
}


/**
 *
 */
static int
jpeginfo_mem_reader(void *handle, void *buf, off_t offset, size_t size)
{
  meminfo_t *mi = handle;

  if(size + offset > mi->size)
    return -1;

  memcpy(buf, mi->data + offset, size);
  return size;
}

/**
 * Load entire image into memory using fileaccess quickload method.
 * Faster than open+read+close.
 */
static pixmap_t *
fa_imageloader2(const char *url, const char **vpaths,
		char *errbuf, size_t errlen)
{
  uint8_t *p;
  struct fa_stat fs;
  meminfo_t mi;
  enum CodecID codec;
  int width = -1, height = -1, orientation = 0;

  if((p = fa_quickload(url, &fs, vpaths, errbuf, errlen)) == NULL) 
    return NULL;

  mi.data = p;
  mi.size = fs.fs_size;

  /* Probe format */

  if((p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') ||
     (p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f')) {
      
    jpeginfo_t ji;
    
    if(jpeg_info(&ji, jpeginfo_mem_reader, &mi, 
		 JPEG_INFO_DIMENSIONS | JPEG_INFO_ORIENTATION,
		 p, fs.fs_size, errbuf, errlen)) {
      free(p);
      return NULL;
    }

    codec = CODEC_ID_MJPEG;

    width = ji.ji_width;
    height = ji.ji_height;
    orientation = ji.ji_orientation;

    jpeg_info_clear(&ji);

  } else if(!memcmp(pngsig, p, 8)) {
    codec = CODEC_ID_PNG;
  } else if(!memcmp(gif87sig, p, sizeof(gif87sig)) ||
	    !memcmp(gif89sig, p, sizeof(gif89sig))) {
    codec = CODEC_ID_GIF;
  } else {
    snprintf(errbuf, errlen, "%s: unknown format", url);
    free(p);
    return NULL;
  }

  pixmap_t *pm = pixmap_alloc_coded(p, fs.fs_size, codec);
  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_orientation = orientation;

  free(p);
  return pm;
}


/**
 *
 */
static int
jpeginfo_reader(void *handle, void *buf, off_t offset, size_t size)
{
  if(avio_seek(handle, offset, SEEK_SET) != offset)
    return -1;
  return avio_read(handle, buf, size);
}


/**
 *
 */
pixmap_t *
fa_imageloader(const char *url, int want_thumb, const char **vpaths,
	       char *errbuf, size_t errlen)
{
  uint8_t p[16];
  int r;
  enum CodecID codec;
  int width = -1, height = -1, orientation = 0;
  AVIOContext *avio;
  pixmap_t *pm;

  if(strchr(url, '#')) {
    hts_mutex_lock(&image_from_video_mutex);
    pm = fa_image_from_video(url);
    if(pm == NULL)
      snprintf(errbuf, errlen, "%s: Unable to extract image", url);
    hts_mutex_unlock(&image_from_video_mutex);
    return pm;
  }

  if(!want_thumb)
    return fa_imageloader2(url, vpaths, errbuf, errlen);

  if((avio = fa_libav_open_vpaths(url, 32768, vpaths)) == NULL) {
    snprintf(errbuf, errlen, "%s: Unable to open file", url);
    return NULL;
  }

  if(avio_read(avio, p, sizeof(p)) != sizeof(p)) {
    snprintf(errbuf, errlen, "%s: file too short", url);
    fa_libav_close(avio);
    return NULL;
  }

  /* Probe format */

  if((p[6] == 'J' && p[7] == 'F' && p[8] == 'I' && p[9] == 'F') ||
     (p[6] == 'E' && p[7] == 'x' && p[8] == 'i' && p[9] == 'f')) {
      
    jpeginfo_t ji;
    
    if(jpeg_info(&ji, jpeginfo_reader, avio,
		 JPEG_INFO_DIMENSIONS |
		 JPEG_INFO_ORIENTATION |
		 (want_thumb ? JPEG_INFO_THUMBNAIL : 0),
		 p, sizeof(p), errbuf, errlen)) {
      fa_libav_close(avio);
      return NULL;
    }

    if(want_thumb && ji.ji_thumbnail) {
      pixmap_t *pm = pixmap_dup(ji.ji_thumbnail);
      fa_libav_close(avio);
      jpeg_info_clear(&ji);
      return pm;
    }

    codec = CODEC_ID_MJPEG;

    width = ji.ji_width;
    height = ji.ji_height;
    orientation = ji.ji_orientation;

    jpeg_info_clear(&ji);

  } else if(!memcmp(pngsig, p, 8)) {
    codec = CODEC_ID_PNG;
  } else if(!memcmp(gif87sig, p, sizeof(gif87sig)) ||
	    !memcmp(gif89sig, p, sizeof(gif89sig))) {
    codec = CODEC_ID_GIF;
  } else {
    snprintf(errbuf, errlen, "%s: unknown format", url);
    fa_libav_close(avio);
    return NULL;
  }

  size_t s = avio_size(avio);
  if(s < 0) {
    snprintf(errbuf, errlen, "%s: Can't read from non-seekable file", url);
    fa_libav_close(avio);
    return NULL;
  }

  pm = pixmap_alloc_coded(NULL, s, codec);

  if(pm == NULL) {
    snprintf(errbuf, errlen, "%s: no memory", url);
    fa_libav_close(avio);
    return NULL;
  }

  pm->pm_width = width;
  pm->pm_height = height;
  pm->pm_orientation = orientation;
  avio_seek(avio, SEEK_SET, 0);
  r = avio_read(avio, pm->pm_data, pm->pm_size);
  fa_libav_close(avio);

  if(r != pm->pm_size) {
    pixmap_release(pm);
    snprintf(errbuf, errlen, "%s: read error", url);
    return NULL;
  }
  return pm;
}

static char *ifv_url;
static AVFormatContext *ifv_fctx;
static AVCodecContext *ifv_ctx;
int ifv_stream;

static void
ifv_close(void)
{
  free(ifv_url);
  ifv_url = NULL;

  if(ifv_ctx != NULL) {
    avcodec_close(ifv_ctx);
    ifv_ctx = NULL;
  }

  if(ifv_fctx != NULL) {
    fa_libav_close_format(ifv_fctx);
    ifv_fctx = NULL;
  }
}



/**
 *
 */
static pixmap_t *
fa_image_from_video(const char *url0)
{
  char *url = mystrdupa(url0);
  char *tim = strchr(url, '#');
  pixmap_t *pm = NULL;

  *tim++ = 0;

  if(ifv_url == NULL || strcmp(url, ifv_url)) {
    // Need to open
    int i;
    AVFormatContext *fctx;
    AVIOContext *avio;
    
    if((avio = fa_libav_open(url, 65536, NULL, 0, 0)) == NULL)
      return NULL;

    if((fctx = fa_libav_open_format(avio, url, NULL, 0, NULL)) == NULL) {
      fa_libav_close(avio);
      return NULL;
    }

    if(!strcmp(fctx->iformat->name, "avi"))
      fctx->flags |= AVFMT_FLAG_GENPTS;

    AVCodecContext *ctx = NULL;
    for(i = 0; i < fctx->nb_streams; i++) {
      if(fctx->streams[i]->codec != NULL && 
	 fctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
	ctx = fctx->streams[i]->codec;
	break;
      }
    }
    if(ctx == NULL) {
      fa_libav_close_format(fctx);
      return NULL;
    }

    AVCodec *codec = avcodec_find_decoder(ctx->codec_id);
    if(codec == NULL) {
      fa_libav_close_format(fctx);
      return NULL;
    }

    if(avcodec_open(ctx, codec) < 0) {
      fa_libav_close_format(fctx);
      return NULL;
    }

    ifv_close();

    ifv_stream = i;
    ifv_url = strdup(url);
    ifv_fctx = fctx;
    ifv_ctx = ctx;
  }

  int secs = atoi(tim);

  AVStream *st = ifv_fctx->streams[ifv_stream];
  int64_t ts = av_rescale(secs, st->time_base.den, st->time_base.num);

  printf("Loading for time %d  ts = %ld\n", secs, ts);

  if(av_seek_frame(ifv_fctx, ifv_stream, ts, 
		   AVSEEK_FLAG_BACKWARD) < 0) {
    ifv_close();
    return NULL;
  }
  

  AVPacket pkt;

  AVFrame *frame = avcodec_alloc_frame();
  int got_pic;

  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;
  do {
    avcodec_decode_video2(ifv_ctx, frame, &got_pic, &pkt);
  } while(got_pic);
  
  
  int cnt = 500;
  int dbg = 0;

  while(1) {
    int r = av_read_frame(ifv_fctx, &pkt);
    
    if(r == AVERROR(EAGAIN))
      continue;
    
    if(r == AVERROR_EOF)
      break;

    if(r != 0) {
      ifv_close();
      break;
    }

    if(pkt.stream_index != ifv_stream) {
      av_free_packet(&pkt);
      continue;
    }
    cnt--;
    if(!dbg) {
      printf(" \t Demuxed frame at %ld\n", pkt.pts);
      dbg = 1;
    }

    int want_pic = pkt.pts >= ts || cnt <= 0;

    ifv_ctx->skip_frame = want_pic ? AVDISCARD_DEFAULT : AVDISCARD_NONREF;
    
    avcodec_decode_video2(ifv_ctx, frame, &got_pic, &pkt);
    if(got_pic == 0 || !want_pic)
      continue;
    printf(" \t Decoded frame at %ld (cnt=%d)\n", pkt.pts, cnt);
    pm = calloc(1, sizeof(pixmap_t));
    pm->pm_refcount = 1;
    pm->pm_width = ifv_ctx->width;
    pm->pm_height = ifv_ctx->height;
    pm->pm_pixfmt = ifv_ctx->pix_fmt;
    pm->pm_codec = CODEC_ID_NONE;

    av_image_alloc(pm->pm_pixels, pm->pm_linesize,
		   pm->pm_width, pm->pm_height,
		   pm->pm_pixfmt, 16);
    
    av_image_copy(pm->pm_pixels, pm->pm_linesize,
		  (const uint8_t **)frame->data, frame->linesize,
		  pm->pm_pixfmt, pm->pm_width, pm->pm_height);
    break;
  }

  av_free(frame);
  return pm;
}
