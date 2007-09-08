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
#include "gl_video.h"
#include "input.h"
#include "miw.h"
#include "menu.h"
#include "subtitles.h"
#include "yadif.h"

static GLuint yuv2rbg_prog;
static const char *yuv2rbg_code =
#include "cg/yuv2rgb.h"
;

static GLuint yuv2rbg_2mix_prog;
static const char *yuv2rbg_2mix_code =
#include "cg/yuv2rgb_2mix.h"
;

static void gvp_destroy(gl_video_pipe_t *gvp);



/**************************************************************************
 *
 *  GL Video Pipe Init
 *
 */


static int
glp_check_error(const char *name)
{
  GLint errpos;
  const GLubyte *errstr;

  glGetIntegerv(GL_PROGRAM_ERROR_POSITION_ARB, &errpos);

  if(errpos == -1)
    return 0;

  errstr = glGetString(GL_PROGRAM_ERROR_STRING_ARB);

  fprintf(stderr, "%s: error \"%s\" on line %d\n", name, errstr, errpos);
  return 0;
}

void
gvp_init(void)
{

  yadif_init();

  glGenProgramsARB(1, &yuv2rbg_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_code), yuv2rbg_code);

  glp_check_error("yuv2rgb");


  glGenProgramsARB(1, &yuv2rbg_2mix_prog);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_2mix_prog);
  glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB, 
		     strlen(yuv2rbg_2mix_code), yuv2rbg_2mix_code);

  glp_check_error("yuv2rgb_2mix");

  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, 0);
}




static void 
gvp_kalman_init(gv_kalman_t *gvk)
{
  gvk->P = 1.0;
  gvk->Q = 1.0/ 100000.0;
  gvk->R = 0.01;
  gvk->x_next = 0.0;
}

static float
gvp_kalman_feed(gv_kalman_t *gvk, float z) __attribute__((unused));

static float
gvp_kalman_feed(gv_kalman_t *gvk, float z)
{
  float x;

  gvk->P_next = gvk->P + gvk->Q;
  gvk->K = gvk->P_next / (gvk->P_next + gvk->R);
  x = gvk->x_next + gvk->K * (z - gvk->x_next);
  gvk->P = (1 - gvk->K) * gvk->P_next;

  gvk->x_next = x;

  return x;
}

static void
gvp_init_timings(gl_video_pipe_t *gvp)
{
  gvp_kalman_init(&gvp->gvp_avfilter);
  gvp->gvp_lastpts = AV_NOPTS_VALUE;
  gvp->gvp_nextpts = AV_NOPTS_VALUE;
  gvp->gvp_estimated_duration = 0;
  gvp->gvp_last_subtitle_index = -1;
}


void 
gvp_conf_init(gvp_conf_t *gc)
{
  memset(gc, 0, sizeof(gvp_conf_t));
  gc->gc_postproc_type = GVP_PP_AUTO;
  gc->gc_zoom = 100;
}


/**************************************************************************
 *
 *  Video decoding thread
 *
 */

static void gl_decode_video(gl_video_pipe_t *gvp, media_buf_t *mb);

void *
gl_decode_thread(void *aux)
{
  gl_video_pipe_t *gvp = aux;
  media_buf_t *mb;
  media_pipe_t *mp = gvp->gvp_mp;

  while(gvp->gvp_state == GVP_STATE_THREAD_RUNNING) {

    mb = mb_dequeue_wait(mp, &mp->mp_video);

    switch(mb->mb_data_type) {

    case MB_EXIT:
      glw_lock();
      gvp->gvp_state = GVP_STATE_IDLE;
      break;

    case MB_NOP:
      break;

    case MB_VIDEO:
      gl_decode_video(gvp, mb);
      break;

    case MB_RESET:
    case MB_FLUSH:
      gvp_init_timings(gvp);
      gvp->gvp_do_flush = 1;
      /* FALLTHRU */

    case MB_RESET_SPU:
      if(gvp->gvp_dvdspu != NULL)
	gl_dvdspu_flush(gvp->gvp_dvdspu);
      break;

    case MB_DVD_SPU:
    case MB_CLUT:
    case MB_DVD_PCI:
    case MB_DVD_HILITE:
      if(gvp->gvp_dvdspu != NULL)
	gl_dvdspu_dispatch(gvp->gvp_dvd, gvp->gvp_dvdspu, mb);
      break;

    default:
      fprintf(stderr, "ERROR unknown hmb type %d\n", mb->mb_data_type);
      abort();
    }

    media_buf_free(mb);
  }

  pthread_mutex_lock(&mp->mp_mutex);
  mq_flush(&mp->mp_video);
  pthread_mutex_unlock(&mp->mp_mutex);
 

  if(gvp->gvp_flags & GLW_DESTROYED) {
    glw_deref(gvp->gvp_widget);
    gvp_destroy(gvp);
  }

  glw_unlock();
  return NULL;
}



