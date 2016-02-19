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
#include <unistd.h>
#include <assert.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavresample/avresample.h>
#include <libavutil/opt.h>
#include <libavutil/mem.h>

#include "misc/queue.h"
#include "main.h"
#include "image/pixmap.h"
#include "glw_rec.h"
#include "misc/minmax.h"
#include "audio2/audio.h"

LIST_HEAD(glw_rec_list, glw_rec);
LIST_HEAD(audio_source_list, audio_source);
TAILQ_HEAD(audio_buf_queue, audio_buf);
TAILQ_HEAD(video_frame_queue, video_frame);

static HTS_MUTEX_DECL(glw_rec_mutex);

static struct glw_rec_list glw_recs;



typedef struct audio_buf {
  TAILQ_ENTRY(audio_buf) ab_link;
  int16_t *ab_buf;
  int ab_samples;
  int ab_used;
  int64_t ab_ts;
} audio_buf_t;


typedef struct audio_source {
  LIST_ENTRY(audio_source) as_link;
  int as_id;
  int as_format;
  int64_t as_channel_layout;
  int as_sample_rate;
  AVAudioResampleContext *as_avr;
  struct audio_buf_queue as_queue;


  int as_start_drop;
  int as_samples_queued;

  int as_samples_consumed;

  int64_t as_last_ts;
  int as_last_ts_sample;

} audio_source_t;



typedef struct video_frame {
  TAILQ_ENTRY(video_frame) vf_link;
  pixmap_t *vf_pm;
} video_frame_t;



struct glw_rec {

  LIST_ENTRY(glw_rec) global_link;
  char *filename;
  AVOutputFormat *fmt;
  AVFormatContext *oc;

  AVCodecContext *v_ctx;
  AVStream *v_st;

  AVCodecContext *a_ctx;
  AVStream *a_st;

  int width;
  int height;
  int fps;
  int framenum;

  int64_t video_pts;
  int64_t audio_pts;

  int samples_written;
  struct audio_source_list asources;

  hts_cond_t gr_cond;
  struct video_frame_queue gr_vframes;
  int gr_vqlen;
  int gr_stop;
};



/**
 *
 */
static void
emit_audio(glw_rec_t *gr, int64_t pts)
{
#define SAMPLES_PER_FRAME 1024
  int16_t data[SAMPLES_PER_FRAME * 2];


  while(1) {

    memset(data, 0, sizeof(data));

    hts_mutex_lock(&glw_rec_mutex);
    audio_source_t *as;

    LIST_FOREACH(as, &gr->asources, as_link) {
      if(as->as_samples_queued < 1024) {
        hts_mutex_unlock(&glw_rec_mutex);
        return;
      }
    }

    LIST_FOREACH(as, &gr->asources, as_link) {
      int offset = 0;

      while(as->as_samples_queued > 0 && offset < SAMPLES_PER_FRAME) {
        audio_buf_t *ab = TAILQ_FIRST(&as->as_queue);


        if(ab->ab_ts != AV_NOPTS_VALUE) {
          if(as->as_last_ts != AV_NOPTS_VALUE) {

          }
          as->as_last_ts = ab->ab_ts;
          as->as_last_ts_sample = as->as_samples_consumed;

        }


        int consume = MIN(ab->ab_samples - ab->ab_used,
                          SAMPLES_PER_FRAME - offset);
        assert(consume > 0);
        for(int i = 0; i < consume; i++) {
          data[(offset + i) * 2 + 0] += ab->ab_buf[(i + ab->ab_used) * 2 + 0];
          data[(offset + i) * 2 + 1] += ab->ab_buf[(i + ab->ab_used) * 2 + 1];
        }
        offset += consume;
        ab->ab_used += consume;
        as->as_samples_consumed += consume;
        assert(ab->ab_used <= ab->ab_samples);
        as->as_samples_queued -= consume;
        if(ab->ab_used == ab->ab_samples) {
          free(ab->ab_buf);
          TAILQ_REMOVE(&as->as_queue, ab, ab_link);
          free(ab);
        }
      }
    }

    hts_mutex_unlock(&glw_rec_mutex);

    AVPacket pkt;
    AVFrame frame;
    memset(&frame, 0, sizeof(frame));

    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    int64_t ts = av_rescale_q(gr->samples_written,
                              (AVRational){1, 48000},
                              gr->a_st->time_base);


    if(ts >= 0) {

      frame.data[0] = (void *)data;
      frame.nb_samples = SAMPLES_PER_FRAME;

      int got_packet;
      int r = avcodec_encode_audio2(gr->a_ctx, &pkt, &frame, &got_packet);
      if(r < 0 || !got_packet)
        abort();


      pkt.pts = pkt.dts = ts;
      pkt.stream_index = gr->a_st->index;
      pkt.duration = av_rescale_q(1, (AVRational){SAMPLES_PER_FRAME, 48000},
                                  gr->v_st->time_base);
      av_interleaved_write_frame(gr->oc, &pkt);
      av_free_packet(&pkt);
    }

    gr->samples_written += SAMPLES_PER_FRAME;

    int64_t t = av_rescale_q(gr->samples_written,
                             (AVRational){1, 48000},
                             AV_TIME_BASE_Q);
    if(t >= pts)
      break;
  }
}


