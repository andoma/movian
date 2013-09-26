/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <sys/ioctl.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <libve.h>

#include "showtime.h"
#include "glw_video_common.h"

#include "video/video_decoder.h"
#include "video/video_playback.h"
#include "video/cedar.h"

#include "arch/sunxi/sunxi.h"


#define DISPMAN_VIDEO_YUV420P 1
#define DISPMAN_VIDEO_CEDAR   2

typedef struct dispman_video {

  int dv_layer;
  int dv_idgen;
  int dv_running;

  // Current mode of scaler
  int dv_width;
  int dv_height;
  int dv_format;

  int64_t last_frame_start;
  int64_t last_aclock;

} dispman_video_t;



/**
 *
 */
static void
surface_free(glw_video_t *gv, glw_video_surface_t *gvs)
{
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  tlsf_free(sunxi.gfxmem, gvs->gvs_data[0]);
  tlsf_free(sunxi.gfxmem, gvs->gvs_data[1]);
  tlsf_free(sunxi.gfxmem, gvs->gvs_data[2]);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  gvs->gvs_data[0] = NULL;
  gvs->gvs_data[1] = NULL;
  gvs->gvs_data[2] = NULL;
}


/**
 *
 */
static void
video_reset_common(glw_video_t *gv)
{
  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;
  unsigned long args[4] = {0};
  args[1] = dv->dv_layer;
  assert(dv->dv_layer >= 100 && dv->dv_layer < 105);
  ioctl(sunxi.dispfd, DISP_CMD_VIDEO_STOP, args);
  ioctl(sunxi.dispfd, DISP_CMD_LAYER_CLOSE, args);
  ioctl(sunxi.dispfd, DISP_CMD_LAYER_RELEASE, args);

  TRACE(TRACE_DEBUG, "GLW", "%s: Released layer %d",
	gv->gv_mp->mp_name, dv->dv_layer);
  free(dv);
  gv->gv_aux = NULL;
}


/**
 *
 */
static void
video_reset_free(glw_video_t *gv)
{
  int i;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_free(gv, &gv->gv_surfaces[i]);
  video_reset_common(gv);
}


/**
 *
 */
static void
video_reset_cedar(glw_video_t *gv)
{
  int i;
  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    if(gv->gv_surfaces[i].gvs_opaque) {
      cedar_frame_done(gv->gv_surfaces[i].gvs_opaque);
      gv->gv_surfaces[i].gvs_opaque = NULL;
    }
  video_reset_common(gv);
}

/**
 *
 */
static void
cedar2_frame_release(glw_video_surface_t *gvs)
{
  if(gvs->gvs_data[2] == NULL)
    return;

  void (*refop)(void *data2, int delta) = gvs->gvs_opaque;
  
  refop(gvs->gvs_data[2], -1);
  gvs->gvs_data[2] = NULL;
}

/**
 *
 */
static void
video_reset_cedar2(glw_video_t *gv)
{
  int i;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    cedar2_frame_release(&gv->gv_surfaces[i]);

  video_reset_common(gv);
}

/**
 *
 */
static void
video_set_param(dispman_video_t *dv, glw_video_surface_t *gvs,
		const media_pipe_t *mp)
{
  unsigned long args[4] = {0};
  int r;
  __disp_layer_info_t l;

  if(gvs->gvs_width[0]  == dv->dv_width &&
     gvs->gvs_height[0] == dv->dv_height &&
     gvs->gvs_format    == dv->dv_format)
    return;


  dv->dv_width =  gvs->gvs_width[0];
  dv->dv_height = gvs->gvs_height[0];
  dv->dv_format = gvs->gvs_format;

  memset(&l, 0, sizeof(l));
    
  l.mode = DISP_LAYER_WORK_MODE_SCALER;
  l.pipe = 1;

  l.fb.size.width  = gvs->gvs_width[0];
  l.fb.size.height = gvs->gvs_height[0];
  l.fb.br_swap       = 0;
  //  l.fb.cs_mode = DISP_BT601;

  switch(gvs->gvs_format) {
  case DISPMAN_VIDEO_YUV420P:
    l.fb.mode   = DISP_MOD_NON_MB_PLANAR;
    l.fb.format = DISP_FORMAT_YUV420;
    break;

  case DISPMAN_VIDEO_CEDAR:
    l.fb.mode   = DISP_MOD_MB_UV_COMBINED;
    l.fb.format = DISP_FORMAT_YUV420;
    l.fb.seq    = DISP_SEQ_UVUV;
    break;

  default:
    abort();
  }


  l.ck_enable        = 0;
  l.alpha_en         = 0;
  l.alpha_val        = 0xff;
  l.src_win.x        = 0;
  l.src_win.y        = 0;
  l.src_win.width    = gvs->gvs_width[0];
  l.src_win.height   = gvs->gvs_height[0];
  l.scn_win.x        = 0;
  l.scn_win.y        = 0;
  l.scn_win.width    = 1280; // HUH
  l.scn_win.height   = 720;

  TRACE(TRACE_DEBUG, "GLW", "%s: Video surface set to %d x %d",
	mp->mp_name, l.src_win.width, l.src_win.height);

  args[1] = dv->dv_layer;
  args[2] = (__u32)&l;
  args[3] = 0;
  r = ioctl(sunxi.dispfd,DISP_CMD_LAYER_SET_PARA,(void*)args);
  if(r)
    perror("ioctl(disphd,DISP_CMD_LAYER_SET_PARA)");
}


