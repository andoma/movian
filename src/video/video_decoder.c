/*
 *  Video decoder
 *  Copyright (C) 2007 - 2010 Andreas Öman
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

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "video_decoder.h"
#include "event.h"
#include "media.h"

static void
vd_init_timings(video_decoder_t *vd)
{
  kalman_init(&vd->vd_avfilter);
  vd->vd_lastpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;
}


/**
 *
 */
typedef struct {
  int64_t pts;
  int64_t dts;
  int epoch;
  int duration;
  int64_t time;
} frame_meta_t;


/**
 *
 */
static int
vd_get_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  media_buf_t *mb = c->opaque;
  int ret = avcodec_default_get_buffer(c, pic);
  frame_meta_t *fm = malloc(sizeof(frame_meta_t));

  fm->pts = mb->mb_pts;
  fm->dts = mb->mb_dts;
  fm->time = mb->mb_time;
  fm->duration = mb->mb_duration;
  fm->epoch = mb->mb_epoch;
  pic->opaque = fm;

  return ret;
}

static void
vd_release_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  frame_meta_t *fm = pic->opaque;

  if(fm != NULL)
    free(fm);

  avcodec_default_release_buffer(c, pic);
}


#define vd_valid_duration(t) ((t) > 1000ULL && (t) < 10000000ULL)

static void 
vd_decode_video(video_decoder_t *vd, media_queue_t *mq, media_buf_t *mb)
{
  int64_t pts, dts, t;
  int got_pic, duration, epoch;
  media_pipe_t *mp = vd->vd_mp;
  float f;
  media_codec_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = vd->vd_frame;
  frame_meta_t *fm;
  event_ts_t *ets;

  got_pic = 0;

  if(vd->vd_do_flush) {
    do {
      avcodec_decode_video(ctx, frame, &got_pic, NULL, 0);
    } while(got_pic);

    vd->vd_do_flush = 0;
    vd->vd_lastpts = AV_NOPTS_VALUE;
    vd->vd_estimated_duration = 0;
    avcodec_flush_buffers(ctx);
    vd->vd_compensate_thres = 5;
  }

  ctx->opaque = mb;
  ctx->get_buffer = vd_get_buffer;
  ctx->release_buffer = vd_release_buffer;

  /*
   * If we are seeking, drop any non-reference frames
   */
  ctx->skip_frame = mb->mb_skip == 1 ? AVDISCARD_NONREF : AVDISCARD_NONE;

  if(mb->mb_skip == 2)
    vd->vd_skip = 1;

  if(mp->mp_stats)
    avgtime_start(&vd->vd_decode_time);

  avcodec_decode_video(ctx, frame, &got_pic, mb->mb_data, mb->mb_size);

  if(mp->mp_stats) {
    avgtime_stop(&vd->vd_decode_time, mq->mq_prop_decode_avg,
		 mq->mq_prop_decode_peak);
    mp_set_mq_meta(mq, cw->codec, cw->codec_ctx);
  }

  if(got_pic == 0 || mb->mb_skip == 1) 
    return;

  vd->vd_skip = 0;

  /* Update aspect ratio */

  switch(mb->mb_aspect_override) {
  case 0:

    if(frame->pan_scan != NULL && frame->pan_scan->width != 0)
      f = (float)frame->pan_scan->width / (float)frame->pan_scan->height;
    else
      f = (float)ctx->width / (float)ctx->height;
    
    vd->vd_aspect = (av_q2d(ctx->sample_aspect_ratio) ?: 1) * f;
    break;
  case 1:
    vd->vd_aspect = (4.0f / 3.0f);
    break;
  case 2:
    vd->vd_aspect = (16.0f / 9.0f);
    break;
  }

  
  /* Compute duration and PTS of frame */

  fm = frame->opaque;
  assert(fm != NULL);

  if(fm->time != AV_NOPTS_VALUE)
    mp_set_current_time(mp, fm->time);

  pts = fm->pts;
  dts = fm->dts;
  duration = fm->duration;
  epoch = fm->epoch;

  if(pts == AV_NOPTS_VALUE && dts != AV_NOPTS_VALUE &&
     (ctx->has_b_frames == 0 || frame->pict_type == FF_B_TYPE)) {
    pts = dts;
  }

  if(!vd_valid_duration(duration)) {
    /* duration is zero or very invalid, use duration from last output */
    duration = vd->vd_estimated_duration;
  }

  if(pts == AV_NOPTS_VALUE && vd->vd_nextpts != AV_NOPTS_VALUE)
    pts = vd->vd_nextpts; /* no pts set, use estimated pts */

  if(pts != AV_NOPTS_VALUE && vd->vd_lastpts != AV_NOPTS_VALUE) {
    /* we know pts of last frame */
    t = (pts - vd->vd_lastpts) / vd->vd_frames_since_last;

    if(vd_valid_duration(t)) {
      /* inter frame duration seems valid, store it */
      vd->vd_estimated_duration = t;
      if(duration == 0)
	duration = t;

    } else if(t < 0 || t > 10000000LL) {
      /* crazy pts jump, use estimated pts from last output instead */
      pts = vd->vd_nextpts;
    }
  }
  
  /* compensate for frame repeat */
  
  duration += frame->repeat_pict * (duration * 0.5);
 
  if(pts != AV_NOPTS_VALUE) {
    vd->vd_lastpts = pts;
    if(duration != 0)
      vd->vd_nextpts = pts + duration;
    vd->vd_frames_since_last = 0;
  }
  vd->vd_frames_since_last++;

  if(duration == 0 || pts == AV_NOPTS_VALUE)
    return;

  ets = event_create(EVENT_CURRENT_PTS, sizeof(event_ts_t));
  ets->pts = pts;
  ets->dts = dts;
  mp_enqueue_event(mp, &ets->h);
  event_unref(&ets->h);

  //  TRACE(TRACE_DEBUG, "frame", "%16lld %d %d\n", pts, epoch, duration);

  int deinterlace = 
    frame->interlaced_frame && !mb->mb_disable_deinterlacer;

  vd->vd_deinterlace |= deinterlace;

  vd->vd_frame_deliver(frame->data, frame->linesize,
		       ctx->width, ctx->height, ctx->pix_fmt,
		       pts, epoch, duration, 
		       (vd->vd_deinterlace ? VD_INTERLACED : 0) |
		       (frame->top_field_first ? VD_TFF : 0),
		       vd->vd_opaque);
}


