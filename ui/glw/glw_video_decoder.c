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
#include <pthread.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "media.h"
#include "glw_video.h"
//#include "subtitles.h"
#include "video/yadif.h"
#include "event.h"

void
gv_init_timings(glw_video_t *gv)
{
  gv_kalman_init(&gv->gv_avfilter);
  gv->gv_lastpts = AV_NOPTS_VALUE;
  gv->gv_nextpts = AV_NOPTS_VALUE;
  gv->gv_estimated_duration = 0;
  gv->gv_last_subtitle_index = -1;
}

/*
 * gv_dequeue_for_decode
 *
 * This function will return a frame with a mapped PBO that have w x h
 * according to the w[] and h[] arrays. If no widget is attached to
 * the decoder it wil return NULL.
 *
 */

static gl_video_frame_t *
gv_dequeue_for_decode(glw_video_t *gv, int w[3], int h[3])
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&gv->gv_queue_mutex);
  
  while((gvf = TAILQ_FIRST(&gv->gv_avail_queue)) == NULL &&
	gv->gv_run_decoder) {
    pthread_cond_wait(&gv->gv_avail_queue_cond, &gv->gv_queue_mutex);
  }

  if(gvf == NULL) {
  fail:
    pthread_mutex_unlock(&gv->gv_queue_mutex);
    return NULL;
  }

  TAILQ_REMOVE(&gv->gv_avail_queue, gvf, link);

  if(gvf->gvf_width[0] == w[0] && gvf->gvf_height[0] == h[0] && 
     gvf->gvf_width[1] == w[1] && gvf->gvf_height[1] == h[1] && 
     gvf->gvf_width[2] == w[2] && gvf->gvf_height[2] == h[2] && 
     gvf->gvf_pbo_ptr != NULL) {
    pthread_mutex_unlock(&gv->gv_queue_mutex);
    return gvf;
  }

  /* Frame has not correct width / height, enqueue it for
     buffer (re-)allocation */

  gvf->gvf_width[0]  = w[0];
  gvf->gvf_height[0] = h[0];
  gvf->gvf_width[1]  = w[1];
  gvf->gvf_height[1] = h[1];
  gvf->gvf_width[2]  = w[2];
  gvf->gvf_height[2] = h[2];

  TAILQ_INSERT_TAIL(&gv->gv_bufalloc_queue, gvf, link);

  while((gvf = TAILQ_FIRST(&gv->gv_bufalloced_queue)) == NULL &&
	gv->gv_run_decoder)
    pthread_cond_wait(&gv->gv_bufalloced_queue_cond, &gv->gv_queue_mutex);

  if(gvf == NULL)
    goto fail;

  TAILQ_REMOVE(&gv->gv_bufalloced_queue, gvf, link);

  pthread_mutex_unlock(&gv->gv_queue_mutex);

  assert(gvf->gvf_pbo_ptr != NULL);
  return gvf;
}


static int
display_or_skip(glw_video_t *gv, int duration)
{
  return 1;
}

typedef struct {
  int refcount;
  int64_t pts;
  int64_t dts;
  int duration;
  int stream;
} frame_meta_t;


static int
gv_get_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  media_buf_t *mb = c->opaque;
  int ret = avcodec_default_get_buffer(c, pic);
  frame_meta_t *fm = malloc(sizeof(frame_meta_t));

  fm->pts = mb->mb_pts;
  fm->dts = mb->mb_dts;
  fm->duration = mb->mb_duration;
  fm->stream = mb->mb_stream;
  pic->opaque = fm;
  return ret;
}

static void
gv_release_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  frame_meta_t *fm = pic->opaque;

  if(fm != NULL)
    free(fm);

  avcodec_default_release_buffer(c, pic);
}


#define gv_valid_duration(t) ((t) > 1000ULL && (t) < 10000000ULL)