static gl_video_frame_t *
gvp_dequeue_for_decode(gl_video_pipe_t *gvp, int w[3], int h[3])
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&gvp->gvp_queue_mutex);
  
  while((gvf = TAILQ_FIRST(&gvp->gvp_avail_queue)) == NULL &&
	gvp->gvp_purged == 0) {
    pthread_cond_wait(&gvp->gvp_avail_queue_cond, &gvp->gvp_queue_mutex);
  }
  if(gvf == NULL)
    return NULL;

  TAILQ_REMOVE(&gvp->gvp_avail_queue, gvf, link);

  if(gvf->gvf_width[0] == w[0] && gvf->gvf_height[0] == h[0] && 
     gvf->gvf_width[1] == w[1] && gvf->gvf_height[1] == h[1] && 
     gvf->gvf_width[2] == w[2] && gvf->gvf_height[2] == h[2] && 
     gvf->gvf_pbo_ptr != NULL) {
    pthread_mutex_unlock(&gvp->gvp_queue_mutex);
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

  TAILQ_INSERT_TAIL(&gvp->gvp_bufalloc_queue, gvf, link);

  while((gvf = TAILQ_FIRST(&gvp->gvp_bufalloced_queue)) == NULL &&
	gvp->gvp_purged == 0 )
    pthread_cond_wait(&gvp->gvp_bufalloced_queue_cond, &gvp->gvp_queue_mutex);

  if(gvf == NULL)
    return 0;

  TAILQ_REMOVE(&gvp->gvp_bufalloced_queue, gvf, link);

  pthread_mutex_unlock(&gvp->gvp_queue_mutex);

  assert(gvf->gvf_pbo_ptr != NULL);
  return gvf;
}


static int
display_or_skip(gl_video_pipe_t *gvp, int duration)
{
  int r = 1;

  if(gvp->gvp_spill == 0)
    return 1;

  pthread_mutex_lock(&gvp->gvp_spill_mutex);

  if(gvp->gvp_spill > duration) {
    gvp->gvp_spill -= duration;
    r = 0;
  }
  pthread_mutex_unlock(&gvp->gvp_spill_mutex);
  return r;
}

typedef struct {
  int refcount;
  int64_t pts;
  int duration;

} frame_meta_t;


static int
gvp_get_buffer(struct AVCodecContext *c, AVFrame *pic)
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
gvp_release_buffer(struct AVCodecContext *c, AVFrame *pic)
{
  frame_meta_t *fm = pic->opaque;

  if(fm != NULL)
    free(fm);

  avcodec_default_release_buffer(c, pic);
}


#define gvp_valid_duration(t) ((t) > 1000ULL && (t) < 10000000ULL)

