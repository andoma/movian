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

static void vd_destroy(video_decoder_t *vd);



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
vd_init(void)
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
vd_kalman_init(gv_kalman_t *gvk)
{
  gvk->P = 1.0;
  gvk->Q = 1.0/ 100000.0;
  gvk->R = 0.01;
  gvk->x_next = 0.0;
}

static float
vd_kalman_feed(gv_kalman_t *gvk, float z) __attribute__((unused));

static float
vd_kalman_feed(gv_kalman_t *gvk, float z)
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


/**************************************************************************
 *
 *  Video decoding thread
 *
 */

static void gl_decode_video(video_decoder_t *vd, media_buf_t *mb);

void *
gl_decode_thread(void *aux)
{
  video_decoder_t *vd = aux;
  media_buf_t *mb;
  media_pipe_t *mp = vd->vd_mp;

  while(vd->vd_state == VD_STATE_THREAD_RUNNING ||
	vd->vd_state == VD_STATE_THREAD_DESTROYING) {

    mb = mb_dequeue_wait(mp, &mp->mp_video);

    switch(mb->mb_data_type) {

    case MB_EXIT:
      glw_lock();
      vd->vd_state = VD_STATE_IDLE;
      break;

    case MB_NOP:
      break;

    case MB_VIDEO:
      gl_decode_video(vd, mb);
      break;

    case MB_RESET:
    case MB_FLUSH:
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
      fprintf(stderr, "ERROR unknown hmb type %d\n", mb->mb_data_type);
      abort();
    }

    media_buf_free(mb);
  }

  pthread_mutex_lock(&mp->mp_mutex);
  mq_flush(&mp->mp_video);
  pthread_mutex_unlock(&mp->mp_mutex);
 

  if(vd->vd_flags & GLW_DESTROYED) {
    glw_deref(vd->vd_widget);
    vd_destroy(vd);
  }

  glw_unlock();
  return NULL;
}



static gl_video_frame_t *
vd_dequeue_for_decode(video_decoder_t *vd, int w[3], int h[3])
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&vd->vd_queue_mutex);
  
  while((gvf = TAILQ_FIRST(&vd->vd_avail_queue)) == NULL &&
	vd->vd_purged == 0) {
    pthread_cond_wait(&vd->vd_avail_queue_cond, &vd->vd_queue_mutex);
  }
  if(gvf == NULL)
    return NULL;

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
	vd->vd_purged == 0 )
    pthread_cond_wait(&vd->vd_bufalloced_queue_cond, &vd->vd_queue_mutex);

  if(gvf == NULL)
    return 0;

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
gl_decode_video(video_decoder_t *vd, media_buf_t *mb)
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
  vd_conf_t *gc = vd->vd_conf;
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



/**************************************************************************
 *
 *  Create video output widget
 *
 */

static int gl_video_widget_callback(glw_t *w, void *opaque, 
				    glw_signal_t signal, ...);

static void
vd_destroy(video_decoder_t *vd)
{
  int i;

  if(vd->vd_yadif_width) for(i = 0; i < 3; i++)
    avpicture_free(&vd->vd_yadif_pic[i]);

  av_free(vd->vd_frame);
  free(vd);
}


