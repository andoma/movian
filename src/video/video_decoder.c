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
#include "dvdspu.h"

static void
vd_init_timings(video_decoder_t *vd)
{
  kalman_init(&vd->vd_avfilter);
  vd->vd_prevpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;
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

  vd->vd_reorder[vd->vd_reorder_ptr] = *mb;
  ctx->reordered_opaque = vd->vd_reorder_ptr;
  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;

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

  mb = &vd->vd_reorder[frame->reordered_opaque];

  if(got_pic == 0 || mb->mb_skip == 1) 
    return;

  video_deliver_frame_avctx(vd, mp, mq, ctx, frame, mb, t);
}

/**
 *
 */
void
video_deliver_frame_avctx(video_decoder_t *vd,
			  media_pipe_t *mp, media_queue_t *mq,
			  AVCodecContext *ctx, AVFrame *frame,
			  const media_buf_t *mb, int decode_time)
{
  frame_info_t fi;
#if 0
  if(mb->mb_time != AV_NOPTS_VALUE)
    mp_set_current_time(mp, mb->mb_time);
#endif
  /* Compute aspect ratio */
  switch(mb->mb_aspect_override) {
  case 0:

    if(frame->pan_scan != NULL && frame->pan_scan->width != 0) {
      fi.fi_dar.num = frame->pan_scan->width;
      fi.fi_dar.den = frame->pan_scan->height;
    } else {
      fi.fi_dar.num = ctx->width;
      fi.fi_dar.den = ctx->height;
    }

    if(ctx->sample_aspect_ratio.num)
      fi.fi_dar = av_mul_q(fi.fi_dar, ctx->sample_aspect_ratio);
    break;
  case 1:
    fi.fi_dar = (AVRational){4,3};
    break;
  case 2:
    fi.fi_dar = (AVRational){16,9};
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
  } else {
    vd->vd_nextpts = AV_NOPTS_VALUE;
  }

  vd->vd_interlaced |=
    frame->interlaced_frame && !mb->mb_disable_deinterlacer;

  fi.fi_width = ctx->width;
  fi.fi_height = ctx->height;
  fi.fi_pix_fmt = ctx->pix_fmt;
  fi.fi_pts = pts;
  fi.fi_epoch = mb->mb_epoch;
  fi.fi_time = mb->mb_time;
  fi.fi_duration = duration;

  fi.fi_interlaced = !!vd->vd_interlaced;
  fi.fi_tff = !!frame->top_field_first;
  fi.fi_prescaled = 0;

  fi.fi_color_space = ctx->colorspace;
  fi.fi_color_range = ctx->color_range;

  video_deliver_frame(vd, FRAME_BUFFER_TYPE_LIBAV_FRAME, frame, &fi);
}


/**
 *
 */
void
video_deliver_frame(video_decoder_t *vd, frame_buffer_type_t type, void *frame,
		    const frame_info_t *info)
{
  vd->vd_skip = 0;
  mp_set_current_time(vd->vd_mp, info->fi_time, info->fi_epoch);

  vd->vd_frame_deliver(type, frame, info, vd->vd_opaque);
  
  video_decoder_scan_ext_sub(vd, info->fi_time);
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
      dvdspu_flush(vd);
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

      if(mc->decode)
	mc->decode(mc, vd, mq, mb, reqsize);
      else
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

#if ENABLE_DVD
    case MB_DVD_RESET_SPU:
      vd->vd_spu_curbut = 1;
      dvdspu_flush(vd);
      break;

    case MB_DVD_HILITE:
      vd->vd_spu_curbut = mb->mb_data32;
      vd->vd_spu_repaint = 1;
      break;

    case MB_DVD_PCI:
      memcpy(&vd->vd_pci, mb->mb_data, sizeof(pci_t));
      vd->vd_spu_repaint = 1;
      event_t *e = event_create(EVENT_DVD_PCI, sizeof(event_t) + sizeof(pci_t));
      memcpy(e->e_payload, mb->mb_data, sizeof(pci_t));
      mp_enqueue_event(mp, e);
      event_release(e);
      break;

    case MB_DVD_CLUT:
      dvdspu_decode_clut(vd->vd_dvd_clut, mb->mb_data);
      break;

    case MB_DVD_SPU:
      dvdspu_enqueue(vd, mb->mb_data, mb->mb_size, 
		     vd->vd_dvd_clut, 0, 0, mb->mb_pts);
      break;
#endif

    case MB_DVD_SPU2:
      dvdspu_enqueue(vd, mb->mb_data+72, mb->mb_size-72,
		     mb->mb_data,
		     ((const uint32_t *)mb->mb_data)[16],
		     ((const uint32_t *)mb->mb_data)[17],
		     mb->mb_pts);
      break;
      


    case MB_SUBTITLE:
      if(vd->vd_ext_subtitles == NULL && mb->mb_stream == mq->mq_stream2)
	video_overlay_decode(vd, mb);
      break;

    case MB_END:
      break;

    case MB_BLACKOUT:
      vd->vd_frame_deliver(FRAME_BUFFER_TYPE_BLACKOUT, NULL, NULL,
			   vd->vd_opaque);
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
    media_buf_free_locked(mp, mb);
  }

  hts_mutex_unlock(&mp->mp_mutex);

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

  vd_init_timings(vd);

  TAILQ_INIT(&vd->vd_spu_queue);
  hts_mutex_init(&vd->vd_spu_mutex);

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
  dvdspu_t *d;

  while((d = TAILQ_FIRST(&vd->vd_spu_queue)) != NULL)
    dvdspu_destroy(vd, d);

  hts_mutex_destroy(&vd->vd_spu_mutex);

  video_overlay_flush(vd, 0);

  hts_mutex_destroy(&vd->vd_overlay_mutex);
  free(vd);
}


/**
 *
 */
void
video_decoder_scan_ext_sub(video_decoder_t *vd, int64_t pts)
{
  pts -= vd->vd_mp->mp_svdelta;
  pts -= vd->vd_mp->mp_start_time;

  if(vd->vd_ext_subtitles != NULL)
    subtitles_pick(vd->vd_ext_subtitles, pts, vd);
}