/**
 *
 */
static int
video_init(glw_video_t *gv)
{
  unsigned long args[4] = {0};
  int scr = 0;
  int i;

  args[0] = scr;
  args[1] = DISP_LAYER_WORK_MODE_SCALER;
  int hlay = ioctl(sunxi.dispfd, DISP_CMD_LAYER_REQUEST, args);
  if(hlay == -1)
    return -1;

  dispman_video_t *dv = calloc(1, sizeof(dispman_video_t));

  gv->gv_aux = dv;

  for(i = 0; i < 3; i++) {
    glw_video_surface_t *gvs = &gv->gv_surfaces[i];
    gvs->gvs_data[0] = NULL;
    gvs->gvs_data[1] = NULL;
    gvs->gvs_data[2] = NULL;
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  }

  dv->dv_layer = hlay;
  TRACE(TRACE_INFO, "GLW", "%s: Got layer %d for output",
	gv->gv_mp->mp_name, hlay);

  return 0;
}


/**
 *
 */
static int64_t
video_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *s;
  unsigned long args[4] = {0};
  media_pipe_t *mp = gv->gv_mp;
  int64_t pts = AV_NOPTS_VALUE;
  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;

  if(dv->dv_layer) {

    args[1] = dv->dv_layer;

    int curnr = ioctl(sunxi.dispfd, DISP_CMD_VIDEO_GET_FRAME_ID, args);
    /**
     * Remove frames if they are idle and push back to the decoder 
     */
    while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL) {
      if(s->gvs_id >= curnr)
	break;
      TAILQ_REMOVE(&gv->gv_displaying_queue, s, gvs_link);
      TAILQ_INSERT_TAIL(&gv->gv_avail_queue, s, gvs_link);
      hts_cond_signal(&gv->gv_avail_queue_cond);
    }
  }

  int64_t aclock;
  int aepoch;
  hts_mutex_lock(&mp->mp_clock_mutex);
  aclock = mp->mp_audio_clock + gr->gr_frame_start_avtime - 
    mp->mp_audio_clock_avtime + mp->mp_avdelta;

  const int aclock_valid = !!mp->mp_audio_clock_epoch;

  aepoch = mp->mp_audio_clock_epoch;

  hts_mutex_unlock(&mp->mp_clock_mutex);
  if(0) {
    printf("%10s: %10lld %10lld %s\n",
	   mp->mp_name,
	   gr->gr_frame_start_avtime - dv->last_frame_start,
	   aclock - dv->last_aclock,
	   TAILQ_FIRST(&gv->gv_decoded_queue) ? "available frames" : "");
  
    dv->last_aclock = aclock;
    dv->last_frame_start = gr->gr_frame_start_avtime;
  }

  while((s = TAILQ_FIRST(&gv->gv_decoded_queue)) != NULL) {
    int64_t delta = gr->gr_frameduration * 2;
    int64_t d;
    pts = s->gvs_pts;
    int epoch = s->gvs_epoch;

    d = s->gvs_pts - aclock;

    if(gv->gv_nextpts_epoch == epoch &&
       (pts == AV_NOPTS_VALUE || d < -5000000LL || d > 5000000LL)) {
      pts = gv->gv_nextpts;
    }

    if(pts != AV_NOPTS_VALUE && (pts - delta) >= aclock && aclock_valid) {

      if(gconf.enable_detailed_avdiff)
        TRACE(TRACE_DEBUG, "AVDIFF",
              "%s: Not sending frame %d:%lld %d:%lld diff:%lld\n",
              mp->mp_name,
              s->gvs_epoch,
              (pts - delta), aepoch, aclock,
              (pts - delta) - aclock);

      break;
    } else {
      if(gconf.enable_detailed_avdiff)
        TRACE(TRACE_DEBUG, "AVDIFF",
              "%s:     Sending frame %d:%lld %d:%lld\n",
             mp->mp_name, s->gvs_epoch,
             (pts - delta), aepoch, aclock);
    }
    video_set_param(dv, s, mp);

    if(!dv->dv_running) {

      args[1] = dv->dv_layer;
      if(ioctl(sunxi.dispfd,DISP_CMD_LAYER_OPEN,(void*)args)) {
	TRACE(TRACE_ERROR, "GLW", "%s: Failed to open layer %d -- %s",
	      mp->mp_name, dv->dv_layer, strerror(errno));
	return PTS_UNSET;
      }
      
      args[1] = dv->dv_layer;
      if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_BOTTOM, args)) {
	TRACE(TRACE_ERROR, "GLW", "%s: Failed to set layer %d zorder -- %s",
	      mp->mp_name, dv->dv_layer, strerror(errno));
	ioctl(sunxi.dispfd, DISP_CMD_LAYER_CLOSE, args);
	return PTS_UNSET;
      }

      args[1] = dv->dv_layer;
      if(ioctl(sunxi.dispfd, DISP_CMD_VIDEO_START, args)) {
	TRACE(TRACE_ERROR, "GLW", "%s: Failed to start video on layer %d -- %s",
	      mp->mp_name, dv->dv_layer, strerror(errno));
	ioctl(sunxi.dispfd, DISP_CMD_LAYER_CLOSE, args);
	return PTS_UNSET;
      }
    }


    __disp_video_fb_t   frmbuf;
    memset(&frmbuf, 0, sizeof(__disp_video_fb_t));
    frmbuf.interlace       = 0; // !pic->pic.is_progressive;
    frmbuf.top_field_first = 0; // pic->pic.top_field_first;
    frmbuf.addr[0]         = va_to_pa(s->gvs_data[0]);//+ 0x40000000;
    frmbuf.addr[1]         = va_to_pa(s->gvs_data[1]);//+ 0x40000000;
    frmbuf.addr[2]         = va_to_pa(s->gvs_data[2]);//+ 0x40000000;
    frmbuf.id              = s->gvs_id;
    
    args[1] = dv->dv_layer;

    args[2] = (intptr_t)&frmbuf;

    if(ioctl(sunxi.dispfd, DISP_CMD_VIDEO_SET_FB, args)) {
      TRACE(TRACE_ERROR, "GLW", "%s: Failed to set FB on layer %d -- %s",
	    mp->mp_name, dv->dv_layer, strerror(errno));
      args[2] = 0;
      ioctl(sunxi.dispfd, DISP_CMD_LAYER_CLOSE, args);
      return PTS_UNSET;
    }

    dv->dv_running = 1;

    if(pts != AV_NOPTS_VALUE) {
      gv->gv_nextpts = pts + s->gvs_duration;
      gv->gv_nextpts_epoch = epoch;
    }

    gv->gv_width  = s->gvs_width[0];
    gv->gv_height = s->gvs_height[0];

    TAILQ_REMOVE(&gv->gv_decoded_queue, s, gvs_link);
    TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, s, gvs_link);
  }
  return pts;
}