/**
 *
 */
static void
update_vbitrate(media_pipe_t *mp, media_queue_t *mq, video_decoder_t *vd)
{
  int i;
  int64_t sum;

  if(vd->vd_estimated_duration == 0 || !mp->mp_stats)
    return;

  sum = 0;
  for(i = 0; i < VD_FRAME_SIZE_LEN; i++)
    sum += vd->vd_frame_size[i];

  sum = 8000000LL * sum / VD_FRAME_SIZE_LEN / vd->vd_estimated_duration;
  prop_set_int(mq->mq_prop_bitrate, sum / 1000);
}

/**
 * Video decoder thread
 */
static void *
vd_thread(void *aux)
{
  video_decoder_t *vd = aux;
  media_pipe_t *mp = vd->vd_mp;
  media_queue_t *mq = &mp->mp_video;
  media_buf_t *mb;
  media_codec_t *mc;
  int i;
  int run = 1;
  int reqsize = -1;
  vd->vd_frame = avcodec_alloc_frame();

  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    if((mb = TAILQ_FIRST(&mq->mq_q)) == NULL) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    if(mb->mb_data_type == MB_VIDEO && vd->vd_hold && 
       vd->vd_skip == 0 && mb->mb_skip == 0) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_len--;
    mq->mq_bytes -= mb->mb_size;
    mq_update_stats(mp, mq);

    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

    mc = mb->mb_cw;

    switch(mb->mb_data_type) {
    case MB_CTRL_EXIT:
      run = 0;
      break;

    case MB_CTRL_PAUSE:
      vd->vd_hold = 1;
      break;

    case MB_CTRL_PLAY:
      vd->vd_hold = 0;
      break;

    case MB_FLUSH:
      vd_init_timings(vd);
      vd->vd_do_flush = 1;
      vd->vd_deinterlace = 0;
      break;

    case MB_VIDEO:
      vd->vd_frame_size[vd->vd_frame_size_ptr] = mb->mb_size;
      vd->vd_frame_size_ptr = (vd->vd_frame_size_ptr + 1) & VD_FRAME_SIZE_MASK;

      if(mc->data)
	mc->data(mc, vd, mq, mb, reqsize);
      else
	vd_decode_video(vd, mq, mb);

      update_vbitrate(mp, mq, vd);
      reqsize = -1;
      break;

    case MB_REQ_OUTPUT_SIZE:
      reqsize = mb->mb_data32;
      break;

#ifdef CONFIG_DVD
    case MB_DVD_HILITE:
    case MB_DVD_RESET_SPU:
    case MB_DVD_CLUT:
    case MB_DVD_PCI:
    case MB_DVD_SPU:
      dvdspu_decoder_dispatch(vd, mb, mp);
      break;
#endif

    case MB_SUBTITLE:
      video_subtitles_decode(vd, mb);
      break;

    case MB_END:
      break;

    case MB_BLACKOUT:
      vd->vd_frame_deliver(NULL, NULL, 0, 0, 0, 0, 0, 0, 0, vd->vd_opaque);
      break;

    default:
      abort();
    }

    media_buf_free(mb);
    hts_mutex_lock(&mp->mp_mutex);
  }

  hts_mutex_unlock(&mp->mp_mutex);

  /* Free YADIF frames */
  if(vd->vd_yadif_width)
    for(i = 0; i < 3; i++)
      avpicture_free(&vd->vd_yadif_pic[i]);

  /* Free ffmpeg frame */
  av_free(vd->vd_frame);
  return NULL;
}




video_decoder_t *
video_decoder_create(media_pipe_t *mp, vd_frame_deliver_t *frame_delivery,
		     void *opaque)
{
  video_decoder_t *vd = calloc(1, sizeof(video_decoder_t));

  vd->vd_frame_deliver = frame_delivery;
  vd->vd_opaque = opaque;

  mp_ref_inc(mp);
  vd->vd_mp = mp;
  vd->vd_decoder_running = 1;

  vd_init_timings(vd);

#ifdef CONFIG_DVD
  dvdspu_decoder_init(vd);
#endif

  video_subtitles_init(vd);

  hts_thread_create_joinable("video decoder", 
			     &vd->vd_decoder_thread, vd_thread, vd);
  
  return vd;
}


/**
 *
 */
void
video_decoder_stop(video_decoder_t *vd)
{
  media_pipe_t *mp = vd->vd_mp;

  mp_send_cmd_head(mp, &mp->mp_video, MB_CTRL_EXIT);

  vd->vd_decoder_running = 0;
  hts_thread_join(&vd->vd_decoder_thread);
  mp_ref_dec(vd->vd_mp);
  vd->vd_mp = NULL;
}


/**
 *
 */
void
video_decoder_destroy(video_decoder_t *vd)
{
#ifdef CONFIG_DVD
  dvdspu_decoder_deinit(vd);
#endif

  video_subtitles_deinit(vd);
  free(vd);
}
