/*
 *  Video output on GL surfaces
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

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "video_decoder.h"
#ifdef CONFIG_DVD
#include "video_dvdspu.h"
#endif
//#include "subtitles.h"
#include "yadif.h"
#include "event.h"

static void
vd_init_timings(video_decoder_t *vd)
{
  kalman_init(&vd->vd_avfilter);
  vd->vd_lastpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;
  //  vd->vd_last_subtitle_index = -1;
}

/**
 * vd_dequeue_for_decode
 *
 * This function will return a frame with a mapped PBO that have w x h
 * according to the w[] and h[] arrays. If no widget is attached to
 * the decoder it wil return NULL.
 *
 */
static video_decoder_frame_t *
vd_dequeue_for_decode(video_decoder_t *vd, int w[3], int h[3])
{
  video_decoder_frame_t *vdf;

  hts_mutex_lock(&vd->vd_queue_mutex);
  
  while((vdf = TAILQ_FIRST(&vd->vd_avail_queue)) == NULL &&
	vd->vd_decoder_running) {
    hts_cond_wait(&vd->vd_avail_queue_cond, &vd->vd_queue_mutex);
  }

  if(!vd->vd_decoder_running) {
    hts_mutex_unlock(&vd->vd_queue_mutex);
    return NULL;
  }

  TAILQ_REMOVE(&vd->vd_avail_queue, vdf, vdf_link);

  if(vdf->vdf_width[0] == w[0] && vdf->vdf_height[0] == h[0] && 
     vdf->vdf_width[1] == w[1] && vdf->vdf_height[1] == h[1] && 
     vdf->vdf_width[2] == w[2] && vdf->vdf_height[2] == h[2] && 
     vdf->vdf_data[0] != NULL) {
    hts_mutex_unlock(&vd->vd_queue_mutex);
    return vdf;
  }

  /* Frame has not correct width / height, enqueue it for
     buffer (re-)allocation */

  vdf->vdf_width[0]  = w[0];
  vdf->vdf_height[0] = h[0];
  vdf->vdf_width[1]  = w[1];
  vdf->vdf_height[1] = h[1];
  vdf->vdf_width[2]  = w[2];
  vdf->vdf_height[2] = h[2];

  TAILQ_INSERT_TAIL(&vd->vd_bufalloc_queue, vdf, vdf_link);

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) == NULL &&
	vd->vd_decoder_running)
    hts_cond_wait(&vd->vd_bufalloced_queue_cond, &vd->vd_queue_mutex);

  if(!vd->vd_decoder_running) {
    hts_mutex_unlock(&vd->vd_queue_mutex);
    return NULL;
  }

  TAILQ_REMOVE(&vd->vd_bufalloced_queue, vdf, vdf_link);

  hts_mutex_unlock(&vd->vd_queue_mutex);

  assert(vdf->vdf_data[0] != NULL);
  return vdf;
}


static int
display_or_skip(video_decoder_t *vd, int duration)
{
  return 1;
}