glw_t *
vd_create(glw_t *p, media_pipe_t *mp, vd_conf_t *gc, int flags)
{
  int i;
  video_decoder_t *vd;


  vd = calloc(1, sizeof(video_decoder_t));
  vd->vd_conf = gc;
  vd->vd_zoom = 100;
  vd->vd_flags = flags;

  /* For the exact meaning of these, see gl_video.h */

  TAILQ_INIT(&vd->vd_inactive_queue);
  TAILQ_INIT(&vd->vd_avail_queue);
  TAILQ_INIT(&vd->vd_displaying_queue);
  TAILQ_INIT(&vd->vd_display_queue);
  TAILQ_INIT(&vd->vd_bufalloc_queue);
  TAILQ_INIT(&vd->vd_bufalloced_queue);

  
  pthread_cond_init(&vd->vd_avail_queue_cond, NULL);
  pthread_cond_init(&vd->vd_bufalloced_queue_cond, NULL);
  pthread_mutex_init(&vd->vd_queue_mutex, NULL);
  pthread_mutex_init(&vd->vd_spill_mutex, NULL);

  vd->vd_mp = mp;

  vd->vd_umax = 1;
  vd->vd_vmax = 1;
  vd_init_timings(vd);


  for(i = 0; i < VD_FRAMES; i++)
    TAILQ_INSERT_TAIL(&vd->vd_inactive_queue, &vd->vd_frames[i], link);

  vd->vd_frame = avcodec_alloc_frame();
  vd->vd_dvdspu = gl_dvdspu_init();

  vd->vd_widget = glw_create(GLW_EXT,
			       GLW_ATTRIB_FLAGS, GLW_EVERY_FRAME,
			       GLW_ATTRIB_PARENT, p, 
			       GLW_ATTRIB_SIGNAL_HANDLER, 
			       gl_video_widget_callback, vd, 0,
			       NULL);

  return vd->vd_widget;
}


void
vd_set_dvd(glw_t *w, struct dvd_player *dvd)
{
  video_decoder_t *vd = glw_get_opaque(w, gl_video_widget_callback);
  vd->vd_dvd = dvd;
}





/**************************************************************************
 *
 *  Buffer allocator
 *
 */


static void
vd_buffer_allocator(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;
  size_t siz;

  pthread_mutex_lock(&vd->vd_queue_mutex);
  
  assert(vd->vd_active_frames_needed <= VD_FRAMES);

  while(vd->vd_active_frames < vd->vd_active_frames_needed) {
    gvf = TAILQ_FIRST(&vd->vd_inactive_queue);
    TAILQ_REMOVE(&vd->vd_inactive_queue, gvf, link);
    TAILQ_INSERT_TAIL(&vd->vd_avail_queue, gvf, link);
    pthread_cond_signal(&vd->vd_avail_queue_cond);
    vd->vd_active_frames++;
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, gvf, link);

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

    TAILQ_INSERT_TAIL(&vd->vd_bufalloced_queue, gvf, link);
    pthread_cond_signal(&vd->vd_bufalloced_queue_cond);
  }

  pthread_mutex_unlock(&vd->vd_queue_mutex);

}

/**************************************************************************
 *
 *  Texture loader
 *
 */


static void
vd_set_tex_meta(void)
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint 
vd_tex_get(video_decoder_t *vd, gl_video_frame_t *gvf, int plane)
{
  return gvf->gvf_textures[plane];
}


static void
render_video_upload(video_decoder_t *vd, gl_video_frame_t *gvf)
{
  if(gvf->gvf_uploaded)
    return;

  gvf->gvf_uploaded = 1;

  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);

  glUnmapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB);
  gvf->gvf_pbo_ptr = NULL;

  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_L));
  vd_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1, 
	       gvf->gvf_width[0], gvf->gvf_height[0],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[0]);

  /* Cr */

  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_Cr));
  vd_set_tex_meta();

  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[1], gvf->gvf_height[1],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[2]);

  /* Cb */
  
  
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, gvf, GVF_TEX_Cb));

  vd_set_tex_meta();
  
  glTexImage2D(GL_TEXTURE_2D, 0, 1,
	       gvf->gvf_width[2], gvf->gvf_height[2],
	       0, GL_RED, GL_UNSIGNED_BYTE,
	       (char *)(intptr_t)gvf->gvf_pbo_offset[1]);
  
  glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

}




/**************************************************************************
 *
 *  Video widget layout
 *
 */



static void
vd_enqueue_for_decode(video_decoder_t *vd, gl_video_frame_t *gvf, 
		       struct gl_video_frame_queue *fromqueue)
{
  
  pthread_mutex_lock(&vd->vd_queue_mutex);

  TAILQ_REMOVE(fromqueue, gvf, link);

  if(gvf->gvf_uploaded) {
    gvf->gvf_uploaded = 0;

    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, gvf->gvf_pbo);
  
