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
#include "video_decoder.h"
#include "input.h"
#include "miw.h"
#include "subtitles.h"
#include "yadif.h"

void
vd_init_timings(video_decoder_t *vd)
{
  vd_kalman_init(&vd->vd_avfilter);
  vd->vd_lastpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;
  vd->vd_last_subtitle_index = -1;
}


void 
vd_conf_init(vd_conf_t *gc)
{
  memset(gc, 0, sizeof(vd_conf_t));
  gc->gc_deilace_type = VD_DEILACE_AUTO;
  gc->gc_zoom = 100;
}



/*
 * vd_dequeue_for_decode
 *
 * This function will return a frame with a mapped PBO that have w x h
 * according to the w[] and h[] arrays. If no widget is attached to
 * the decoder it wil return NULL.
 *
 */

static gl_video_frame_t *
vd_dequeue_for_decode(video_decoder_t *vd, int w[3], int h[3])
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&vd->vd_queue_mutex);
  
  while((gvf = TAILQ_FIRST(&vd->vd_avail_queue)) == NULL &&
	vd->vd_widget != NULL) {
    pthread_cond_wait(&vd->vd_avail_queue_cond, &vd->vd_queue_mutex);
  }

  if(gvf == NULL) {
  fail:
    pthread_mutex_unlock(&vd->vd_queue_mutex);
    return NULL;
  }

  TAILQ_REMOVE(&vd->vd_avail_queue, gvf, link);

  if(gvf->gvf_width[0] == w[0] && gvf->gvf_height[0] == h[0] && 
     gvf->gvf_width[1] == w[1] && gvf->gvf_height[1] == h[1] && 
     gvf->gvf_width[2] == w[2] && gvf->gvf_height[2] == h[2] && 
     gvf->gvf_pbo_ptr != NULL) {
    pthread_mutex_unlock(&vd->vd_queue_mutex);
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

  TAILQ_INSERT_TAIL(&vd->vd_bufalloc_queue, gvf, link);

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) == NULL &&
	vd->vd_widget != NULL)
    pthread_cond_wait(&vd->vd_bufalloced_queue_cond, &vd->vd_queue_mutex);

  if(gvf == NULL)
    goto fail;

  TAILQ_REMOVE(&vd->vd_bufalloced_queue, gvf, link);

  pthread_mutex_unlock(&vd->vd_queue_mutex);

  assert(gvf->gvf_pbo_ptr != NULL);
  return gvf;
}


static int
display_or_skip(video_decoder_t *vd, int duration)
{
  int r = 1;

  if(vd->vd_spill == 0)
    return 1;

  pthread_mutex_lock(&vd->vd_spill_mutex);

  if(vd->vd_spill > duration) {
    vd->vd_spill -= duration;
    r = 0;
  }
  pthread_mutex_unlock(&vd->vd_spill_mutex);
  return r;
}

typedef struct {
  int refcount;
  int64_t pts;
  int duration;

} frame_meta_t;