/**
 *
 */
static void
video_render(glw_video_t *gv, glw_rctx_t *rc)
{
  hts_mutex_lock(&gv->gv_surface_mutex);

  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;

  if(dv->dv_layer != 0 &&
     gv->gv_rect.x2 > gv->gv_rect.x1 &&
     gv->gv_rect.y2 > gv->gv_rect.y1) {

    __disp_rect_t rect;
    rect.x      = gv->gv_rect.x1;
    rect.y      = gv->gv_rect.y1;
    rect.width  = gv->gv_rect.x2 - gv->gv_rect.x1;
    rect.height = gv->gv_rect.y2 - gv->gv_rect.y1;

    unsigned long args[4] = {0};
    args[0] = 0;
    args[1] = dv->dv_layer;
    args[2] = (intptr_t)&rect;

    ioctl(sunxi.dispfd, DISP_CMD_LAYER_SET_SCN_WINDOW, &args);
  }
  hts_mutex_unlock(&gv->gv_surface_mutex);
}


static void deliver_yuvp(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_video_sunxi = {
  .gve_type     = 'YUVP',
  .gve_newframe = video_newframe,
  .gve_render   = video_render,
  .gve_reset    = video_reset_free,
  .gve_init     = video_init,
  .gve_deliver  = deliver_yuvp,
};

GLW_REGISTER_GVE(glw_video_sunxi);

/**
 *
 */
static void
deliver_yuvp(const frame_info_t *fi, glw_video_t *gv)
{
  glw_video_surface_t *gvs;

  if(glw_video_configure(gv, &glw_video_sunxi))
    return;

  if((gvs = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return;

  surface_free(gv, gvs);
  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;

  gvs->gvs_id = ++dv->dv_idgen;
  gvs->gvs_format = DISPMAN_VIDEO_YUV420P;
  gvs->gvs_width[0]  = fi->fi_width;
  gvs->gvs_height[0] = fi->fi_height;

  int i, y;
  for(i = 0; i < 3; i++) {
    int width  = fi->fi_width;
    int height = fi->fi_height;
    if(i) {
      width >>= fi->fi_hshift;
      height >>= fi->fi_vshift;
    }
    
    hts_mutex_lock(&sunxi.gfxmem_mutex);
    uint8_t *dst = tlsf_memalign(sunxi.gfxmem, 1024, width * height);
    hts_mutex_unlock(&sunxi.gfxmem_mutex);
    gvs->gvs_data[i] = dst;
    const uint8_t *src = fi->fi_data[i];
    for(y = 0; y < height; y++) {
      memcpy(dst, src, width);
      dst += width;
      src += fi->fi_pitch[i];
    }
  }

  glw_video_put_surface(gv, gvs, fi->fi_pts, fi->fi_epoch,
			fi->fi_duration, 0, 0);
}

static void cedar_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_video_sunxi_cedar = {
  .gve_type     = 'CEDR',
  .gve_newframe = video_newframe,
  .gve_render   = video_render,
  .gve_reset    = video_reset_cedar,
  .gve_init     = video_init,
  .gve_deliver  = cedar_deliver,
};

GLW_REGISTER_GVE(glw_video_sunxi_cedar);

#include "video/cedar.h"

static void
cedar_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  glw_video_surface_t *gvs;

  if(glw_video_configure(gv, &glw_video_sunxi_cedar)) {
    cedar_frame_done(fi->fi_data[3]);
    return;
  }

  if((gvs = glw_video_get_surface(gv, NULL, NULL)) == NULL) {
    cedar_frame_done(fi->fi_data[3]);
    return;
  }

  if(gvs->gvs_opaque) {
    cedar_frame_done(gvs->gvs_opaque);
    gvs->gvs_opaque = NULL;
  }

  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;

  gvs->gvs_id = ++dv->dv_idgen;
  gvs->gvs_format = DISPMAN_VIDEO_CEDAR;
  gvs->gvs_width[0]  = fi->fi_width;
  gvs->gvs_height[0] = fi->fi_height;

  gvs->gvs_id = ++dv->dv_idgen;
  gvs->gvs_data[0] = fi->fi_data[0];
  gvs->gvs_data[1] = fi->fi_data[1];
  gvs->gvs_data[2] = fi->fi_data[2];
  gvs->gvs_opaque  = fi->fi_data[3];

  glw_video_put_surface(gv, gvs, fi->fi_pts, fi->fi_epoch,
			fi->fi_duration, 0, 0);
}




static void cedar2_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_video_sunxi_cedar2 = {
  .gve_type     = 'CED2',
  .gve_newframe = video_newframe,
  .gve_render   = video_render,
  .gve_reset    = video_reset_cedar2,
  .gve_init     = video_init,
  .gve_deliver  = cedar2_deliver,
};

GLW_REGISTER_GVE(glw_video_sunxi_cedar2);

static void
cedar2_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  glw_video_surface_t *gvs;

  if(glw_video_configure(gv, &glw_video_sunxi_cedar2))
    return;

  if((gvs = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return;

  cedar2_frame_release(gvs);

  dispman_video_t *dv = (dispman_video_t *)gv->gv_aux;

  gvs->gvs_id = ++dv->dv_idgen;
  gvs->gvs_format = DISPMAN_VIDEO_CEDAR;
  gvs->gvs_width[0]  = fi->fi_width;
  gvs->gvs_height[0] = fi->fi_height;

  gvs->gvs_id = ++dv->dv_idgen;
  gvs->gvs_data[0] = fi->fi_data[0];
  gvs->gvs_data[1] = fi->fi_data[1];
  gvs->gvs_data[2] = fi->fi_data[2];

  gvs->gvs_opaque  = fi->fi_refop;

  fi->fi_refop(gvs->gvs_data[2], 1);

  glw_video_put_surface(gv, gvs, fi->fi_pts, fi->fi_epoch,
			fi->fi_duration, 0, 0);
}