static void 
gl_decode_video(gl_video_pipe_t *gvp, media_buf_t *mb)
{
  gl_video_frame_t *gvf;
  int64_t pts, t;
  int i, j, got_pic, h, w, duration;
  media_pipe_t *mp = gvp->gvp_mp;
  unsigned char *src, *dst;
  float f;
  codecwrap_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  AVFrame *frame = gvp->gvp_frame;
  gvp_conf_t *gc = gvp->gvp_conf;
  frame_meta_t *fm;
  media_queue_t *mq = &mp->mp_video;
  time_t now;
  int hvec[3], wvec[3];
  int tff, w2, mode;
  uint8_t *prev, *cur, *next;
  int hshift, vshift;

  got_pic = 0;

  wrap_lock_codec(cw);

  if(gvp->gvp_do_flush) {
    do {
      avcodec_decode_video(ctx, frame, &got_pic, NULL, 0);
    } while(got_pic);

    gvp->gvp_do_flush = 0;
    gvp->gvp_lastpts = AV_NOPTS_VALUE;
    gvp->gvp_estimated_duration = 0;
    avcodec_flush_buffers(ctx);
    gvp->gvp_compensate_thres = 5;
  }

  time(&now);
  if(now != mq->mq_info_last_time) {
    mq->mq_info_rate = mq->mq_info_rate_acc / 125;
    mq->mq_info_last_time = now;
    mq->mq_info_rate_acc = 0;
  }
  mq->mq_info_rate_acc += mb->mb_size;

  ctx->opaque = mb;
  ctx->get_buffer = gvp_get_buffer;
  ctx->release_buffer = gvp_release_buffer;

  avcodec_decode_video(ctx, frame, &got_pic, mb->mb_data, mb->mb_size);

  nice_codec_name(mq->mq_info_codec, sizeof(mq->mq_info_codec), ctx);
  
  wrap_unlock_codec(cw);

  if(got_pic == 0)
    return;

  gvp->gvp_codectxt = ctx->codec->name;


  /* Update aspect ratio */

  switch(mb->mb_rate) {
  case 0:

    if(frame->pan_scan != NULL && frame->pan_scan->width != 0)
      f = (float)frame->pan_scan->width / (float)frame->pan_scan->height;
    else
      f = (float)ctx->width / (float)ctx->height;
    
    gvp->gvp_aspect = (av_q2d(ctx->sample_aspect_ratio) ?: 1) * f;
    break;
  case 1:
    gvp->gvp_aspect = (4.0f / 3.0f);
    break;
  case 2:
    gvp->gvp_aspect = (16.0f / 9.0f);
    break;
  }

  if(gc->gc_postproc_type == GVP_PP_AUTO) {
    
    if(frame->interlaced_frame)
      gvp->gvp_postproc_type = GVP_PP_DEINTERLACER;
    else
      gvp->gvp_postproc_type = GVP_PP_NONE;
  } else {
    gvp->gvp_postproc_type = gc->gc_postproc_type;
  }
  
  /* Compute duration and PTS of frame */

  fm = frame->opaque;
  if(fm == NULL)
    return;

  pts = fm->pts;
  duration = fm->duration;

  if(!gvp_valid_duration(duration)) {
    /* duration is zero or very invalid, use duration from last output */
    duration = gvp->gvp_estimated_duration;
  }

  if(pts == AV_NOPTS_VALUE && gvp->gvp_nextpts != AV_NOPTS_VALUE)
    pts = gvp->gvp_nextpts; /* no pts set, use estimated pts */

  if(pts != AV_NOPTS_VALUE && gvp->gvp_lastpts != AV_NOPTS_VALUE) {
    /* we know pts of last frame */
    t = pts - gvp->gvp_lastpts;

    if(gvp_valid_duration(t)) {
      /* inter frame duration seems valid, store it */
      gvp->gvp_estimated_duration = t;
      if(duration == 0)
	duration = t;

    } else if(t < 0 || t > 10000000LL) {
      /* crazy pts jump, use estimated pts from last output instead */
      pts = gvp->gvp_nextpts;
    }
  }
  
  /* compensate for frame repeat */
  
  duration += frame->repeat_pict * (duration * 0.5);
 
  if(pts != AV_NOPTS_VALUE) {
    gvp->gvp_lastpts = pts;
    gvp->gvp_nextpts = pts + duration;
  }

  if(duration == 0 || pts == AV_NOPTS_VALUE)
    return;

  if(gvp->gvp_mp->mp_subtitles) {
    i = subtitles_index_by_pts(gvp->gvp_mp->mp_subtitles, pts);
    if(i != gvp->gvp_last_subtitle_index) {

      if(gvp->gvp_subtitle_widget != NULL)
	glw_destroy(gvp->gvp_subtitle_widget);
      
      gvp->gvp_subtitle_widget =
	subtitles_make_widget(gvp->gvp_mp->mp_subtitles, i);
      gvp->gvp_last_subtitle_index = i;
    }
  }

  /* deinterlacer will generate two frames */

  if(gvp->gvp_postproc_type == GVP_PP_DEINTERLACER)
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

  switch(gvp->gvp_postproc_type) {

  case GVP_PP_AUTO:
    return;

    /*
     *  No post processing
     */

  case GVP_PP_NONE:
    gvp->gvp_active_frames_needed = 3;
    gvp->gvp_interlaced = 0;
    if(!display_or_skip(gvp, duration))
      return;

    if((gvf = gvp_dequeue_for_decode(gvp, wvec, hvec)) == NULL)
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
    
    gvp->gvp_interlaced = 0;
    gvf->gvf_pts = pts;
    gvf->gvf_duration = duration;
    TAILQ_INSERT_TAIL(&gvp->gvp_display_queue, gvf, link);
    return;

  case GVP_PP_DEINTERLACER:
    tff = !!frame->top_field_first ^ gc->gc_field_parity;

    gvp->gvp_active_frames_needed = 3;

    /*
     *  Deinterlace by 2 x framerate and 0.5 * y-res,
     *  OpenGL does bledning for us
     */

    gvp->gvp_interlaced = 1;

    hvec[0] = hvec[0] / 2;
    hvec[1] = hvec[1] / 2;
    hvec[2] = hvec[2] / 2;

    if(display_or_skip(gvp, duration)) {
      if((gvf = gvp_dequeue_for_decode(gvp, wvec, hvec)) == NULL)
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
      TAILQ_INSERT_TAIL(&gvp->gvp_display_queue, gvf, link);
    }

    if(display_or_skip(gvp, duration)) {

      if((gvf = gvp_dequeue_for_decode(gvp, wvec, hvec)) == NULL)
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
      TAILQ_INSERT_TAIL(&gvp->gvp_display_queue, gvf, link);
    }
    return;
    
  case GVP_PP_YADIF_FRAME:
    mode = 0; goto yadif;
  case GVP_PP_YADIF_FIELD:
    mode = 1; goto yadif;
  case GVP_PP_YADIF_FRAME_NO_SPATIAL_ILACE:
    mode = 2; goto yadif;
  case GVP_PP_YADIF_FIELD_NO_SPATIAL_ILACE:
    mode = 3;
  yadif:
    if(gvp->gvp_yadif_width   != ctx->width  ||
       gvp->gvp_yadif_height  != ctx->height ||
       gvp->gvp_yadif_pix_fmt != ctx->pix_fmt) {
      
      gvp->gvp_yadif_width   = ctx->width;
      gvp->gvp_yadif_height  = ctx->height;
      gvp->gvp_yadif_pix_fmt = ctx->pix_fmt;

      for(i = 0; i < 3; i++) {
	avpicture_free(&gvp->gvp_yadif_pic[i]);
	avpicture_alloc(&gvp->gvp_yadif_pic[i], ctx->pix_fmt, 
			ctx->width, ctx->height);
      }
    }

    gvp->gvp_active_frames_needed = 3;
    gvp->gvp_interlaced = 0;

    for(i = 0; i < 3; i++) {
      w = gvp->gvp_yadif_width  >> !!i;
      h = gvp->gvp_yadif_height >> !!i;
       src = frame->data[i];
       dst = gvp->gvp_yadif_pic[gvp->gvp_yadif_phase].data[i];
       while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += frame->linesize[i];
      }
    }

    if(!display_or_skip(gvp, duration))
      return;

    tff = !!frame->top_field_first ^ gc->gc_field_parity;

    if(mode & 1) 
      duration /= 2;

    for(j = 0; j <= (mode & 1); j++) {

      if((gvf = gvp_dequeue_for_decode(gvp, wvec, hvec)) == NULL)
	return;

      for(i = 0; i < 3; i++) {
	int y;
	int parity = j ^ tff ^ 1;

	h = gvp->gvp_yadif_phase;
	next = gvp->gvp_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	cur = gvp->gvp_yadif_pic[h].data[i];
	if(--h < 0) h = 2;
	prev = gvp->gvp_yadif_pic[h].data[i];

	dst = gvf->gvf_pbo_ptr + gvf->gvf_pbo_offset[i];
	h = gvf->gvf_height[i];
	w = gvf->gvf_width[i];
	w2 = gvp->gvp_yadif_width >> !!i;

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

      gvp->gvp_interlaced = 0;
      gvf->gvf_pts = pts + j * duration;
      gvf->gvf_duration = duration;
      TAILQ_INSERT_TAIL(&gvp->gvp_display_queue, gvf, link);
    }

    gvp->gvp_yadif_phase++;
    if(gvp->gvp_yadif_phase > 2)
      gvp->gvp_yadif_phase = 0;
    

    return;
  }
}