    gvf->gvf_pbo_ptr = glMapBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 
				      GL_WRITE_ONLY);
    glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
  }

  TAILQ_INSERT_TAIL(&vd->vd_avail_queue, gvf, link);
  pthread_cond_signal(&vd->vd_avail_queue_cond);
  pthread_mutex_unlock(&vd->vd_queue_mutex);
}


static void
vd_enqueue_for_display(video_decoder_t *vd, gl_video_frame_t *gvf,
			struct gl_video_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvf, link);
  TAILQ_INSERT_TAIL(&vd->vd_displaying_queue, gvf, link);
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
vd_color_matrix_update(video_decoder_t *vd, media_pipe_t *mp)
{
  float *f;
  int i;

  f = mp_get_playstatus(mp) == MP_PAUSE ? cmatrix_bw : cmatrix_color;
  
  for(i = 0; i < 9; i++)
    vd->vd_cmatrix[i] = (vd->vd_cmatrix[i] * 15.0 + f[i]) / 16.0;

}





static int
vd_compute_output_duration(video_decoder_t *vd, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(vd->vd_avdiff_x > 0) {
    delta = pow(vd->vd_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(vd->vd_avdiff_x < 0) {
    delta = -pow(-vd->vd_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}

static void
vd_compute_avdiff(video_decoder_t *vd, media_pipe_t *mp, int64_t pts)
{
  if(!mp->mp_clock_valid) {
    vd->vd_avdiff_x = 0;
    vd_kalman_init(&vd->vd_avfilter);
    return;
  }

  if(vd->vd_compensate_thres > 0) {
    vd->vd_compensate_thres--;
    vd->vd_avdiff_x = 0;
    vd_kalman_init(&vd->vd_avfilter);
    return;
  }
  

  vd->vd_avdiff = mp->mp_clock - pts - vd->vd_conf->gc_avcomp * 1000;

  if(abs(vd->vd_avdiff) < 10000000) {

    vd->vd_avdiff_x = vd_kalman_feed(&vd->vd_avfilter, 
					(float)vd->vd_avdiff / 1000000);
    if(vd->vd_avdiff_x > 10.0f)
      vd->vd_avdiff_x = 10.0f;
    
    if(vd->vd_avdiff_x < -10.0f)
      vd->vd_avdiff_x = -10.0f;
  }
#if 0
  printf("%s: AVDIFF = %f %d %lld %lld\n", 
	 mp->mp_name, vd->vd_avdiff_x, vd->vd_avdiff,
	 mp->mp_clock, pts);
#endif
}


int64_t
vd_compute_blend(video_decoder_t *vd, gl_video_frame_t *fra,
		  gl_video_frame_t *frb, int output_duration)
{
  int64_t pts;
  int x;

  if(fra->gvf_duration >= output_duration) {
  
    vd->vd_fra = fra;
    vd->vd_frb = NULL;

    fra->gvf_duration -= output_duration;
    pts = fra->gvf_pts;
    fra->gvf_pts += output_duration;

  } else if(frb != NULL) {

    vd->vd_fra = fra;
    vd->vd_frb = frb;
    vd->vd_blend = (float) fra->gvf_duration / (float)output_duration;

    if(fra->gvf_duration + frb->gvf_duration < output_duration) {

      pthread_mutex_lock(&vd->vd_spill_mutex);
      x = output_duration - (fra->gvf_duration + frb->gvf_duration);
      vd->vd_spill += x;
      pthread_mutex_unlock(&vd->vd_spill_mutex);

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
    vd->vd_fra = fra;
    vd->vd_frb = NULL;
    pts = AV_NOPTS_VALUE;
  }

  return pts;
}





static void 
layout_video_pipe(video_decoder_t *vd, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = vd->vd_mp;
  int output_duration;
  int width = 0, height = 0;
  int64_t pts = 0;
  vd_conf_t *gc = vd->vd_conf;
  struct gl_video_frame_queue *dq;

  if(vd->vd_subtitle_widget)
    glw_layout(vd->vd_subtitle_widget, rc);

  vd->vd_zoom = (vd->vd_zoom * 7.0f + gc->gc_zoom) / 8.0f;


  vd->vd_rendered = 1;
  vd_color_matrix_update(vd, mp);
  output_duration = vd_compute_output_duration(vd, frame_duration);


  dq = &vd->vd_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&vd->vd_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      vd->vd_fra = fra;
      vd->vd_frb = NULL;
    } else {
      vd->vd_fra = NULL;
      vd->vd_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL)
      vd_enqueue_for_decode(vd, frb, &vd->vd_displaying_queue);

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];
      
    frb = TAILQ_NEXT(fra, link);
    pts = vd_compute_blend(vd, fra, frb, output_duration);

    if(mp_get_playstatus(mp) == MP_PLAY) {

      if(fra != NULL && fra->gvf_duration == 0)
	vd_enqueue_for_display(vd, fra, dq);

      if(frb != NULL && frb->gvf_duration == 0)
	vd_enqueue_for_display(vd, frb, dq);
    }
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    vd_compute_avdiff(vd, mp, pts);
  }
  gl_dvdspu_layout(vd->vd_dvd, vd->vd_dvdspu);
}




/**************************************************************************
 *
 *  Video widget render
 *
 */

static void
render_video_quad(media_pipe_t *mp, video_decoder_t *vd, 
		  gl_video_frame_t *gvf)
{
  
  float tzoom = vd->vd_interlaced ? 0.01 : 0.00;
  
  if(mp_get_playstatus(mp) == MP_STOP)
    return;

  glBegin(GL_QUADS);

  glTexCoord2f(tzoom, tzoom);
  glVertex3f( -1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(vd->vd_umax - tzoom, tzoom);
  glVertex3f( 1.0f, 1.0f, 0.0f);
  
  glTexCoord2f(vd->vd_umax - tzoom, vd->vd_vmax - tzoom);
  glVertex3f( 1.0f, -1.0f, 0.0f);

  glTexCoord2f(tzoom, vd->vd_vmax - tzoom); 
  glVertex3f( -1.0f, -1.0f, 0.0f);

  glEnd();
}




static void
render_video_1f(media_pipe_t *mp, video_decoder_t *vd, 
		gl_video_frame_t *gvf, float alpha)
{
  int i;
  GLuint tex;

  render_video_upload(vd, gvf);


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
				 vd->vd_cmatrix[i * 3 + 0],
				 vd->vd_cmatrix[i * 3 + 1],
				 vd->vd_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_Cb);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE1_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_Cr);
  glBindTexture(GL_TEXTURE_2D, tex);

  glActiveTextureARB(GL_TEXTURE0_ARB);
  tex = vd_tex_get(vd, gvf, GVF_TEX_L);
  glBindTexture(GL_TEXTURE_2D, tex);
  
  render_video_quad(mp, vd, gvf);
  
  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}


static void
vd_blend_frames(video_decoder_t *vd, glw_rctx_t *rc, gl_video_frame_t *fra,
		 gl_video_frame_t *frb, media_pipe_t *mp)
{
  float blend = vd->vd_blend;
  int i;
  
  render_video_upload(vd, fra);
  render_video_upload(vd, frb);
    
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
				 vd->vd_cmatrix[i * 3 + 0],
				 vd->vd_cmatrix[i * 3 + 1],
				 vd->vd_cmatrix[i * 3 + 2], 0.0f);

  glActiveTextureARB(GL_TEXTURE2_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE1_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE0_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, fra, GVF_TEX_L));

  glActiveTextureARB(GL_TEXTURE5_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_Cb));

  glActiveTextureARB(GL_TEXTURE4_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_Cr));

  glActiveTextureARB(GL_TEXTURE3_ARB);
  glBindTexture(GL_TEXTURE_2D, vd_tex_get(vd, frb, GVF_TEX_L));

  render_video_quad(mp, vd, fra);

  glDisable(GL_FRAGMENT_PROGRAM_ARB);
}




