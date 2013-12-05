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

#include <libavformat/avformat.h>
#include <libavutil/audioconvert.h>

#include "showtime.h"
#include "media.h"
#include "libav.h"
#include "fileaccess/fa_libav.h"

/**
 *
 */
static int
media_codec_create_lavc(media_codec_t *cw, const media_codec_params_t *mcp,
                        media_pipe_t *mp)
{
  const AVCodec *codec = avcodec_find_decoder(cw->codec_id);

  if(codec == NULL)
    return -1;

  if(cw->codec_id == CODEC_ID_AC3 ||
     cw->codec_id == CODEC_ID_EAC3 ||
     cw->codec_id == CODEC_ID_DTS) {

    // We create codec instances later in audio thread.
    return 0;
  }
  
  cw->ctx = cw->fmt_ctx ?: avcodec_alloc_context3(codec);

  //  cw->codec_ctx->debug = FF_DEBUG_PICT_INFO | FF_DEBUG_BUGS;

  if(mcp != NULL && mcp->extradata != NULL && !cw->ctx->extradata) {
    cw->ctx->extradata = calloc(1, mcp->extradata_size +
				FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(cw->ctx->extradata, mcp->extradata, mcp->extradata_size);
    cw->ctx->extradata_size = mcp->extradata_size;
  }

  if(cw->codec_id == CODEC_ID_H264 && gconf.concurrency > 1) {
    cw->ctx->thread_count = gconf.concurrency;
    if(mcp && mcp->cheat_for_speed)
      cw->ctx->flags2 |= CODEC_FLAG2_FAST;
  }

  if(avcodec_open2(cw->ctx, codec, NULL) < 0) {
    TRACE(TRACE_INFO, "libav", "Unable to open codec %s",
	  codec ? codec->name : "<noname>");
    if(cw->fmt_ctx != cw->ctx)
      av_freep(&cw->ctx);
    return -1;
  }
  return 0;
}


REGISTER_CODEC(NULL, media_codec_create_lavc, 1000);

/**
 *
 */
media_format_t *
media_format_create(AVFormatContext *fctx)
{
  media_format_t *fw = malloc(sizeof(media_format_t));
  fw->refcount = 1;
  fw->fctx = fctx;
  return fw;
}


/**
 *
 */
void
media_format_deref(media_format_t *fw)
{
  if(atomic_add(&fw->refcount, -1) > 1)
    return;
  fa_libav_close_format(fw->fctx);
  free(fw);
}


/**
 * 
 */
void
metadata_from_libav(char *dst, size_t dstlen,
		    const AVCodec *codec, const AVCodecContext *avctx)
{
  char *n;
  int off = snprintf(dst, dstlen, "%s", codec->name);

  n = dst;
  while(*n) {
    *n = toupper((int)*n);
    n++;
  }

  if(codec->id  == CODEC_ID_H264) {
    const char *p;
    switch(avctx->profile) {
    case FF_PROFILE_H264_BASELINE:  p = "Baseline";  break;
    case FF_PROFILE_H264_MAIN:      p = "Main";      break;
    case FF_PROFILE_H264_EXTENDED:  p = "Extended";  break;
    case FF_PROFILE_H264_HIGH:      p = "High";      break;
    case FF_PROFILE_H264_HIGH_10:   p = "High 10";   break;
    case FF_PROFILE_H264_HIGH_422:  p = "High 422";  break;
    case FF_PROFILE_H264_HIGH_444:  p = "High 444";  break;
    case FF_PROFILE_H264_CAVLC_444: p = "CAVLC 444"; break;
    default:                        p = NULL;        break;
    }

    if(p != NULL && avctx->level != FF_LEVEL_UNKNOWN)
      off += snprintf(dst + off, dstlen - off,
		      ", %s (Level %d.%d)",
		      p, avctx->level / 10, avctx->level % 10);
  }
    
  if(avctx->codec_type == AVMEDIA_TYPE_AUDIO) {
    char buf[64];

    av_get_channel_layout_string(buf, sizeof(buf), avctx->channels,
                                 avctx->channel_layout);
					    
    off += snprintf(dst + off, dstlen - off, ", %d Hz, %s",
		    avctx->sample_rate, buf);
  }

  if(avctx->width)
    off += snprintf(dst + off, dstlen - off,
		    ", %dx%d", avctx->width, avctx->height);
  
  if(avctx->codec_type == AVMEDIA_TYPE_AUDIO && avctx->bit_rate)
    off += snprintf(dst + off, dstlen - off,
		    ", %d kb/s", avctx->bit_rate / 1000);

}

/**
 * 
 */
void
mp_set_mq_meta(media_queue_t *mq, const AVCodec *codec, 
	       const AVCodecContext *avctx)
{
  char buf[128];
  metadata_from_libav(buf, sizeof(buf), codec, avctx);
  prop_set_string(mq->mq_prop_codec, buf);
}