/**************************************************************************
 *
 *  Create video output widget
 *
 */

static int gl_video_widget_callback(glw_t *w, void *opaque, 
				    glw_signal_t signal, ...);

static void
gvp_destroy(gl_video_pipe_t *gvp)
{
  abort(); /* free yadif */
  av_free(gvp->gvp_frame);
  free(gvp);
}


glw_t *
gvp_create(glw_t *p, media_pipe_t *mp, gvp_conf_t *gc, int flags)
{
  int i;
  gl_video_pipe_t *gvp;


  gvp = calloc(1, sizeof(gl_video_pipe_t));
  gvp->gvp_conf = gc;
  gvp->gvp_zoom = 100;
  gvp->gvp_flags = flags;

  /* For the exact meaning of these, see gl_video.h */

  TAILQ_INIT(&gvp->gvp_inactive_queue);
  TAILQ_INIT(&gvp->gvp_avail_queue);
  TAILQ_INIT(&gvp->gvp_displaying_queue);
  TAILQ_INIT(&gvp->gvp_display_queue);
  TAILQ_INIT(&gvp->gvp_bufalloc_queue);
  TAILQ_INIT(&gvp->gvp_bufalloced_queue);

  
  pthread_cond_init(&gvp->gvp_avail_queue_cond, NULL);
  pthread_cond_init(&gvp->gvp_bufalloced_queue_cond, NULL);
  pthread_mutex_init(&gvp->gvp_queue_mutex, NULL);
  pthread_mutex_init(&gvp->gvp_spill_mutex, NULL);

  gvp->gvp_mp = mp;

  gvp->gvp_umax = 1;
  gvp->gvp_vmax = 1;
  gvp_init_timings(gvp);


  for(i = 0; i < GVP_FRAMES; i++)
    TAILQ_INSERT_TAIL(&gvp->gvp_inactive_queue, &gvp->gvp_frames[i], link);

  gvp->gvp_frame = avcodec_alloc_frame();
  gvp->gvp_dvdspu = gl_dvdspu_init();

  gvp->gvp_widget = glw_create(GLW_EXT,
			       GLW_ATTRIB_FLAGS, GLW_EVERY_FRAME,
			       GLW_ATTRIB_PARENT, p, 
			       GLW_ATTRIB_SIGNAL_HANDLER, 
			       gl_video_widget_callback, gvp, 0,
			       NULL);

  return gvp->gvp_widget;
}


void
gvp_set_dvd(glw_t *w, struct dvd_player *dvd)
{
  gl_video_pipe_t *gvp = glw_get_opaque(w, gl_video_widget_callback);
  gvp->gvp_dvd = dvd;
}





/**************************************************************************
 *
 *  Buffer allocator
 *
 */


static void
gvp_buffer_allocator(gl_video_pipe_t *gvp)
{
  gl_video_frame_t *gvf;
  size_t siz;

  pthread_mutex_lock(&gvp->gvp_queue_mutex);
  
  assert(gvp->gvp_active_frames_needed <= GVP_FRAMES);

  while(gvp->gvp_active_frames < gvp->gvp_active_frames_needed) {
    gvf = TAILQ_FIRST(&gvp->gvp_inactive_queue);
    TAILQ_REMOVE(&gvp->gvp_inactive_queue, gvf, link);
    TAILQ_INSERT_TAIL(&gvp->gvp_avail_queue, gvf, link);
    pthread_cond_signal(&gvp->gvp_avail_queue_cond);
    gvp->gvp_active_frames++;
  }

  while((gvf = TAILQ_FIRST(&gvp->gvp_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_bufalloc_queue, gvf, link);

    if(gvf->gvf_pbo != 0)
      glDeleteBuffersARB(1, &gvf->gvf_pbo);
    glGenBuffersARB(1, &gvf->gvf_pbo);


    /* XXX: Do we really need to delete textures if they are already here ? */
       
    if(gvf->gvf_textures[0] != 0)
      glDeleteTextures(3, gvf->gvf_textures);

    glGenTextures(3, gvf->gvf_textures);

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

    siz = 
      gvf->gvf_width[0] * gvf->gvf_height[0] + 
      gvf->gvf_width[1] * gvf->gvf_height[1] + 
      gvf->gvf_width[2] * gvf->gvf_height[2];

    glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, siz, NULL, GL_STREAM_DRAW_ARB);

    gvf->gvf_pbo_offset[0] = 0;
    gvf->gvf_pbo_offset[1] = gvf->gvf_width[0] * gvf->gvf_height[0];

    gvf->gvf_pbo_offset[2] = 
      gvf->gvf_pbo_offset[1] + gvf->gvf_width[1] * gvf->gvf_height[1];


    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

    TAILQ_INSERT_TAIL(&gvp->gvp_bufalloced_queue, gvf, link);
    pthread_cond_signal(&gvp->gvp_bufalloced_queue_cond);
  }

  pthread_mutex_unlock(&gvp->gvp_queue_mutex);

}