static void 
render_video_pipe(video_decoder_t *vd, glw_rctx_t *rc)
{
  gl_video_frame_t *fra, *frb;
  media_pipe_t *mp = vd->vd_mp;
  int width = 0, height = 0;

  static GLdouble clip_left[4] = {1.0, 0.0, 0.0, 1.0};
  static GLdouble clip_right[4] = {-1.0, 0.0, 0.0, 1.0};
  static GLdouble clip_bot[4] = {0.0, 1.0, 0.0, 1.0};
  static GLdouble clip_top[4] = {0.0, -1.0, 0.0, 1.0};

  /*
   * rescale
   */
 
  glPushMatrix();
  if(vd->vd_zoom != 100) {
    if(vd->vd_zoom > 100) {
      glClipPlane(GL_CLIP_PLANE0, clip_bot);
      glClipPlane(GL_CLIP_PLANE1, clip_top);
      glClipPlane(GL_CLIP_PLANE2, clip_left);
      glClipPlane(GL_CLIP_PLANE3, clip_right);
      glEnable(GL_CLIP_PLANE0);
      glEnable(GL_CLIP_PLANE1);
      glEnable(GL_CLIP_PLANE2);
      glEnable(GL_CLIP_PLANE3);
    }
    glScalef(vd->vd_zoom / 100.0f, vd->vd_zoom / 100.0f, 1.0f);
  }
  glPolygonOffset(0, rc->rc_polyoffset - 1);
  glw_scale_and_rotate(rc->rc_aspect, vd->vd_aspect, 0.0f);



  if(rc->rc_alpha < 0.98f) 
    glEnable(GL_BLEND); 
  else
    glDisable(GL_BLEND); 


  fra = vd->vd_fra;
  frb = vd->vd_frb;

  if(fra != NULL) {

    width = fra->gvf_width[0];
    height = fra->gvf_height[0];

    if(frb != NULL) {
      vd_blend_frames(vd, rc, fra, frb, mp);
    } else {
      render_video_1f(mp, vd, fra, rc->rc_alpha);
    }
  }

  glDisable(GL_BLEND); 

  if(width > 0) {
    glPolygonOffset(0, rc->rc_polyoffset - 2);
    gl_dvdspu_render(vd->vd_dvdspu, width, height, rc->rc_alpha);
  }

  if(vd->vd_zoom > 100) {
    glDisable(GL_CLIP_PLANE0);
    glDisable(GL_CLIP_PLANE1);
    glDisable(GL_CLIP_PLANE2);
    glDisable(GL_CLIP_PLANE3);
  }

  glPopMatrix();

  if(vd->vd_subtitle_widget)
    glw_render(vd->vd_subtitle_widget, rc);

}