typedef struct {
  int refcount;
  int64_t pts;
  int64_t dts;
  int duration;
  int stream;
  int64_t time;
} frame_meta_t;


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
  fm->stream = mb->mb_stream;
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
vd_decode_video(video_decoder_t *vd, media_buf_t *mb, int justdecode)
{
  video_decoder_frame_t *vdf;
  int64_t pts, t;
  int i, j, got_pic, h, w, duration;
  media_pipe_t *mp = vd->vd_mp;
  unsigned char *src, *dst;
  float f;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = vd->vd_frame;
  frame_meta_t *fm;
  time_t now;
  int hvec[3], wvec[3];
  int tff, w2, mode;
  uint8_t *prev, *cur, *next;
  int hshift, vshift;
  deilace_type_t dt;

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

  time(&now);

  ctx->opaque = mb;
  ctx->get_buffer = vd_get_buffer;
  ctx->release_buffer = vd_release_buffer;

  /*
   * If we are seeking, drop any non-reference frames
   */
  ctx->skip_frame = justdecode ? AVDISCARD_NONREF : AVDISCARD_NONE;


  avcodec_decode_video(ctx, frame, &got_pic, mb->mb_data, mb->mb_size);

  if(got_pic == 0)
    return;

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


  dt = mb->mb_disable_deinterlacer ? VD_DEILACE_NONE : vd->vd_deilace_conf;
  if(dt == VD_DEILACE_AUTO)
    dt = frame->interlaced_frame ? VD_DEILACE_YADIF_FIELD : VD_DEILACE_NONE;
  
  /* Compute duration and PTS of frame */

  fm = frame->opaque;
  assert(fm != NULL);

  if(fm->time != AV_NOPTS_VALUE)
    mp_set_current_time(mp, fm->time);

  pts = fm->pts;
  duration = fm->duration;

  if(!vd_valid_duration(duration)) {
    /* duration is zero or very invalid, use duration from last output */
    duration = vd->vd_estimated_duration;
  }

  if(pts == AV_NOPTS_VALUE && vd->vd_nextpts != AV_NOPTS_VALUE)
    pts = vd->vd_nextpts; /* no pts set, use estimated pts */

  if(pts != AV_NOPTS_VALUE && vd->vd_lastpts != AV_NOPTS_VALUE) {
    /* we know pts of last frame */
    t = pts - vd->vd_lastpts;

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
    vd->vd_nextpts = pts + duration;
  }

  if(duration == 0 || pts == AV_NOPTS_VALUE)
    return;

#if 0
  if(vd->vd_mp->mp_subtitles) {
    i = subtitles_index_by_pts(vd->vd_mp->mp_subtitles, pts);
    if(i != vd->vd_last_subtitle_index) {

      if(vd->vd_subtitle_widget != NULL)
	glw_destroy(vd->vd_subtitle_widget);
      
      vd->vd_subtitle_widget =
	subtitles_make_widget(vd->vd_mp->mp_subtitles, i);
      abort();
      vd->vd_last_subtitle_index = i;
    }
  }
#endif

  /* deinterlacer will generate two frames */

  if(dt == VD_DEILACE_HALF_RES)
    duration /= 2;

  avcodec_get_chroma_sub_sample(ctx->pix_fmt, &hshift, &vshift);

  wvec[0] = ctx->width;
  wvec[1] = ctx->width >> hshift;
  wvec[2] = ctx->width >> hshift;
  hvec[0] = ctx->height;
  hvec[1] = ctx->height >> vshift;
  hvec[2] = ctx->height >> vshift;

  switch(dt) {

  case VD_DEILACE_AUTO:
    return;

    /*
     *  No post processing
     */

  case VD_DEILACE_NONE:
    vd->vd_active_frames_needed = 3;
    vd->vd_interlaced = 0;
    if(!display_or_skip(vd, duration))
      return;

    if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      h = vdf->vdf_height[i];
      w = vdf->vdf_width[i];
      
      src = frame->data[i];
      dst = vdf->vdf_data[i];
 
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }
    
    vd->vd_interlaced = 0;
    vdf->vdf_pts = pts;
    vdf->vdf_duration = duration;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    return;

  case VD_DEILACE_HALF_RES:
    tff = !!frame->top_field_first ^ vd->vd_field_parity;

    vd->vd_active_frames_needed = 3;

    /*
     *  Deinterlace by 2 x framerate and 0.5 * y-res,
     *  OpenGL does bledning for us
     */

    vd->vd_interlaced = 1;

    hvec[0] = hvec[0] / 2;
    hvec[1] = hvec[1] / 2;
    hvec[2] = hvec[2] / 2;

    if(display_or_skip(vd, duration)) {
      if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {

	src = frame->data[i]; 
	dst = vdf->vdf_data[i];
	h = vdf->vdf_height[i];
	w = vdf->vdf_width[i];

	while(h -= 2 > 0) {
	  memcpy(dst, src, w);
	  dst += w;
	  src += frame->linesize[i] * 2;
	}
      }

      vdf->vdf_debob = !tff;
      
      vdf->vdf_pts = pts;
      vdf->vdf_duration = duration;
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    }

    if(display_or_skip(vd, duration)) {

      if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
      
	src = frame->data[i] + frame->linesize[i];
	dst = vdf->vdf_data[i];
	h = vdf->vdf_height[i];
	w = vdf->vdf_width[i];
	
	while(h -= 2 > 0) {
	  memcpy(dst, src, w);
	  dst += w;
	  src += frame->linesize[i] * 2;
	}
      }

      vdf->vdf_debob = tff;

      vdf->vdf_pts = pts + duration;
      vdf->vdf_duration = duration;
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    }
    return;
    
  case VD_DEILACE_YADIF_FRAME:
    mode = 0; goto yadif;
  case VD_DEILACE_YADIF_FIELD:
    mode = 1; goto yadif;
  case VD_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE:
    mode = 2; goto yadif;
  case VD_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE:
    mode = 3;
  yadif:
    if(vd->vd_yadif_width   != ctx->width  ||
       vd->vd_yadif_height  != ctx->height ||
       vd->vd_yadif_pix_fmt != ctx->pix_fmt) {
      
      vd->vd_yadif_width   = ctx->width;
      vd->vd_yadif_height  = ctx->height;
      vd->vd_yadif_pix_fmt = ctx->pix_fmt;

      for(i = 0; i < 3; i++) {
	avpicture_free(&vd->vd_yadif_pic[i]);
	avpicture_alloc(&vd->vd_yadif_pic[i], ctx->pix_fmt, 
			ctx->width, ctx->height);
      }
    }

    vd->vd_active_frames_needed = 3;
    vd->vd_interlaced = 1;

    for(i = 0; i < 3; i++) {
      w = vd->vd_yadif_width  >> (i ? hshift : 0);
      h = vd->vd_yadif_height >> (i ? vshift : 0);
      src = frame->data[i];
      dst = vd->vd_yadif_pic[vd->vd_yadif_phase].data[i];
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }

    if(!display_or_skip(vd, duration))
      return;

    tff = !!frame->top_field_first ^ vd->vd_field_parity;

    pts -= duration;

    if(mode & 1) 
      duration /= 2;

    for(j = 0; j <= (mode & 1); j++) {

      if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
	int y;
	int parity = j ^ tff ^ 1;

	h = vd->vd_yadif_phase;
	next = vd->vd_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	cur = vd->vd_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	prev = vd->vd_yadif_pic[h].data[i];

	dst = vdf->vdf_data[i];
	h = vdf->vdf_height[i];
	w = vdf->vdf_width[i];
	w2 = vd->vd_yadif_width >> (i ? hshift : 0);

	for(y = 0; y < 2; y++) {
	  memcpy(dst, cur, w);
	  dst  += w; prev += w2; cur += w2; next += w2;
	}

	for(; y < h - 2; y++) {

	  if((y ^ parity) & 1) {
	    yadif_filter_line(mode, dst, prev, cur, next, w, w2, parity ^ tff);
	  } else {
	    memcpy(dst, cur, w);
	  }
	  dst  += w; prev += w2; cur += w2; next += w2;
	}

	for(; y < h; y++) {
	  memcpy(dst, cur, w);
	  dst  += w; prev += w2; cur += w2; next += w2;
	}
      }

      /* XXXX: Ugly */