/**************************************************************************
 *
 *  Texture loader
 *
 */


static void
gvp_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint 
gvp_tex_get(gl_video_pipe_t *gvp, gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


static void
render_video_upload(gl_video_pipe_t *gvp, gl_video_frame_t *gvf)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr = NULL;

  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, gvf, GVF_TEX_L));
  gvp_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1, 
	       gvf->gvf_width[0], gvf->gvf_height[0],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)gvf->gvf_pbo_offset[0]);

  /* Cr */

  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, gvf, GVF_TEX_Cr));
  gvp_set_tex_meta();

  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[1], gvf->gvf_height[1],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)gvf->gvf_pbo_offset[2]);

  /* Cb */
  
  
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, gvf, GVF_TEX_Cb));

  gvp_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[2], gvf->gvf_height[2],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)gvf->gvf_pbo_offset[1]);
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

}




/**************************************************************************
 *
 *  Video widget layout
 *
 */



static void
gvp_enqueue_for_decode(gl_video_pipe_t *gvp, gl_video_frame_t *gvf, 
		       struct gl_video_frame_queue *fromqueue)
{
  
  pthread_mutex_lock(&gvp->gvp_queue_mutex);

  TAILQ_REMOVE(fromqueue, gvf, link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
  
    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&gvp->gvp_avail_queue, gvf, link);
  pthread_cond_signal(&gvp->gvp_avail_queue_cond);
  pthread_mutex_unlock(&gvp->gvp_queue_mutex);
}


static void
gvp_enqueue_for_display(gl_video_pipe_t *gvp, gl_video_frame_t *gvf,
			struct gl_video_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvf, link);
  TAILQ_INSERT_TAIL(&gvp->gvp_displaying_queue, gvf, link);
}


static float cmatrix_color[9] = {
  1.1643,  0,        1.5958,
  1.1643, -0.39173, -0.81290,
  1.1643,  2.017,    0
};

static float cmatrix_bw[9] = {
  1.1643,  0,        0,
  1.1643,  0,        0,
  1.1643,  0,        0
};

static void
gvp_color_matrix_update(gl_video_pipe_t *gvp, media_pipe_t *mp)
{
  float *f;
  int i;

  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  
  for(i = 0; i < 9; i++)
    gvp->gvp_cmatrix[i] = (gvp->gvp_cmatrix[i] * 15.0 + f[i]) / 16.0;

}





static int
gvp_compute_output_duration(gl_video_pipe_t *gvp, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(gvp->gvp_avdiff_x > 0) {
    delta = pow(gvp->gvp_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(gvp->gvp_avdiff_x < 0) {
    delta = -pow(-gvp->gvp_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}

static void
gvp_compute_avdiff(gl_video_pipe_t *gvp, media_pipe_t *mp, int64_t pts)
{
  if(!mp->mp_clock_valid) {
    gvp->gvp_avdiff_x = 0;
    gvp_kalman_init(&gvp->gvp_avfilter);
    return;
  }

  if(gvp->gvp_compensate_thres > 0) {
    gvp->gvp_compensate_thres--;
    gvp->gvp_avdiff_x = 0;
    gvp_kalman_init(&gvp->gvp_avfilter);
    return;
  }
  

  gvp->gvp_avdiff = mp->mp_clock - pts - gvp->gvp_conf->gc_avcomp * 1000;

  if(abs(gvp->gvp_avdiff) < 10000000) {

    gvp->gvp_avdiff_x = gvp_kalman_feed(&gvp->gvp_avfilter, 
					(float)gvp->gvp_avdiff / 1000000);
    if(gvp->gvp_avdiff_x > 10.0f)
      gvp->gvp_avdiff_x = 10.0f;
    
    if(gvp->gvp_avdiff_x < -10.0f)
      gvp->gvp_avdiff_x = -10.0f;
  }
#if 0
  printf("%s: AVDIFF = %f %d %lld %lld\n", 
	 mp->mp_name, gvp->gvp_avdiff_x, gvp->gvp_avdiff,
	 mp->mp_clock, pts);
#endif
}


int64_t
gvp_compute_blend(gl_video_pipe_t *gvp, gl_video_frame_t *fra,
		  gl_video_frame_t *frb, int output_duration)
{
  int64_t pts;
  int x;

  if(fra->gvf_duration >= output_duration) {
  
    gvp->gvp_fra = fra;
    gvp->gvp_frb = NULL;

    fra->gvf_duration -= output_duration;
    pts = fra->gvf_pts;
    fra->gvf_pts += output_duration;

  } else if(frb != NULL) {

    gvp->gvp_fra = fra;
    gvp->gvp_frb = frb;
    gvp->gvp_blend = (float) fra->gvf_duration / (float)output_duration;

    if(fra->gvf_duration + frb->gvf_duration < output_duration) {

      pthread_mutex_lock(&gvp->gvp_spill_mutex);
      x = output_duration - (fra->gvf_duration + frb->gvf_duration);
      gvp->gvp_spill += x;
      pthread_mutex_unlock(&gvp->gvp_spill_mutex);

      frb->gvf_duration = 0;
      pts = frb->gvf_pts;

    } else {
      pts = fra->gvf_pts;
      x = output_duration - fra->gvf_duration;
      frb->gvf_duration -= x;
      frb->gvf_pts += x;
    }
    fra->gvf_duration = 0;

  } else {
    gvp->gvp_fra = fra;
    gvp->gvp_frb = NULL;
    pts = AV_NOPTS_VALUE;
  }

  return pts;
}





static void 
layout_video_pipe(gl_video_pipe_t *gvp, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = gvp->gvp_mp;
  int output_duration;
  int width = 0, height = 0;
  int64_t pts = 0;
  gvp_conf_t *gc = gvp->gvp_conf;
  struct gl_video_frame_queue *dq;

  if(gvp->gvp_subtitle_widget)
    glw_layout(gvp->gvp_subtitle_widget, rc);

  gvp->gvp_zoom = (gvp->gvp_zoom * 7.0f + gc->gc_zoom) / 8.0f;


  gvp->gvp_rendered = 1;
  gvp_color_matrix_update(gvp, mp);
  output_duration = gvp_compute_output_duration(gvp, frame_duration);


  dq = &gvp->gvp_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&gvp->gvp_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      gvp->gvp_fra = fra;
      gvp->gvp_frb = NULL;
    } else {
      gvp->gvp_fra = NULL;
      gvp->gvp_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&gvp->gvp_displaying_queue)) != NULL)
      gvp_enqueue_for_decode(gvp, frb, &gvp->gvp_displaying_queue);

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];
      
    frb = TAILQ_NEXT(fra, link);
    pts = gvp_compute_blend(gvp, fra, frb, output_duration);

    if(mp_get_playstatus(mp) == MP_PLAY) {

      if(fra != NULL && fra->gvf_duration == 0)
	gvp_enqueue_for_display(gvp, fra, dq);

      if(frb != NULL && frb->gvf_duration == 0)
	gvp_enqueue_for_display(gvp, frb, dq);
    }
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    gvp_compute_avdiff(gvp, mp, pts);
  }
  gl_dvdspu_layout(gvp->gvp_dvd, gvp->gvp_dvdspu);
}