/*
 * 
 */


static void
gvf_purge(video_decoder_t *vd, gl_video_frame_t *gvf)
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

  TAILQ_INSERT_TAIL(&vd->vd_inactive_queue, gvf, link);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
}



static void
vd_purge(video_decoder_t *vd)
{
  gl_video_frame_t *gvf;

  pthread_mutex_lock(&vd->vd_queue_mutex);

  while((gvf = TAILQ_FIRST(&vd->vd_avail_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_avail_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloced_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_displaying_queue, gvf, link);
    gvf_purge(vd, gvf);
  }

  while((gvf = TAILQ_FIRST(&vd->vd_display_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_display_queue, gvf, link);
    gvf_purge(vd, gvf);
  }
  vd->vd_purged = 1;
  pthread_cond_signal(&vd->vd_avail_queue_cond);
  pthread_cond_signal(&vd->vd_bufalloced_queue_cond);
  
  pthread_mutex_unlock(&vd->vd_queue_mutex);
}



static void
gl_constant_frame_flush(video_decoder_t *vd)
{
  gl_video_frame_t *fra;

  fra = TAILQ_FIRST(&vd->vd_displaying_queue);
  if(fra != NULL) {
    assert(fra->gvf_pbo_ptr == NULL);
    vd_enqueue_for_decode(vd, fra, &vd->vd_displaying_queue);
  } else {
    fra = TAILQ_FIRST(&vd->vd_display_queue);
    if(fra != NULL) {
      vd_enqueue_for_decode(vd, fra, &vd->vd_display_queue);
    }
  }
}


static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  pthread_attr_t attr;
  video_decoder_t *vd = opaque;
  media_pipe_t *mp = vd->vd_mp;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_DTOR:
    vd_purge(vd);

    gl_dvdspu_deinit(vd->vd_dvdspu);
    vd->vd_dvdspu = NULL;

    switch(vd->vd_state) {
    case VD_STATE_IDLE:
      vd_destroy(vd);
      return 0;
    case VD_STATE_THREAD_RUNNING:
      mp_send_cmd(mp, &mp->mp_video, MB_EXIT);
      /* FALLTHRU */
    case VD_STATE_THREAD_DESTROYING:
      glw_ref(vd->vd_widget);
      return 0;
    }
    return 0;


  case GLW_SIGNAL_LAYOUT:
    glw_set_active(w);

    switch(vd->vd_state) {
    case VD_STATE_IDLE:
      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
      pthread_create(&vd->vd_decode_thrid, &attr, gl_decode_thread, vd);
      vd->vd_state = VD_STATE_THREAD_RUNNING;
      /* FALLTHRU */
    case VD_STATE_THREAD_RUNNING:
      layout_video_pipe(vd, va_arg(ap, void *));
      break;

    case VD_STATE_THREAD_DESTROYING:
      return 0;
    }
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video_pipe(vd, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_INACTIVE:
    mp_send_cmd(mp, &mp->mp_video, MB_EXIT);
    vd->vd_state = VD_STATE_THREAD_DESTROYING;
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    vd_buffer_allocator(vd);

    switch(vd->vd_state) {
    case VD_STATE_IDLE:
    case VD_STATE_THREAD_DESTROYING:
      gl_constant_frame_flush(vd);
      return 0;
    case VD_STATE_THREAD_RUNNING:
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
vd_menu_pp(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  vd_conf_t *gc = opaque;
  glw_t *b;
  float v;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_BITMAP)) == NULL)
      return 0;
    
    v = w->glw_u32 == gc->gc_deilace_type ? 1 : 0;
    b->glw_alpha = (b->glw_alpha * 15 + v) / 16.0;
    return 0;

  case GLW_SIGNAL_CLICK:
    gc->gc_deilace_type = w->glw_u32;
    return 1;
    
  default:
    return 0;
  }
}



