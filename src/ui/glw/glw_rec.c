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
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

#include "main.h"
#include "image/pixmap.h"
#include "glw_rec.h"

struct glw_rec {
  AVOutputFormat *fmt;
  AVFormatContext *oc;
  AVCodecContext *v_ctx;
  AVStream *v_st;

  int width;
  int height;
  int fps;
  int framenum;

  int64_t pts;

};


glw_rec_t *
glw_rec_init(const char *filename, int width, int height, int fps)
{
  AVCodec *c;
  struct glw_rec *gr = calloc(1, sizeof(glw_rec_t));

  gr->width = width;
  gr->height = height;
  gr->fps = fps;

  gr->fmt = av_guess_format(NULL, filename, NULL);
  if(gr->fmt == NULL) {
    TRACE(TRACE_ERROR, "GLWREC",
	  "Unable to record to %s -- Unknown file format",
	  filename);
    return NULL;
  }

  gr->oc = avformat_alloc_context();
  gr->oc->oformat = gr->fmt;
  snprintf(gr->oc->filename, sizeof(gr->oc->filename), "%s", filename);

  gr->v_st = avformat_new_stream(gr->oc, 0);

  gr->v_st->avg_frame_rate.num = fps;
  gr->v_st->avg_frame_rate.den = 1;

  gr->v_ctx = gr->v_st->codec;
  gr->v_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  gr->v_ctx->codec_id = AV_CODEC_ID_FFVHUFF;

  gr->v_ctx->width = width;
  gr->v_ctx->height = height;
  gr->v_ctx->time_base.den = fps;
  gr->v_ctx->time_base.num = 1;
  gr->v_ctx->pix_fmt = PIX_FMT_RGB32;
  gr->v_ctx->coder_type = 1;

  av_dump_format(gr->oc, 0, filename, 1);

  c = avcodec_find_encoder(gr->v_ctx->codec_id);
  if(avcodec_open2(gr->v_ctx, c, NULL)) {
    TRACE(TRACE_ERROR, "GLWREC",
	  "Unable to record to %s -- Unable to open video codec",
	  filename);
    return NULL;
  }

  gr->v_ctx->thread_count = gconf.concurrency;

  if(avio_open(&gr->oc->pb, filename, AVIO_FLAG_WRITE) < 0) {
    TRACE(TRACE_ERROR, "GLWREC",
	  "Unable to record to %s -- Unable to open file for writing",
	  filename);
    return NULL;
  }

  /* write the stream header, if any */
  avformat_write_header(gr->oc, NULL);
  return gr;
}


/**
 *
 */

void 
glw_rec_stop(glw_rec_t *gr)
{
  int i;

  av_write_trailer(gr->oc);

  for(i = 0; i < gr->oc->nb_streams; i++) {
    AVStream *st = gr->oc->streams[i];
    avcodec_close(st->codec);
    free(st->codec);
    free(st);
  }

  avio_close(gr->oc->pb);
  free(gr->oc);

  free(gr);
}


/**
 *
 */
void
glw_rec_deliver_vframe(glw_rec_t *gr, struct pixmap *pm)
{
  int r;
  AVPacket pkt;
  AVFrame frame;

  memset(&frame, 0, sizeof(frame));

  frame.data[0] = pm->pm_data + pm->pm_linesize * (gr->height - 1);
  frame.linesize[0] = -pm->pm_linesize;
  frame.pts = 1000000LL * gr->framenum / gr->fps;
  gr->framenum++;

  av_init_packet(&pkt);
  pkt.data = NULL;    // packet data will be allocated by the encoder
  pkt.size = 0;

  int got_packet;
  r = avcodec_encode_video2(gr->v_ctx, &pkt, &frame, &got_packet);
  if(r < 0 || !got_packet)
    return;

  int64_t pts = gr->v_ctx->coded_frame->pts;
  if(pts == AV_NOPTS_VALUE)
    pts = frame.pts;

  pkt.dts = pkt.pts = av_rescale_q(pts,
                                   AV_TIME_BASE_Q, gr->v_st->time_base);

  if(gr->v_ctx->coded_frame->key_frame)
    pkt.flags |= AV_PKT_FLAG_KEY;

  pkt.stream_index = gr->v_st->index;
  pkt.duration = av_rescale_q(1, (AVRational){1, gr->fps}, gr->v_st->time_base);
  av_interleaved_write_frame(gr->oc, &pkt);
  av_free_packet(&pkt);
}