#if defined(__i386__) || defined(__x86_64__)
      asm volatile("emms \n\t" : : : "memory");
#endif

      vdf->vdf_pts = pts + j * duration;
      vdf->vdf_duration = duration;
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    }

    vd->vd_yadif_phase++;
    if(vd->vd_yadif_phase > 2)
      vd->vd_yadif_phase = 0;
    return;
  }
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
  int i;
  int run = 1;

  vd->vd_frame = avcodec_alloc_frame();

  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    if((mb = TAILQ_FIRST(&mq->mq_q)) == NULL) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    if(mb->mb_data_type == MB_VIDEO && vd->vd_hold) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_len--;
    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

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
      break;

    case MB_VIDEO:
      vd_decode_video(vd, mb, 0);
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
video_decoder_create(media_pipe_t *mp)
{
  video_decoder_t *vd = calloc(1, sizeof(video_decoder_t));

  vd->vd_mp = mp;
  vd->vd_decoder_running = 1;

  vd_init_timings(vd);

  /* For the exact meaning of these, see gl_video.h */
    
  TAILQ_INIT(&vd->vd_avail_queue);
  TAILQ_INIT(&vd->vd_displaying_queue);
  TAILQ_INIT(&vd->vd_display_queue);
  TAILQ_INIT(&vd->vd_bufalloc_queue);
  TAILQ_INIT(&vd->vd_bufalloced_queue);

  hts_cond_init(&vd->vd_avail_queue_cond);
  hts_cond_init(&vd->vd_bufalloced_queue_cond);
  hts_mutex_init(&vd->vd_queue_mutex);

  hts_thread_create_joinable(&vd->vd_decoder_thread, vd_thread, vd);
  
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

  hts_mutex_lock(&vd->vd_queue_mutex);
  vd->vd_decoder_running = 0;
  hts_cond_signal(&vd->vd_avail_queue_cond);
  hts_cond_signal(&vd->vd_bufalloced_queue_cond);
  hts_mutex_unlock(&vd->vd_queue_mutex);

  hts_thread_join(&vd->vd_decoder_thread);
  vd->vd_mp = NULL;
}


/**
 *
 */
void
video_decoder_destroy(video_decoder_t *vd)
{
#ifdef CONFIG_DVD
  if(vd->vd_dvdspu != NULL)
    dvdspu_decoder_destroy(vd->vd_dvdspu);
#endif

  assert(TAILQ_FIRST(&vd->vd_avail_queue) == NULL);
  assert(TAILQ_FIRST(&vd->vd_displaying_queue) == NULL);
  assert(TAILQ_FIRST(&vd->vd_display_queue) == NULL);
  assert(TAILQ_FIRST(&vd->vd_bufalloc_queue) == NULL);
  assert(TAILQ_FIRST(&vd->vd_bufalloced_queue) == NULL);

  hts_cond_destroy(&vd->vd_avail_queue_cond);
  hts_cond_destroy(&vd->vd_bufalloced_queue_cond);
  hts_mutex_destroy(&vd->vd_queue_mutex);
  free(vd);
}