/**
 *
 */
static void
encode_vframe(glw_rec_t *gr, struct pixmap *pm)
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

  emit_audio(gr, pts);
}


/**
 *
 */
static void *
rec_thread(void *aux)
{
  glw_rec_t *gr = aux;
  video_frame_t *vf;


  gr->fmt = av_guess_format(NULL, gr->filename, NULL);
  if(gr->fmt == NULL) {
    TRACE(TRACE_ERROR, "REC",
	  "Unable to record to %s -- Unknown file format",
	  gr->filename);
    return NULL;
  }

  gr->oc = avformat_alloc_context();
  gr->oc->oformat = gr->fmt;
  snprintf(gr->oc->filename, sizeof(gr->oc->filename), "%s", gr->filename);

  gr->v_st = avformat_new_stream(gr->oc, 0);

  gr->v_st->avg_frame_rate.num = gr->fps;
  gr->v_st->avg_frame_rate.den = 1;

  gr->v_ctx = gr->v_st->codec;
  gr->v_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
  gr->v_ctx->codec_id = AV_CODEC_ID_FFVHUFF;

  gr->v_ctx->width = gr->width;
  gr->v_ctx->height = gr->height;
  gr->v_ctx->time_base.den = gr->fps;
  gr->v_ctx->time_base.num = 1;
  gr->v_ctx->pix_fmt = AV_PIX_FMT_RGB32;
  gr->v_ctx->coder_type = 0;

  AVCodec *c = avcodec_find_encoder(gr->v_ctx->codec_id);
  if(avcodec_open2(gr->v_ctx, c, NULL)) {
    TRACE(TRACE_ERROR, "REC",
	  "Unable to record to %s -- Unable to open video codec",
	  gr->filename);
    return NULL;
  }

  gr->v_ctx->thread_count = gconf.concurrency;

  gr->a_st = avformat_new_stream(gr->oc, 0);

  gr->a_ctx = gr->a_st->codec;
  gr->a_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
  gr->a_ctx->codec_id = AV_CODEC_ID_PCM_S16LE;

  gr->a_ctx->sample_rate = 48000;
  gr->a_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
  gr->a_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
  gr->a_ctx->time_base.den = 48000;
  gr->a_ctx->time_base.num = 1;

  c = avcodec_find_encoder(gr->a_ctx->codec_id);
  if(avcodec_open2(gr->a_ctx, c, NULL)) {
    TRACE(TRACE_ERROR, "REC",
	  "Unable to record to %s -- Unable to open audio codec",
	  gr->filename);
    return NULL;
  }

  gr->v_ctx->thread_count = gconf.concurrency;

  // Output output file

  av_dump_format(gr->oc, 0, gr->filename, 1);

  if(avio_open(&gr->oc->pb, gr->filename, AVIO_FLAG_WRITE) < 0) {
    TRACE(TRACE_ERROR, "REC",
	  "Unable to record to %s -- Unable to open file for writing",
	  gr->filename);
    return NULL;
  }

  /* write the stream header, if any */
  avformat_write_header(gr->oc, NULL);


  hts_mutex_lock(&glw_rec_mutex);

  while(!gr->gr_stop) {
    vf = TAILQ_FIRST(&gr->gr_vframes);
    if(vf == NULL) {
      hts_cond_wait(&gr->gr_cond, &glw_rec_mutex);
      continue;
    }
    TAILQ_REMOVE(&gr->gr_vframes, vf, vf_link);
    gr->gr_vqlen--;
    hts_mutex_unlock(&glw_rec_mutex);
    encode_vframe(gr, vf->vf_pm);
    pixmap_release(vf->vf_pm);
    free(vf);
    hts_mutex_lock(&glw_rec_mutex);
  }

  hts_mutex_unlock(&glw_rec_mutex);

  av_write_trailer(gr->oc);

  for(int i = 0; i < gr->oc->nb_streams; i++) {
    AVStream *st = gr->oc->streams[i];
    avcodec_close(st->codec);
    free(st->codec);
    free(st);
  }

  avio_close(gr->oc->pb);
  free(gr->oc);
  free(gr);
  return NULL;
}


/**
 *
 */
glw_rec_t *
glw_rec_init(const char *filename, int width, int height, int fps)
{
  struct glw_rec *gr = calloc(1, sizeof(glw_rec_t));

  TAILQ_INIT(&gr->gr_vframes);
  //  gr->samples_written = -40000;
  gr->width = width;
  gr->height = height;
  gr->fps = fps;
  gr->filename = strdup(filename);
  hts_cond_init(&gr->gr_cond, &glw_rec_mutex);

  hts_mutex_lock(&glw_rec_mutex);
  LIST_INSERT_HEAD(&glw_recs, gr, global_link);
  hts_mutex_unlock(&glw_rec_mutex);

  hts_thread_create_detached("rec", rec_thread, gr, THREAD_PRIO_BGTASK);
  return gr;
}