/**************************************************************************
 *
 *  Video widget render
 *
 */

static void
render_video_quad(media_pipe_t *mp, gl_video_pipe_t *gvp, 
		  gl_video_frame_t *gvf)
{
  
  float tzoom = gvp->gvp_interlaced ? 0.01 : 0.00;
  
  if(mp_get_playstatus(mp) == MP_STOP)
    return;

  glBegin(GL_QUADS);

  glTexCoord2f(tzoom, tzoom);
  glVertex3f( -1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(gvp->gvp_umax - tzoom, tzoom);
  glVertex3f( 1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(gvp->gvp_umax - tzoom, gvp->gvp_vmax - tzoom);
  glVertex3f( 1.0f, -1.0f, 0.0f);

  glTexCoord2f(tzoom, gvp->gvp_vmax - tzoom); 
  glVertex3f( -1.0f, -1.0f, 0.0f);

  glEnd();
}




static void
render_video_1f(media_pipe_t *mp, gl_video_pipe_t *gvp, 
		gl_video_frame_t *gvf, float alpha)
{
  int i;
  GLuint tex;

  render_video_upload(gvp, gvf);


  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_prog);


  /* ctrl constants */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       alpha,
			       /* ctrl.y == ishift */
			       (-0.5f * gvf->gvf_debob) / 
			       (float)gvf->gvf_height[0],
			       0.0,
			       0.0);

  /* color matrix */

  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 gvp->gvp_cmatrix[i * 3 + 0],
				 gvp->gvp_cmatrix[i * 3 + 1],
				 gvp->gvp_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = gvp_tex_get(gvp, gvf, GVF_TEX_Cb);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = gvp_tex_get(gvp, gvf, GVF_TEX_Cr);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = gvp_tex_get(gvp, gvf, GVF_TEX_L);
  glBindTexture(GL_TEXTURE_2D, tex);
  
  render_video_quad(mp, gvp, gvf);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


static void
gvp_blend_frames(gl_video_pipe_t *gvp, glw_rctx_t *rc, gl_video_frame_t *fra,
		 gl_video_frame_t *frb, media_pipe_t *mp)
{
  float blend = gvp->gvp_blend;
  int i;
  
  render_video_upload(gvp, fra);
  render_video_upload(gvp, frb);
    
  glEnable(GL_FRAGMENT_PROGRAM_ARB);
  glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, yuv2rbg_2mix_prog);

  /* ctrl */
  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
			       /* ctrl.x == alpha */
			       rc->rc_alpha, 
			       /* ctrl.y == blend */
			       blend,
			       /* ctrl.z == image a, y displace */
			       (-0.5f * fra->gvf_debob) / 
			       (float)fra->gvf_height[0],
			       /* ctrl.w == image b, y displace */
			       (-0.5f * frb->gvf_debob) / 
			       (float)frb->gvf_height[0]);
				
  /* color matrix */
  for(i = 0; i < 3; i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 2 + i,
				 gvp->gvp_cmatrix[i * 3 + 0],
				 gvp->gvp_cmatrix[i * 3 + 1],
				 gvp->gvp_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, fra, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, fra, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, fra, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, frb, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, frb, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(GL_TEXTURE_2D, gvp_tex_get(gvp, frb, GVF_TEX_L));

  render_video_quad(mp, gvp, fra);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}




