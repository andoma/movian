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
#include "ext_subtitles.h"
#include "video_overlay.h"
#include "misc/sha.h"

static void
vd_init_timings(video_decoder_t *vd)
{
  kalman_init(&vd->vd_avfilter);
  vd->vd_prevpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;
}


/**
 *
 */
static int
vd_get_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  int ret = avcodec_default_get_buffer(c, pic);
  media_buf_t *mb = malloc(sizeof(media_buf_t));
  memcpy(mb, c->opaque, sizeof(media_buf_t));
  pic->opaque = mb;
  return ret;
}


static void
vd_release_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  if(pic->opaque != NULL)
    free(pic->opaque);

  avcodec_default_release_buffer(c, pic);
}


#define vd_valid_duration(t) ((t) > 1000ULL && (t) < 10000000ULL)

static void 
vd_decode_video(video_decoder_t *vd, media_queue_t *mq, media_buf_t *mb)
{
  int got_pic = 0;
  media_pipe_t *mp = vd->vd_mp;
  media_codec_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = vd->vd_frame;
  int t;
  
  if(vd->vd_do_flush) {
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = NULL;
    avpkt.size = 0;
    do {
      avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);
    } while(got_pic);

    vd->vd_do_flush = 0;
    vd->vd_prevpts = AV_NOPTS_VALUE;
    vd->vd_nextpts = AV_NOPTS_VALUE;
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
  ctx->skip_frame = mb->mb_skip == 1 ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;

  avgtime_start(&vd->vd_decode_time);

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = mb->mb_data;
  avpkt.size = mb->mb_size;

  avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);

  t = avgtime_stop(&vd->vd_decode_time, mq->mq_prop_decode_avg,
	       mq->mq_prop_decode_peak);

  if(mp->mp_stats)
    mp_set_mq_meta(mq, cw->codec, cw->codec_ctx);

  mb = frame->opaque;

  if(got_pic == 0 || mb->mb_skip == 1) 
    return;

  vd->vd_skip = 0;
  video_deliver_frame(vd, mp, mq, ctx, frame, mb, t);
}


/**
 *
 */
void
video_deliver_frame(video_decoder_t *vd,
		    media_pipe_t *mp, media_queue_t *mq,
		    AVCodecContext *ctx, AVFrame *frame,
		    const media_buf_t *mb, int decode_time)
{
  event_ts_t *ets;
  frame_info_t fi;

  if(mb->mb_time != AV_NOPTS_VALUE)
    mp_set_current_time(mp, mb->mb_time);

  /* Compute aspect ratio */
  switch(mb->mb_aspect_override) {
  case 0:

    if(frame->pan_scan != NULL && frame->pan_scan->width != 0) {
      fi.dar.num = frame->pan_scan->width;
      fi.dar.den = frame->pan_scan->height;
    } else {
      fi.dar.num = ctx->width;
      fi.dar.den = ctx->height;
    }

    if(ctx->sample_aspect_ratio.num)
      fi.dar = av_mul_q(fi.dar, ctx->sample_aspect_ratio);
    break;
  case 1:
    fi.dar = (AVRational){4,3};
    break;
  case 2:
    fi.dar = (AVRational){16,9};
    break;
  }

  int64_t pts = mb->mb_pts;

  /* Compute duration and PTS of frame */
  if(pts == AV_NOPTS_VALUE && mb->mb_dts != AV_NOPTS_VALUE &&
     (ctx->has_b_frames == 0 || frame->pict_type == FF_B_TYPE)) {
    pts = mb->mb_dts;
  }

  int duration = mb->mb_duration;

  if(!vd_valid_duration(duration)) {
    /* duration is zero or very invalid, use duration from last output */
    duration = vd->vd_estimated_duration;
  }

  if(pts == AV_NOPTS_VALUE && vd->vd_nextpts != AV_NOPTS_VALUE)
    pts = vd->vd_nextpts; /* no pts set, use estimated pts */

  if(pts != AV_NOPTS_VALUE && vd->vd_prevpts != AV_NOPTS_VALUE) {
    /* we know PTS of a prior frame */
    int64_t t = (pts - vd->vd_prevpts) / vd->vd_prevpts_cnt;

    if(vd_valid_duration(t)) {
      /* inter frame duration seems valid, store it */
      vd->vd_estimated_duration = t;
      if(duration == 0)
	duration = t;

    } else if(t < 0 || t > 10000000LL) {
      /* PTS discontinuity, use estimated PTS from last output instead */
      pts = vd->vd_nextpts;
    }
  }
  
  duration += frame->repeat_pict * duration / 2;
 
  if(pts != AV_NOPTS_VALUE) {
    vd->vd_prevpts = pts;
    vd->vd_prevpts_cnt = 0;
  }
  vd->vd_prevpts_cnt++;

  if(duration == 0) {
    TRACE(TRACE_DEBUG, "Video", "Dropping frame with duration = 0");
    return;
  }

  prop_set_int(mq->mq_prop_too_slow, decode_time > duration);

  if(pts != AV_NOPTS_VALUE) {
    vd->vd_nextpts = pts + duration;

    if(mb->mb_send_pts) {
      ets = event_create(EVENT_CURRENT_PTS, sizeof(event_ts_t));
      ets->ts = pts;
      mp_enqueue_event(mp, &ets->h);
      event_release(&ets->h);
    }

  } else {
    vd->vd_nextpts = AV_NOPTS_VALUE;
  }

  vd->vd_interlaced |=
    frame->interlaced_frame && !mb->mb_disable_deinterlacer;

  fi.width = ctx->width;
  fi.height = ctx->height;
  fi.pix_fmt = ctx->pix_fmt;
  fi.pts = pts;
  fi.epoch = mb->mb_epoch;
  fi.duration = duration;

  fi.interlaced = !!vd->vd_interlaced;
  fi.tff = !!frame->top_field_first;
  fi.prescaled = 0;

  fi.color_space = ctx->colorspace;
  fi.color_range = ctx->color_range;

  vd->vd_frame_deliver(frame->data, frame->linesize, &fi, vd->vd_opaque);

  video_decoder_scan_ext_sub(vd, fi.pts);
}