static int 
vd_menu_pp_field_parity(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  vd_conf_t *gc = opaque;
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
vd_menu_avsync(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  vd_conf_t *gc = opaque;
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
vd_menu_video_zoom(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  vd_conf_t *gc = opaque;
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
vd_menu_setup(glw_t *p, vd_conf_t *gc)
{
  glw_t *v, *s;
  
  v = menu_create_submenu(p, "icon://tv.png", "Video settings", 1);


  /*** Post processor */

  s = menu_create_submenu(v, "icon://tv.png", "Deinterlacer", 0);

  menu_create_item(s, "icon://menu-current.png", "No deinterlacing", 
		   vd_menu_pp, gc, VD_DEILACE_NONE, 0);

  menu_create_item(s, "icon://menu-current.png", "Automatic", 
		   vd_menu_pp, gc, VD_DEILACE_AUTO, 0);

  menu_create_item(s, "icon://menu-current.png", "Simple",
		   vd_menu_pp, gc, VD_DEILACE_HALF_RES, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif",
		   vd_menu_pp, gc, VD_DEILACE_YADIF_FRAME, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif 2x",
		   vd_menu_pp, gc, VD_DEILACE_YADIF_FIELD, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif NSI",
		   vd_menu_pp, gc, 
		   VD_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE, 0);
  menu_create_item(s, "icon://menu-current.png", "Yadif 2x NSI",
		   vd_menu_pp, gc, 
		   VD_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE, 0);


  menu_create_item(s, NULL, "Field Parity", 
		   vd_menu_pp_field_parity, gc, 0, 0);


 /*** AV sync */

  s = menu_create_submenu(v, "icon://audio.png", "A/V Sync", 0);

  menu_create_item(s, NULL, "", vd_menu_avsync, gc, 0, 0);


 /*** Video zoom */

  s = menu_create_submenu(v, "icon://zoom.png", "Video zoom", 0);

  menu_create_item(s, NULL, "", vd_menu_video_zoom, gc, 0, 0);

  return v;
}