static void 
render_video_pipe(gl_video_pipe_t *gvp, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = gvp->gvp_mp;
  int width = 0, height = 0;

  static GLdouble clip_left[4] = {1.0, 0.0, 0.0, 1.0};
  static GLdouble clip_right[4] = {-1.0, 0.0, 0.0, 1.0};
  static GLdouble clip_bot[4] = {0.0, 1.0, 0.0, 1.0};
  static GLdouble clip_top[4] = {0.0, -1.0, 0.0, 1.0};

  /*
   * rescale
   */
 
  glPushMatrix();
  if(gvp->gvp_zoom != 100) {
    if(gvp->gvp_zoom > 100) {
      glClipPlane(GL_CLIP_PLANE0, clip_bot);
      glClipPlane(GL_CLIP_PLANE1, clip_top);
      glClipPlane(GL_CLIP_PLANE2, clip_left);
      glClipPlane(GL_CLIP_PLANE3, clip_right);
      glEnable(GL_CLIP_PLANE0);
      glEnable(GL_CLIP_PLANE1);
      glEnable(GL_CLIP_PLANE2);
      glEnable(GL_CLIP_PLANE3);
    }
    glScalef(gvp->gvp_zoom / 100.0f, gvp->gvp_zoom / 100.0f, 1.0f);
  }
  glPolygonOffset(0, rc->rc_polyoffset - 1);
  glw_scale_and_rotate(rc->rc_aspect, gvp->gvp_aspect, 0.0f);



  if(rc->rc_alpha < 0.98f) 
    glEnable(GL_BLEND); 
  else
    glDisable(GL_BLEND); 


  fra = gvp->gvp_fra;
  frb = gvp->gvp_frb;

  if(fra != NULL) {

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];

    if(frb != NULL) {
      gvp_blend_frames(gvp, rc, fra, frb, mp);
    } else {
      render_video_1f(mp, gvp, fra, rc->rc_alpha);
    }
  }

  glDisable(GL_BLEND); 

  if(width > 0) {
    glPolygonOffset(0, rc->rc_polyoffset - 2);
    gl_dvdspu_render(gvp->gvp_dvdspu, width, height, rc->rc_alpha);
  }

  if(gvp->gvp_zoom > 100) {
    glDisable(GL_CLIP_PLANE0);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);
  }

  glPopMatrix();

  if(gvp->gvp_subtitle_widget)
    glw_render(gvp->gvp_subtitle_widget, rc);

}

/*
 * 
 */


static void
gvf_purge(gl_video_pipe_t *gvp, gl_video_frame_t *gvf)
{
  if(gvf->gvf_pbo != 0) {
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
    glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
    gvf->gvf_pbo_ptr = NULL;
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
    glDeleteBuffersARB(1, &gvf->gvf_pbo);
  }

  gvf->gvf_pbo = 0;

  if(gvf->gvf_textures[0] != 0)
    glDeleteTextures(3, gvf->gvf_textures);

  gvf->gvf_textures[0] = 0;

  TAILQ_INSERT_TAIL(&gvp->gvp_inactive_queue, gvf, link);
  assert(gvp->gvp_active_frames > 0);
  gvp->gvp_active_frames--;
}



static void
gvp_purge(gl_video_pipe_t *gvp)
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&gvp->gvp_queue_mutex);

  while((gvf = TAILQ_FIRST(&gvp->gvp_avail_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_avail_queue, gvf, link);
    gvf_purge(gvp, gvf);
  }

  while((gvf = TAILQ_FIRST(&gvp->gvp_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_bufalloc_queue, gvf, link);
    gvf_purge(gvp, gvf);
  }

  while((gvf = TAILQ_FIRST(&gvp->gvp_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_bufalloced_queue, gvf, link);
    gvf_purge(gvp, gvf);
  }

  while((gvf = TAILQ_FIRST(&gvp->gvp_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_displaying_queue, gvf, link);
    gvf_purge(gvp, gvf);
  }

  while((gvf = TAILQ_FIRST(&gvp->gvp_display_queue)) != NULL) {
    TAILQ_REMOVE(&gvp->gvp_display_queue, gvf, link);
    gvf_purge(gvp, gvf);
  }
  gvp->gvp_purged = 1;
  pthread_cond_signal(&gvp->gvp_avail_queue_cond);
  pthread_cond_signal(&gvp->gvp_bufalloced_queue_cond);
  
  pthread_mutex_unlock(&gvp->gvp_queue_mutex);
}



static void
gl_constant_frame_flush(gl_video_pipe_t *gvp)
{
  gl_video_frame_t *fra;

  fra = TAILQ_FIRST(&gvp->gvp_displaying_queue);
  if(fra != NULL) {
    assert(fra->gvf_pbo_ptr == NULL);
    gvp_enqueue_for_decode(gvp, fra, &gvp->gvp_displaying_queue);
  } else {
    fra = TAILQ_FIRST(&gvp->gvp_display_queue);
    if(fra != NULL) {
      gvp_enqueue_for_decode(gvp, fra, &gvp->gvp_display_queue);
    }
  }
}


static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  pthread_attr_t attr;
  gl_video_pipe_t *gvp = opaque;
  media_pipe_t *mp = gvp->gvp_mp;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_DTOR:
    gvp_purge(gvp);

    gl_dvdspu_deinit(gvp->gvp_dvdspu);
    gvp->gvp_dvdspu = NULL;

    switch(gvp->gvp_state) {
    case GVP_STATE_IDLE:
      gvp_destroy(gvp);
      return 0;
    case GVP_STATE_THREAD_RUNNING:
      mp_send_cmd(mp, &mp->mp_video, MB_EXIT);
      /* FALLTHRU */
    case GVP_STATE_THREAD_DESTROYING:
      glw_ref(gvp->gvp_widget);
      return 0;
    }
    return 0;


  case GLW_SIGNAL_LAYOUT:
    glw_set_active(w);

    switch(gvp->gvp_state) {
    case GVP_STATE_IDLE:
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      pthread_create(&gvp->gvp_decode_thrid, &attr, gl_decode_thread, gvp);
      gvp->gvp_state = GVP_STATE_THREAD_RUNNING;
      /* FALLTHRU */
    case GVP_STATE_THREAD_RUNNING:
      layout_video_pipe(gvp, va_arg(ap, void *));
      break;

    case GVP_STATE_THREAD_DESTROYING:
      return 0;
    }
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video_pipe(gvp, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_INACTIVE:
    mp_send_cmd(mp, &mp->mp_video, MB_EXIT);
    gvp->gvp_state = GVP_STATE_THREAD_DESTROYING;
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    gvp_buffer_allocator(gvp);

    switch(gvp->gvp_state) {
    case GVP_STATE_IDLE:
    case GVP_STATE_THREAD_DESTROYING:
      gl_constant_frame_flush(gvp);
      return 0;
    case GVP_STATE_THREAD_RUNNING:
      return 0;
    }
    return 0;

  default:
    return 0;
  }
}




/******************************************************************************
 *
 * Menus
 *
 */



static int 
gvp_menu_pp(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  gvp_conf_t *gc = opaque;
  glw_t *b;
  float v;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_BITMAP)) == NULL)
      return 0;
    
    v = w->glw_u32 == gc->gc_postproc_type ? 1 : 0;
    b->glw_alpha = (b->glw_alpha * 15 + v) / 16.0;
    return 0;

  case GLW_SIGNAL_CLICK:
    gc->gc_postproc_type = w->glw_u32;
    return 1;
    
  default:
    return 0;
  }
}