/**
 *
 */
void
glw_rec_stop(glw_rec_t *gr)
{
  hts_mutex_lock(&glw_rec_mutex);
  gr->gr_stop = 1;
  hts_cond_signal(&gr->gr_cond);
  LIST_REMOVE(gr, global_link);
  hts_mutex_unlock(&glw_rec_mutex);
}


/**
 *
 */
void
glw_rec_audio_send(struct audio_decoder *ad, AVFrame *frame, int64_t pts)
{
  glw_rec_t *gr;

  if(LIST_FIRST(&glw_recs) == NULL)
    return;

  hts_mutex_lock(&glw_rec_mutex);
  LIST_FOREACH(gr, &glw_recs, global_link) {

    audio_source_t *as;

    LIST_FOREACH(as, &gr->asources, as_link) {
      if(as->as_id == ad->ad_id)
        break;
    }


    if(frame == NULL) {
      if(as == NULL)
        continue;
      LIST_REMOVE(as, as_link);
      printf("Stream %d stopped\n", as->as_id);
      continue;
    }



    if(as == NULL) {
      printf("Delay = %d\n", ad->ad_delay);

      as = calloc(1, sizeof(audio_source_t));
      as->as_last_ts = AV_NOPTS_VALUE;
      TAILQ_INIT(&as->as_queue);
      LIST_INSERT_HEAD(&gr->asources, as, as_link);
      as->as_id = ad->ad_id;
      as->as_start_drop = 24000;
      as->as_avr = avresample_alloc_context();
    }

    if(as->as_format != ad->ad_in_sample_format ||
       as->as_channel_layout != ad->ad_in_channel_layout ||
       as->as_sample_rate != ad->ad_in_sample_rate) {

      avresample_close(as->as_avr);

      as->as_format = ad->ad_in_sample_format;
      as->as_channel_layout = ad->ad_in_channel_layout;
      as->as_sample_rate = ad->ad_in_sample_rate;

      av_opt_set_int(as->as_avr, "in_sample_fmt",
                     as->as_format, 0);
      av_opt_set_int(as->as_avr, "in_sample_rate",
                     as->as_sample_rate, 0);
      av_opt_set_int(as->as_avr, "in_channel_layout",
                     as->as_channel_layout, 0);

      av_opt_set_int(as->as_avr, "out_sample_fmt",
                     AV_SAMPLE_FMT_S16, 0);
      av_opt_set_int(as->as_avr, "out_sample_rate",
                     48000, 0);
      av_opt_set_int(as->as_avr, "out_channel_layout",
                     AV_CH_LAYOUT_STEREO, 0);

      char buf1[128];

      av_get_channel_layout_string(buf1, sizeof(buf1),
                                   -1, as->as_channel_layout);

      TRACE(TRACE_DEBUG, "REC",
            "Converting from [%s %dHz %s]",
            buf1, as->as_sample_rate,
            av_get_sample_fmt_name(as->as_format));

      if(avresample_open(as->as_avr)) {
        TRACE(TRACE_ERROR, "REC", "Unable to open resampler");
        avresample_free(&as->as_avr);
      }
    }

    if(as->as_avr == NULL)
      continue;

    avresample_convert(as->as_avr, NULL, 0, 0,
                       frame->data, frame->linesize[0],
                       frame->nb_samples);

    int avail = avresample_available(as->as_avr);

    if(avail == 0)
      continue;

    if(as->as_start_drop > 0) {
      avresample_read(as->as_avr, NULL, avail);
      printf("Dropped %d\n", avail);
      as->as_start_drop -= avail;
    } else {

      int bytes = avail * sizeof(int16_t) * 2;  // 16 bit stereo

      void *buf = malloc(bytes);
      uint8_t *data[8] = {0};
      data[0] = (uint8_t *)buf;
      avresample_read(as->as_avr, data, avail);
      audio_buf_t *ab = calloc(1, sizeof(audio_buf_t));
      ab->ab_buf = buf;
      ab->ab_samples = avail;
      for(int i = 0; i < avail * 2; i++) {
        ab->ab_buf[i] *= ad->ad_vol_scale;

      }
      TAILQ_INSERT_TAIL(&as->as_queue, ab, ab_link);
      as->as_samples_queued += avail;
    }
  }
  hts_mutex_unlock(&glw_rec_mutex);
}


void
glw_rec_deliver_vframe(glw_rec_t *gr, struct pixmap *pm)
{
  video_frame_t *vf = calloc(1, sizeof(video_frame_t));
  vf->vf_pm = pixmap_dup(pm);

  hts_mutex_lock(&glw_rec_mutex);
  hts_cond_signal(&gr->gr_cond);
  TAILQ_INSERT_TAIL(&gr->gr_vframes, vf, vf_link);
  gr->gr_vqlen++;
  if(gr->gr_vqlen > 10)
    TRACE(TRACE_ERROR, "REC", "Warning video queue length is %d", gr->gr_vqlen);
  hts_mutex_unlock(&glw_rec_mutex);
}