/**
 *
 */
static void
update_vbitrate(media_pipe_t *mp, media_queue_t *mq, 
		int size, video_decoder_t *vd)
{
  int i;
  int64_t sum;

  vd->vd_frame_size[vd->vd_frame_size_ptr] = size;
  vd->vd_frame_size_ptr = (vd->vd_frame_size_ptr + 1) & VD_FRAME_SIZE_MASK;

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
  int run = 1;
  int reqsize = -1;
  int reinit = 0;
  int size;
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
    mq->mq_freeze_tail = 1;
    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb->mb_size;
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
      vd->vd_interlaced = 0;
      video_overlay_flush(vd, 1);
      break;

    case MB_VIDEO:
      if(reinit) {
	reinit = 0;
	if(mc->reinit != NULL)
	  mc->reinit(mc);
      }

      if(mb->mb_skip == 2)
	vd->vd_skip = 1;

      size = mb->mb_size;

      if(mc->decode) {
	if(mc->decode(mc, vd, mq, mb, reqsize))
	  mb = NULL;
      } else
	vd_decode_video(vd, mq, mb);

      update_vbitrate(mp, mq, size, vd);
      reqsize = -1;
      break;

    case MB_REQ_OUTPUT_SIZE:
      reqsize = mb->mb_data32;
      break;

    case MB_REINITIALIZE:
      reinit = 1;
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
      if(vd->vd_ext_subtitles == NULL && mb->mb_stream == mq->mq_stream2)
	video_overlay_decode(vd, mb);
      break;

    case MB_END:
      break;

    case MB_BLACKOUT:
      if(vd->vd_accelerator_blackout)
	vd->vd_accelerator_blackout(vd->vd_accelerator_opaque);
      else
	vd->vd_frame_deliver(NULL, NULL, NULL, vd->vd_opaque);
      break;

    case MB_FLUSH_SUBTITLES:
      video_overlay_flush(vd, 1);
      break;

    case MB_EXT_SUBTITLE:
      if(vd->vd_ext_subtitles != NULL)
         subtitles_destroy(vd->vd_ext_subtitles);

      // Steal subtitle from the media_buf
      vd->vd_ext_subtitles = mb->mb_data;
      mb->mb_data = NULL; 
      video_overlay_flush(vd, 1);
      break;

    default:
      abort();
    }

    hts_mutex_lock(&mp->mp_mutex);
    if(mb != NULL) {
      mq->mq_freeze_tail--;
      media_buf_free_locked(mp, mb);
    }
      
  }

  hts_mutex_unlock(&mp->mp_mutex);

  // Stop any video accelerator helper threads 
  video_decoder_set_accelerator(vd, NULL, NULL, NULL);

  if(vd->vd_ext_subtitles != NULL)
    subtitles_destroy(vd->vd_ext_subtitles);

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

  TAILQ_INIT(&vd->vd_overlay_queue);
  hts_mutex_init(&vd->vd_overlay_mutex);

  hts_thread_create_joinable("video decoder", 
			     &vd->vd_decoder_thread, vd_thread, vd,
			     THREAD_PRIO_NORMAL);
  
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
  video_overlay_flush(vd, 0);

  hts_mutex_destroy(&vd->vd_overlay_mutex);
  free(vd);
}


/**
 *
 */
void
video_decoder_set_accelerator(video_decoder_t *vd,
			      void (*stopfn)(void *opaque),
			      void (*blackoutfn)(void *opaque),
			      void *opaque)
{
  if(vd->vd_accelerator_opaque != NULL)
    vd->vd_accelerator_stop(vd->vd_accelerator_opaque);
  
  vd->vd_accelerator_stop = stopfn;
  vd->vd_accelerator_blackout = blackoutfn;
  vd->vd_accelerator_opaque = opaque;
}


/**
 *
 */
void
video_decoder_scan_ext_sub(video_decoder_t *vd, int64_t pts)
{
  pts -= vd->vd_mp->mp_svdelta;

  if(vd->vd_ext_subtitles != NULL) {
    ext_subtitle_entry_t *ese = subtitles_pick(vd->vd_ext_subtitles, pts);
    if(ese != NULL)
      vd->vd_ext_subtitles->es_decode(vd, vd->vd_ext_subtitles, ese);
  }
}