static int
vd_get_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  media_buf_t *mb = c->opaque;
  int ret = avcodec_default_get_buffer(c, pic);
  frame_meta_t *fm = malloc(sizeof(frame_meta_t));

  fm->pts = mb->mb_pts;
  fm->duration = mb->mb_duration;
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
vd_decode_video(video_decoder_t *vd, media_buf_t *mb)
{
  gl_video_frame_t *gvf;
  int64_t pts, t;
  int i, j, got_pic, h, w, duration;
  media_pipe_t *mp = vd->vd_mp;
  unsigned char *src, *dst;
  float f;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = vd->vd_frame;
  vd_conf_t *gc = mp->mp_video_conf;
  frame_meta_t *fm;
  media_queue_t *mq = &mp->mp_video;
  time_t now;
  int hvec[3], wvec[3];
  int tff, w2, mode;
  uint8_t *prev, *cur, *next;
  int hshift, vshift;

  got_pic = 0;

  wrap_lock_codec(cw);

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
  if(now != mq->mq_info_last_time) {
    mq->mq_info_rate = mq->mq_info_rate_acc / 125;
    mq->mq_info_last_time = now;
    mq->mq_info_rate_acc = 0;
  }
  mq->mq_info_rate_acc += mb->mb_size;

  ctx->opaque = mb;
  ctx->get_buffer = vd_get_buffer;
  ctx->release_buffer = vd_release_buffer;

  avcodec_decode_video(ctx, frame, &got_pic, mb->mb_data, mb->mb_size);

  nice_codec_name(mq->mq_info_codec, sizeof(mq->mq_info_codec), ctx);
  
  wrap_unlock_codec(cw);

  if(got_pic == 0)
    return;

  vd->vd_codectxt = ctx->codec->name;


  /* Update aspect ratio */

  switch(mb->mb_rate) {
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

  if(gc->gc_deilace_type == VD_DEILACE_AUTO) {
    
    if(frame->interlaced_frame)
      vd->vd_deilace_type = VD_DEILACE_HALF_RES;
    else
      vd->vd_deilace_type = VD_DEILACE_NONE;
  } else {
    vd->vd_deilace_type = gc->gc_deilace_type;
  }
  
  /* Compute duration and PTS of frame */

  fm = frame->opaque;
  if(fm == NULL)
    return;

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

  if(vd->vd_mp->mp_subtitles) {
    i = subtitles_index_by_pts(vd->vd_mp->mp_subtitles, pts);
    if(i != vd->vd_last_subtitle_index) {

      if(vd->vd_subtitle_widget != NULL)
	glw_destroy(vd->vd_subtitle_widget);
      
      vd->vd_subtitle_widget =
	subtitles_make_widget(vd->vd_mp->mp_subtitles, i);
      vd->vd_last_subtitle_index = i;
    }
  }

  /* deinterlacer will generate two frames */

  if(vd->vd_deilace_type == VD_DEILACE_HALF_RES)
    duration /= 2;

  snprintf(mq->mq_info_output_type, sizeof(mq->mq_info_output_type),
	   "%d * %d%c @ %.2f Hz (%s)",
	   ctx->width,
	   ctx->height,
	   frame->interlaced_frame ? 'i' : 'p',
	   1000000.0f / (float) duration,
	   avcodec_get_pix_fmt_name(ctx->pix_fmt));

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

  switch(vd->vd_deilace_type) {

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

    if((gvf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
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
    
    vd->vd_interlaced = 0;
    gvf->gvf_pts = pts;
    gvf->gvf_duration = duration;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, gvf, link);
    return;

  case VD_DEILACE_HALF_RES:
    tff = !!frame->top_field_first ^ gc->gc_field_parity;

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
      if((gvf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
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
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, gvf, link);
    }

    if(display_or_skip(vd, duration)) {

      if((gvf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
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
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, gvf, link);
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

    tff = !!frame->top_field_first ^ gc->gc_field_parity;

    if(mode & 1) 
      duration /= 2;

    for(j = 0; j <= (mode & 1); j++) {

      if((gvf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
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

	dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
	h = gvf->gvf_height[i];
	w = gvf->gvf_width[i];
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

      asm volatile("emms \n\t" : : : "memory");
      gvf->gvf_pts = pts + j * duration;
      gvf->gvf_duration = duration;
      TAILQ_INSERT_TAIL(&vd->vd_display_queue, gvf, link);
    }

    vd->vd_yadif_phase++;
    if(vd->vd_yadif_phase > 2)
      vd->vd_yadif_phase = 0;
    return;
  }
}



















void *
vd_thread(void *aux)
{
  video_decoder_t *vd = aux;
  media_pipe_t *mp = vd->vd_mp;
  media_buf_t *mb;

  while((mb = mb_dequeue_wait(mp, &mp->mp_video)) != NULL) {

    switch(mb->mb_data_type) {

    case MB_VIDEO:
      vd_decode_video(vd, mb);
      break;

    case MB_RESET:
      vd_init_timings(vd);
      vd->vd_do_flush = 1;
      /* FALLTHRU */

    case MB_RESET_SPU:
      if(vd->vd_dvdspu != NULL)
	gl_dvdspu_flush(vd->vd_dvdspu);
      break;

    case MB_DVD_SPU:
    case MB_CLUT:
    case MB_DVD_PCI:
    case MB_DVD_HILITE:
      if(vd->vd_dvdspu != NULL)
	gl_dvdspu_dispatch(vd->vd_dvd, vd->vd_dvdspu, mb);
      break;

    default:
      abort();
    }

    media_buf_free(mb);
  }

  return NULL;
}




void
video_decoder_create(media_pipe_t *mp)
{
  int i;
  video_decoder_t *vd;

  vd = mp->mp_video_decoder = calloc(1, sizeof(video_decoder_t));

  vd->vd_zoom = 100;
  vd->vd_mp = mp;
  vd->vd_umax = 1;
  vd->vd_vmax = 1;
  vd_init_timings(vd);

  /* For the exact meaning of these, see gl_video.h */

  TAILQ_INIT(&vd->vd_inactive_queue);
  TAILQ_INIT(&vd->vd_avail_queue);
  TAILQ_INIT(&vd->vd_displaying_queue);
  TAILQ_INIT(&vd->vd_display_queue);
  TAILQ_INIT(&vd->vd_bufalloc_queue);
  TAILQ_INIT(&vd->vd_bufalloced_queue);
  
  for(i = 0; i < VD_FRAMES; i++)
    TAILQ_INSERT_TAIL(&vd->vd_inactive_queue, &vd->vd_frames[i], link);

  pthread_cond_init(&vd->vd_avail_queue_cond, NULL);
  pthread_cond_init(&vd->vd_bufalloced_queue_cond, NULL);
  pthread_mutex_init(&vd->vd_queue_mutex, NULL);
  pthread_mutex_init(&vd->vd_spill_mutex, NULL);

  /* */

  vd->vd_frame = avcodec_alloc_frame();

}


void
video_decoder_start(video_decoder_t *vd)
{
  if(vd->vd_ptid == 0)
    pthread_create(&vd->vd_ptid, NULL, vd_thread, vd);
}

void
video_decoder_join(media_pipe_t *mp, video_decoder_t *vd)
{
  if(vd->vd_ptid != 0) {
    pthread_join(vd->vd_ptid, NULL);
    vd->vd_ptid = 0;
  }

  glw_lock();
  video_decoder_purge(vd);
  glw_unlock();
} 


void
video_decoder_purge(video_decoder_t *vd)
{
  int i;

  if(vd->vd_widget != NULL || vd->vd_ptid != 0)
    return;

  /* Free YADIF frames */

  if(vd->vd_yadif_width) for(i = 0; i < 3; i++)
    avpicture_free(&vd->vd_yadif_pic[i]);

  /* Free ffmpeg frame */

  av_free(vd->vd_frame);

  /* We are really gone now */

  vd->vd_mp->mp_video_decoder = NULL;
  free(vd);
}


void
vd_set_dvd(media_pipe_t *mp, struct dvd_player *dvd)
{
  video_decoder_t *vd = mp->mp_video_decoder;
  vd->vd_dvd = dvd;
}