static void 
gv_decode_video(glw_video_t *gv, media_buf_t *mb)
{
  gl_video_frame_t *gvf;
  int64_t pts, t;
  int i, j, got_pic, h, w, duration;
  media_pipe_t *mp = gv->gv_mp;
  unsigned char *src, *dst;
  float f;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = gv->gv_frame;
  frame_meta_t *fm;
  time_t now;
  int hvec[3], wvec[3];
  int tff, w2, mode;
  uint8_t *prev, *cur, *next;
  int hshift, vshift;

  got_pic = 0;

  if(gv->gv_do_flush) {
    do {
      avcodec_decode_video(ctx, frame, &got_pic, NULL, 0);
    } while(got_pic);

    gv->gv_do_flush = 0;
    gv->gv_lastpts = AV_NOPTS_VALUE;
    gv->gv_estimated_duration = 0;
    avcodec_flush_buffers(ctx);
    gv->gv_compensate_thres = 5;
  }

  time(&now);

  ctx->opaque = mb;
  ctx->get_buffer = gv_get_buffer;
  ctx->release_buffer = gv_release_buffer;

  /*
   * If we are seeking, drop any non-reference frames
   */
  if((mp->mp_playstatus == MP_VIDEOSEEK_PLAY || 
      mp->mp_playstatus == MP_VIDEOSEEK_PAUSE) &&
     mb->mb_dts < mp->mp_videoseekdts)
    ctx->skip_frame = AVDISCARD_NONREF;
  else
    ctx->skip_frame = AVDISCARD_NONE;

  avcodec_decode_video(ctx, frame, &got_pic, mb->mb_data, mb->mb_size);

  if(got_pic == 0)
    return;

  gv->gv_codectxt = ctx->codec->name;


  /* Update aspect ratio */

  switch(mb->mb_aspect_override) {
  case 0:

    if(frame->pan_scan != NULL && frame->pan_scan->width != 0)
      f = (float)frame->pan_scan->width / (float)frame->pan_scan->height;
    else
      f = (float)ctx->width / (float)ctx->height;
    
    gv->gv_aspect = (av_q2d(ctx->sample_aspect_ratio) ?: 1) * f;
    break;
  case 1:
    gv->gv_aspect = (4.0f / 3.0f);
    break;
  case 2:
    gv->gv_aspect = (16.0f / 9.0f);
    break;
  }

  if(gv->gv_deilace_conf == GV_DEILACE_AUTO) {
    
    if(frame->interlaced_frame)
      gv->gv_deilace_type = GV_DEILACE_HALF_RES;
    else
      gv->gv_deilace_type = GV_DEILACE_NONE;
  } else {
    gv->gv_deilace_type = gv->gv_deilace_conf;
  }
  
  /* Compute duration and PTS of frame */

  fm = frame->opaque;
  assert(fm != NULL);

  if(mp->mp_playstatus == MP_VIDEOSEEK_PLAY || 
     mp->mp_playstatus == MP_VIDEOSEEK_PAUSE) {
    if(fm->dts < mp->mp_videoseekdts)
      return;

    if(mp->mp_playstatus == MP_VIDEOSEEK_PAUSE) {
      mp_set_playstatus(mp, MP_PAUSE, 0);
    } else {
      mp_set_playstatus(mp, MP_PLAY, 0);
    }
  }

  if(mp->mp_feedback != NULL) {
    event_ts_t *et;

    et = event_create(EVENT_VIDEO_CLOCK, sizeof(event_ts_t));
    et->dts    = fm->dts;
    et->pts    = fm->pts;
    et->stream = fm->stream;

    event_enqueue(mp->mp_feedback, &et->h);
    event_unref(&et->h);
  }

  pts = fm->pts;
  duration = fm->duration;

  if(!gv_valid_duration(duration)) {
    /* duration is zero or very invalid, use duration from last output */
    duration = gv->gv_estimated_duration;
  }

  if(pts == AV_NOPTS_VALUE && gv->gv_nextpts != AV_NOPTS_VALUE)
    pts = gv->gv_nextpts; /* no pts set, use estimated pts */

  if(pts != AV_NOPTS_VALUE && gv->gv_lastpts != AV_NOPTS_VALUE) {
    /* we know pts of last frame */
    t = pts - gv->gv_lastpts;

    if(gv_valid_duration(t)) {
      /* inter frame duration seems valid, store it */
      gv->gv_estimated_duration = t;
      if(duration == 0)
	duration = t;

    } else if(t < 0 || t > 10000000LL) {
      /* crazy pts jump, use estimated pts from last output instead */
      pts = gv->gv_nextpts;
    }
  }
  
  /* compensate for frame repeat */
  
  duration += frame->repeat_pict * (duration * 0.5);
 
  if(pts != AV_NOPTS_VALUE) {
    gv->gv_lastpts = pts;
    gv->gv_nextpts = pts + duration;
  }

  if(duration == 0 || pts == AV_NOPTS_VALUE)
    return;

#if 0
  if(gv->gv_mp->mp_subtitles) {
    i = subtitles_index_by_pts(gv->gv_mp->mp_subtitles, pts);
    if(i != gv->gv_last_subtitle_index) {

      if(gv->gv_subtitle_widget != NULL)
	glw_destroy(gv->gv_subtitle_widget);
      
      gv->gv_subtitle_widget =
	subtitles_make_widget(gv->gv_mp->mp_subtitles, i);
      abort();
      gv->gv_last_subtitle_index = i;
    }
  }
#endif

  /* deinterlacer will generate two frames */

  if(gv->gv_deilace_type == GV_DEILACE_HALF_RES)
    duration /= 2;

  duration = (float)duration / mp->mp_speed_gain;

  if(mp_get_playstatus(mp) == MP_STOP)
    return;


  avcodec_get_chroma_sub_sample(ctx->pix_fmt, &hshift, &vshift);

  wvec[0] = ctx->width;
  wvec[1] = ctx->width >> hshift;
  wvec[2] = ctx->width >> hshift;
  hvec[0] = ctx->height;
  hvec[1] = ctx->height >> vshift;
  hvec[2] = ctx->height >> vshift;

  switch(gv->gv_deilace_type) {

  case GV_DEILACE_AUTO:
    return;

    /*
     *  No post processing
     */

  case GV_DEILACE_NONE:
    gv->gv_active_frames_needed = 3;
    gv->gv_interlaced = 0;
    if(!display_or_skip(gv, duration))
      return;

    if((gvf = gv_dequeue_for_decode(gv, wvec, hvec)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      h = gvf->gvf_height[i];
      w = gvf->gvf_width[i];
      
      src = frame->data[i];
      dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
 
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }
    
    gv->gv_interlaced = 0;
    gvf->gvf_pts = pts;
    gvf->gvf_duration = duration;
    TAILQ_INSERT_TAIL(&gv->gv_display_queue, gvf, link);
    return;

  case GV_DEILACE_HALF_RES:
    tff = !!frame->top_field_first ^ gv->gv_field_parity;

    gv->gv_active_frames_needed = 3;

    /*
     *  Deinterlace by 2 x framerate and 0.5 * y-res,
     *  OpenGL does bledning for us
     */

    gv->gv_interlaced = 1;

    hvec[0] = hvec[0] / 2;
    hvec[1] = hvec[1] / 2;
    hvec[2] = hvec[2] / 2;

    if(display_or_skip(gv, duration)) {
      if((gvf = gv_dequeue_for_decode(gv, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {

	src = frame->data[i]; 
	dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
	h = gvf->gvf_height[i];
	w = gvf->gvf_width[i];

	while(h -= 2 > 0) {
	  memcpy(dst, src, w);
	  dst += w;
	  src += frame->linesize[i] * 2;
	}
      }

      gvf->gvf_debob = !tff;
      
      gvf->gvf_pts = pts;
      gvf->gvf_duration = duration;
      TAILQ_INSERT_TAIL(&gv->gv_display_queue, gvf, link);
    }

    if(display_or_skip(gv, duration)) {

      if((gvf = gv_dequeue_for_decode(gv, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
      
	src = frame->data[i] + frame->linesize[i];
	dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
	h = gvf->gvf_height[i];
	w = gvf->gvf_width[i];
	
	while(h -= 2 > 0) {
	  memcpy(dst, src, w);
	  dst += w;
	  src += frame->linesize[i] * 2;
	}
      }

      gvf->gvf_debob = tff;

      gvf->gvf_pts = pts + duration;
      gvf->gvf_duration = duration;
      TAILQ_INSERT_TAIL(&gv->gv_display_queue, gvf, link);
    }
    return;
    
  case GV_DEILACE_YADIF_FRAME:
    mode = 0; goto yadif;
  case GV_DEILACE_YADIF_FIELD:
    mode = 1; goto yadif;
  case GV_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE:
    mode = 2; goto yadif;
  case GV_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE:
    mode = 3;
  yadif:
    if(gv->gv_yadif_width   != ctx->width  ||
       gv->gv_yadif_height  != ctx->height ||
       gv->gv_yadif_pix_fmt != ctx->pix_fmt) {
      
      gv->gv_yadif_width   = ctx->width;
      gv->gv_yadif_height  = ctx->height;
      gv->gv_yadif_pix_fmt = ctx->pix_fmt;

      for(i = 0; i < 3; i++) {
	avpicture_free(&gv->gv_yadif_pic[i]);
	avpicture_alloc(&gv->gv_yadif_pic[i], ctx->pix_fmt, 
			ctx->width, ctx->height);
      }
    }

    gv->gv_active_frames_needed = 3;
    gv->gv_interlaced = 1;

    for(i = 0; i < 3; i++) {
      w = gv->gv_yadif_width  >> (i ? hshift : 0);
      h = gv->gv_yadif_height >> (i ? vshift : 0);
      src = frame->data[i];
      dst = gv->gv_yadif_pic[gv->gv_yadif_phase].data[i];
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }

    if(!display_or_skip(gv, duration))
      return;

    tff = !!frame->top_field_first ^ gv->gv_field_parity;

    if(mode & 1) 
      duration /= 2;

    for(j = 0; j <= (mode & 1); j++) {

      if((gvf = gv_dequeue_for_decode(gv, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
	int y;
	int parity = j ^ tff ^ 1;

	h = gv->gv_yadif_phase;
	next = gv->gv_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	cur = gv->gv_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	prev = gv->gv_yadif_pic[h].data[i];

	dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
	h = gvf->gvf_height[i];
	w = gvf->gvf_width[i];
	w2 = gv->gv_yadif_width >> (i ? hshift : 0);

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

      asm volatile("emms \n\t" : : : "memory");
      gvf->gvf_pts = pts + j * duration;
      gvf->gvf_duration = duration;
      TAILQ_INSERT_TAIL(&gv->gv_display_queue, gvf, link);
    }

    gv->gv_yadif_phase++;
    if(gv->gv_yadif_phase > 2)
      gv->gv_yadif_phase = 0;
    return;
  }
}
















/**
 * Video decoder thread
 */
static void *
gv_thread(void *aux)
{
  glw_video_t *gv = aux;
  media_pipe_t *mp = gv->gv_mp;
  media_queue_t *mq = &mp->mp_video;
  media_buf_t *mb;
  int i;


  hts_mutex_lock(&mp->mp_mutex);

  while(1) {

    mb = TAILQ_FIRST(&mq->mq_q);

    if(mb != NULL && mb->mb_data_type == MB_EXIT)
      break;

    if(mp->mp_playstatus <= MP_PLAY || mb == NULL)
      continue;

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_len--;
    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

    switch(mb->mb_data_type) {

    case MB_VIDEO:
      gv_decode_video(gv, mb);
      break;

    case MB_RESET:
      gv_init_timings(gv);
      gv->gv_do_flush = 1;
      /* FALLTHRU */

    case MB_RESET_SPU:
#if 0
      if(gv->gv_dvdspu != NULL)
	gl_dvdspu_flush(gv->gv_dvdspu);
#endif
      break;

    case MB_DVD_SPU:
    case MB_CLUT:
    case MB_DVD_PCI:
    case MB_DVD_HILITE:
#if 0
      if(gv->gv_dvdspu != NULL)
	gl_dvdspu_dispatch(gv->gv_dvd, gv->gv_dvdspu, mb);
#endif
      break;

    default:
      abort();
    }

    media_buf_free(mb);
    hts_mutex_lock(&mp->mp_mutex);
  }


  /* Flush any remaining packets in video decoder queue */
  mq_flush(mq);

  /* Wakeup any stalled sender */
  hts_cond_signal(&mp->mp_backpressure);


  hts_mutex_unlock(&mp->mp_mutex);

  /* Free YADIF frames */
  if(gv->gv_yadif_width)
    for(i = 0; i < 3; i++)
      avpicture_free(&gv->gv_yadif_pic[i]);

  /* Free ffmpeg frame */
  av_free(gv->gv_frame);

  /* Drop reference to mediapipe */
  mp_unref(mp);

  free(gv);
  return NULL;
}




/**
 *
 */
void
glw_video_boot_decoder(glw_video_t *gv, media_pipe_t *mp)
{
  mp_ref(mp);
  gv->gv_mp = mp;
  gv->gv_run_decoder = 1;

  gv->gv_frame = avcodec_alloc_frame();
  hts_thread_create_detached(&gv->gv_ptid, gv_thread, gv);
}

#if 0
void
gv_set_dvd(media_pipe_t *mp, struct dvd_player *dvd)
{
  glw_video_t *gv = mp->mp_video_decoder;
  gv->gv_dvd = dvd;
}
#endif