static int 
gvp_menu_pp_field_parity(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  gvp_conf_t *gc = opaque;
  glw_t *b;
  char txt[30];

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;

    snprintf(txt, sizeof(txt), "Field parity: %s",
	     gc->gc_field_parity ? "Inverted" : "Normal");
    glw_set(b, GLW_ATTRIB_CAPTION, txt, NULL);
    return 0;

  case GLW_SIGNAL_CLICK:
    gc->gc_field_parity = !gc->gc_field_parity;
    return 1;
    
  default:
    return 0;
  }
}






static int 
gvp_menu_avsync(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  gvp_conf_t *gc = opaque;
  char buf[50];
  glw_t *b;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;
    snprintf(buf, sizeof(buf), "Video output delay: %dms", gc->gc_avcomp);

    glw_set(b, GLW_ATTRIB_CAPTION, buf, NULL);
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    switch(ie->type) {
    case INPUT_KEY:
      switch(ie->u.key) {
      case INPUT_KEY_LEFT:
	gc->gc_avcomp -= 50;
	break;
      case INPUT_KEY_RIGHT:
	gc->gc_avcomp += 50;
	break;
      default:
	break;
      }
      break;
    default:
      break;
    }
    return 1;
    
  case GLW_SIGNAL_CLICK:
    return 1;

  default:
    return 0;
  }

  va_end(ap);
  return 0;
}







static int 
gvp_menu_video_zoom(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  gvp_conf_t *gc = opaque;
  char buf[50];
  glw_t *b;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;
    snprintf(buf, sizeof(buf), "Video zoom: %d%%", gc->gc_zoom);

    glw_set(b, GLW_ATTRIB_CAPTION, buf, NULL);
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    switch(ie->type) {
    case INPUT_KEY:
      switch(ie->u.key) {
      case INPUT_KEY_LEFT:
	gc->gc_zoom -= 10;
	break;
      case INPUT_KEY_RIGHT:
	gc->gc_zoom += 10;
	break;
      default:
	break;
      }
      break;
    default:
      break;
    }
    return 1;
    
  case GLW_SIGNAL_CLICK:
    return 1;

  default:
    return 0;
  }

  va_end(ap);
  return 0;
}



glw_t *
gvp_menu_setup(glw_t *p, gvp_conf_t *gc)
{
  glw_t *v, *s;
  
  v = menu_create_submenu(p, "icon://tv.png", "Video settings", 1);


  /*** Post processor */

  s = menu_create_submenu(v, "icon://tv.png", "Postprocessor", 0);

  menu_create_item(s, "icon://menu-current.png", "No postprocessing", 
		   gvp_menu_pp, gc, GVP_PP_NONE, 0);

  menu_create_item(s, "icon://menu-current.png", "Automatic", 
		   gvp_menu_pp, gc, GVP_PP_AUTO, 0);

  menu_create_item(s, "icon://menu-current.png", "Simple deinterlacer",
		   gvp_menu_pp, gc, GVP_PP_DEINTERLACER, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif",
		   gvp_menu_pp, gc, GVP_PP_YADIF_FRAME, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif 2x",
		   gvp_menu_pp, gc, GVP_PP_YADIF_FIELD, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif NSI",
		   gvp_menu_pp, gc, GVP_PP_YADIF_FRAME_NO_SPATIAL_ILACE, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif 2x NSI",
		   gvp_menu_pp, gc, GVP_PP_YADIF_FIELD_NO_SPATIAL_ILACE, 0);


  menu_create_item(s, NULL, "Field Parity", 
		   gvp_menu_pp_field_parity, gc, 0, 0);


 /*** AV sync */

  s = menu_create_submenu(v, "icon://audio.png", "A/V Sync", 0);

  menu_create_item(s, NULL, "", gvp_menu_avsync, gc, 0, 0);


 /*** Video zoom */

  s = menu_create_submenu(v, "icon://zoom.png", "Video zoom", 0);

  menu_create_item(s, NULL, "", gvp_menu_video_zoom, gc, 0, 0);

  return v;
}


