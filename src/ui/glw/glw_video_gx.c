/*
 *  Video output on GL surfaces
 *  Copyright (C) 2009 Andreas Öman
 *
 *  Based on gx_supp.c from Mplayer CE/TT et al.
 *
 *      softdev 2007
 *	dhewg 2008
 *	sepp256 2008 - Coded YUV->RGB conversion in TEV.
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
#include <malloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "media.h"
#include "glw_video_gx.h"
#include "misc/perftimer.h"

static void glw_video_frame_deliver(video_decoder_t *vd, AVCodecContext *ctx,
				    AVFrame *frame, int64_t pts, int epoch, 
				    int duration, int disable_deinterlacer);


/**
 *  Buffer allocator
 */
static void
gv_buffer_allocator(video_decoder_t *vd)
{
  gx_video_frame_t *gvf;
  video_decoder_frame_t *vdf;
  int i;

  hts_mutex_lock(&vd->vd_queue_mutex);
  
  while(vd->vd_active_frames < vd->vd_active_frames_needed) {
    vdf = calloc(1, sizeof(gx_video_frame_t));
    TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_avail_queue_cond);
    vd->vd_active_frames++;
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);


    gvf = (gx_video_frame_t *)vdf;

    for(i = 0; i < 3; i++) {
      gvf->gvf_size[i] = vdf->vdf_width[i] * vdf->vdf_height[i];
      gvf->gvf_mem[i]  = memalign(32, gvf->gvf_size[i]);

      TRACE(TRACE_DEBUG, "Wii", "%p[%d]: Alloc %d bytes = %p", 
	    gvf, i, gvf->gvf_size[i], gvf->gvf_mem[i]);

      if(gvf->gvf_mem[i] == NULL)
	abort();
      
      vdf->vdf_data[i] = gvf->gvf_mem[i];

      GX_InitTexObj(&gvf->gvf_obj[i], gvf->gvf_mem[i], 
		    vdf->vdf_width[i], vdf->vdf_height[i],
		    GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    }

    TAILQ_INSERT_TAIL(&vd->vd_bufalloced_queue, vdf, vdf_link);
    hts_cond_signal(&vd->vd_bufalloced_queue_cond);
  }

  hts_mutex_unlock(&vd->vd_queue_mutex);

}


/**
 *  Video widget layout
 */
static void
gv_enqueue_for_decode(video_decoder_t *vd, video_decoder_frame_t *vdf,
		      struct video_decoder_frame_queue *fromqueue)
{
  hts_mutex_lock(&vd->vd_queue_mutex);

  TAILQ_REMOVE(fromqueue, vdf, vdf_link);

  TAILQ_INSERT_TAIL(&vd->vd_avail_queue, vdf, vdf_link);
  hts_cond_signal(&vd->vd_avail_queue_cond);
  hts_mutex_unlock(&vd->vd_queue_mutex);
}


static void
gv_enqueue_for_display(video_decoder_t *vd, video_decoder_frame_t *vdf,
		       struct video_decoder_frame_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, vdf, vdf_link);
  TAILQ_INSERT_TAIL(&vd->vd_displaying_queue, vdf, vdf_link);
}


static int
compute_output_duration(video_decoder_t *vd, int frame_duration)
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
compute_avdiff(video_decoder_t *vd, media_pipe_t *mp, int64_t pts, int epoch)
{
  int64_t rt;
  int64_t aclock;

  if(mp->mp_audio_clock_epoch != epoch) {
    /* Not the same clock epoch, can not sync */
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }

  if(vd->vd_compensate_thres > 0) {
    vd->vd_compensate_thres--;
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }
  

  hts_mutex_lock(&mp->mp_clock_mutex);

  rt = showtime_get_ts();

  aclock = mp->mp_audio_clock + rt - mp->mp_audio_clock_realtime;

  hts_mutex_unlock(&mp->mp_clock_mutex);

  aclock += mp->mp_avdelta;

  vd->vd_avdiff = aclock - (pts - 16666) - vd->vd_avd_delta;

  if(abs(vd->vd_avdiff) < 10000000) {

    vd->vd_avdiff_x = kalman(&vd->vd_avfilter, (float)vd->vd_avdiff / 1000000);
    if(vd->vd_avdiff_x > 10.0f)
      vd->vd_avdiff_x = 10.0f;
    
    if(vd->vd_avdiff_x < -10.0f)
      vd->vd_avdiff_x = -10.0f;
  }

#if 0
 {
   static int64_t lastpts, lastaclock;
   
   TRACE(TRACE_DEBUG, "AVDIFF", 
	 "%s: %10f %10d %15lld %15lld %15lld %15lld %15lld\n", 
	 mp->mp_name, vd->vd_avdiff_x, vd->vd_avdiff,
	 aclock, aclock - lastaclock, pts, pts - lastpts,
	 mp->mp_audio_clock);
  lastpts = pts;
  lastaclock = aclock;
 }
#endif
}


static int64_t
gv_compute_blend(glw_video_t *gv, video_decoder_frame_t *fra,
		 video_decoder_frame_t *frb, int output_duration)
{
  int64_t pts;
  int x;

  gv->gv_fra = fra;

  //  TRACE(TRACE_DEBUG, "glw", "duration=%d, od=%d", fra->vdf_duration, output_duration);

  if(fra->vdf_duration >= output_duration) {
  
    pts = fra->vdf_pts;

    fra->vdf_duration -= output_duration;
    fra->vdf_pts      += output_duration;

  } else if(frb != NULL) {

    if(fra->vdf_duration + frb->vdf_duration < output_duration) {

      pts = frb->vdf_pts;

    } else {
      pts = fra->vdf_pts;
      x = output_duration - fra->vdf_duration;
      frb->vdf_duration -= x;
      frb->vdf_pts      += x;
    }
    fra->vdf_duration = 0;

  } else {
    gv->gv_fra = fra;
    gv->gv_frb = NULL;
    fra->vdf_pts      += output_duration;

    pts = fra->vdf_pts;
  }

  return pts;
}



#if ENABLE_DVD
/**
 *
 */
static void
spu_repaint(glw_video_t *gv, dvdspu_decoder_t *dd, dvdspu_t *d,
	    const glw_root_t *gr)
{
  int width  = d->d_x2 - d->d_x1;
  int height = d->d_y2 - d->d_y1;
  int outsize = width * height * 4;
  uint32_t *tmp, *t0; 
  int x, y, i;
  uint8_t *buf = d->d_bitmap;
  pci_t *pci = &dd->dd_pci;
  dvdnav_highlight_area_t ha;
  int hi_palette[4];
  int hi_alpha[4];

  if(dd->dd_clut == NULL)
    return;
  
  gv->gv_in_menu = pci->hli.hl_gi.hli_ss;

  if(pci->hli.hl_gi.hli_ss &&
     dvdnav_get_highlight_area(pci, dd->dd_curbut, 0, &ha) 
     == DVDNAV_STATUS_OK) {

    hi_alpha[0] = (ha.palette >>  0) & 0xf;
    hi_alpha[1] = (ha.palette >>  4) & 0xf;
    hi_alpha[2] = (ha.palette >>  8) & 0xf;
    hi_alpha[3] = (ha.palette >> 12) & 0xf;
     
    hi_palette[0] = (ha.palette >> 16) & 0xf;
    hi_palette[1] = (ha.palette >> 20) & 0xf;
    hi_palette[2] = (ha.palette >> 24) & 0xf;
    hi_palette[3] = (ha.palette >> 28) & 0xf;
  }

  t0 = tmp = malloc(outsize);


  ha.sx -= d->d_x1;
  ha.ex -= d->d_x1;

  ha.sy -= d->d_y1;
  ha.ey -= d->d_y1;

  /* XXX: this can be optimized in many ways */

  for(y = 0; y < height; y++) {
    for(x = 0; x < width; x++) {
      i = buf[0];

      if(pci->hli.hl_gi.hli_ss &&
	 x >= ha.sx && y >= ha.sy && x <= ha.ex && y <= ha.ey) {

	if(hi_alpha[i] == 0) {
	  *tmp = 0;
	} else {
	  *tmp = dd->dd_clut[hi_palette[i] & 0xf] | 
	    ((hi_alpha[i] * 0x11) << 24);
	}

      } else {

	if(d->d_alpha[i] == 0) {
	  
	  /* If it's 100% transparent, write RGB as zero too, or weird
	     aliasing effect will occure when GL scales texture */
	  
	  *tmp = 0;
	} else {
	  *tmp = dd->dd_clut[d->d_palette[i] & 0xf] | 
	    ((d->d_alpha[i] * 0x11) << 24);
	}
      }

      buf++;
      tmp++;
    }
  }

  gv->gv_sputex_width  = gr->gr_normalized_texture_coords ? 1.0 : width;
  gv->gv_sputex_height = gr->gr_normalized_texture_coords ? 1.0 : height;

  glTexImage2D(gr->gr_be.gbr_primary_texture_mode, 0, GL_RGBA, 
	       width, height, 0,
	       GL_RGBA, GL_UNSIGNED_BYTE, t0);

  free(t0);
}




static void
spu_layout(glw_video_t *gv, dvdspu_decoder_t *dd, int64_t pts,
	   const glw_root_t *gr)
{
  int textype = gr->gr_be.gbr_primary_texture_mode;
  dvdspu_t *d;
  int x;

  hts_mutex_lock(&dd->dd_mutex);

 again:
  d = TAILQ_FIRST(&dd->dd_queue);

  if(d == NULL) {
    hts_mutex_unlock(&dd->dd_mutex);
    return;
  }

  if(d->d_destroyme == 1)
    goto destroy;

  x = dvdspu_decode(d, pts);

  switch(x) {
  case -1:
  destroy:
    dvdspu_destroy(dd, d);
    gv->gv_in_menu = 0;

    glDeleteTextures(1, &gv->gv_sputex);
    gv->gv_sputex = 0;
    goto again;

  case 0:
    if(dd->dd_repaint == 0)
      break;

    dd->dd_repaint = 0;
    /* FALLTHRU */

  case 1:
    if(gv->gv_sputex == 0) {

      glGenTextures(1, &gv->gv_sputex);

      glBindTexture(textype, gv->gv_sputex);
      glTexParameterf(textype, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(textype, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    } else {
      glBindTexture(textype, gv->gv_sputex);
    }
    spu_repaint(gv, dd, d, gr);
    break;
  }
  hts_mutex_unlock(&dd->dd_mutex);
}
#endif



static void 
gv_new_frame(video_decoder_t *vd, glw_video_t *gv, const glw_root_t *gr)
{
  video_decoder_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  struct video_decoder_frame_queue *dq;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  output_duration = compute_output_duration(vd, frame_duration);

  dq = &vd->vd_display_queue;

  /* Find frame to display */

  fra = TAILQ_FIRST(dq);
  if(fra == NULL) {
    /* No frame available */
    fra = TAILQ_FIRST(&vd->vd_displaying_queue);
    if(fra != NULL) {
      /* Continue to display last frame */
      gv->gv_fra = fra;
      gv->gv_frb = NULL;
    } else {
      gv->gv_fra = NULL;
      gv->gv_frb = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
      
    while((frb = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL)
      gv_enqueue_for_decode(vd, frb, &vd->vd_displaying_queue);
    
    frb = TAILQ_NEXT(fra, vdf_link);

    pts = gv_compute_blend(gv, fra, frb, output_duration);
    epoch = fra->vdf_epoch;

    if(!vd->vd_hold || frb != NULL) {
      if(fra != NULL && fra->vdf_duration == 0)
	gv_enqueue_for_display(vd, fra, dq);
    }
    if(frb != NULL && frb->vdf_duration == 0)
      gv_enqueue_for_display(vd, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    compute_avdiff(vd, mp, pts, epoch);

#if ENABLE_DVD
    if(vd->vd_dvdspu != NULL)
      spu_layout(gv, vd->vd_dvdspu, pts, gr);
#endif
  }
}



/**
 * Render
 */
static void
render_video_1f(glw_video_t *gv, video_decoder_t *vd,
		video_decoder_frame_t *vdf, glw_rctx_t *rc)
{
  gx_video_frame_t *gvf = (gx_video_frame_t *)vdf;

  GX_LoadPosMtxImm(rc->rc_be.gbr_model_matrix, GX_PNMTX0);
    
 // setup the vertex descriptor
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX1, GX_DIRECT);


  GX_SetNumTexGens(3);

  // 0 not needed?
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, GX_IDENTITY);
  

  //Y'UV->RGB formulation 2
  GX_SetNumTevStages(12);
  GX_SetTevKColor(GX_KCOLOR0, (GXColor) {255,   0,   0,  19});	//R {1, 0, 0, 16*1.164}
  GX_SetTevKColor(GX_KCOLOR1, (GXColor) {  0,   0, 255,  42});	//B {0, 0, 1, 0.164}
  GX_SetTevKColor(GX_KCOLOR2, (GXColor) {204,  104,   0, 255});	// {1.598/2, 0.813/2, 0}
  GX_SetTevKColor(GX_KCOLOR3, (GXColor) {  0,  25, 129, 255});	// {0, 0.391/4, 2.016/4}
  //Stage 0: TEVREG0 <- { 0, 2Um, 2Up }; TEVREG0A <- {16*1.164}
  GX_SetTevKColorSel(GX_TEVSTAGE0,GX_TEV_KCSEL_K1);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1,GX_COLOR0A0);
  GX_SetTevColorIn (GX_TEVSTAGE0, GX_CC_RASC, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
  GX_SetTevColorOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_2, GX_ENABLE, GX_TEVREG0);
  GX_SetTevKAlphaSel(GX_TEVSTAGE0,GX_TEV_KASEL_K0_A);
  GX_SetTevAlphaIn (GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_RASA, GX_CA_KONST, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG0);
  //Stage 1: TEVREG1 <- { 0, 2Up, 2Um };
  GX_SetTevKColorSel(GX_TEVSTAGE1,GX_TEV_KCSEL_K1);
  GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1,GX_COLOR0A0);
  GX_SetTevColorIn (GX_TEVSTAGE1, GX_CC_KONST, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
  GX_SetTevColorOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_2, GX_ENABLE, GX_TEVREG1);
  GX_SetTevAlphaIn (GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 2: TEVREG2 <- { Vp, Vm, 0 }
  GX_SetTevKColorSel(GX_TEVSTAGE2,GX_TEV_KCSEL_K0);
  GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD1, GX_TEXMAP2,GX_COLOR0A0);
  GX_SetTevColorIn (GX_TEVSTAGE2, GX_CC_RASC, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
  GX_SetTevColorOp (GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG2);
  GX_SetTevAlphaIn (GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 3: TEVPREV <- { (Vm), (Vp), 0 }
  GX_SetTevKColorSel(GX_TEVSTAGE3,GX_TEV_KCSEL_K0);
  GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD1, GX_TEXMAP2,GX_COLOR0A0);
  GX_SetTevColorIn (GX_TEVSTAGE3, GX_CC_KONST, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
  GX_SetTevColorOp (GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_SUBHALF, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 4: TEVPREV <- { (-1.598Vm), (-0.813Vp), 0 }; TEVPREVA <- {Y' - 16*1.164}
  GX_SetTevKColorSel(GX_TEVSTAGE4,GX_TEV_KCSEL_K2);
  GX_SetTevOrder(GX_TEVSTAGE4, GX_TEXCOORD0, GX_TEXMAP0,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE4, GX_CC_ZERO, GX_CC_KONST, GX_CC_CPREV, GX_CC_ZERO);
  GX_SetTevColorOp (GX_TEVSTAGE4, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_2, GX_DISABLE, GX_TEVPREV);
  GX_SetTevKAlphaSel(GX_TEVSTAGE4,GX_TEV_KASEL_1);
  GX_SetTevAlphaIn (GX_TEVSTAGE4, GX_CA_ZERO, GX_CA_KONST, GX_CA_A0, GX_CA_TEXA);
  GX_SetTevAlphaOp (GX_TEVSTAGE4, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  //Stage 5: TEVPREV <- { -1.598Vm (+1.139/2Vp), -0.813Vp +0.813/2Vm), 0 }; TEVREG1A <- {Y' -16*1.164 - Y'*0.164} = {(Y'-16)*1.164}
  GX_SetTevKColorSel(GX_TEVSTAGE5,GX_TEV_KCSEL_K2);
  GX_SetTevOrder(GX_TEVSTAGE5, GX_TEXCOORD0, GX_TEXMAP0,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE5, GX_CC_ZERO, GX_CC_KONST, GX_CC_C2, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevKAlphaSel(GX_TEVSTAGE5,GX_TEV_KASEL_K1_A);
  GX_SetTevAlphaIn (GX_TEVSTAGE5, GX_CA_ZERO, GX_CA_KONST, GX_CA_TEXA, GX_CA_APREV);
  GX_SetTevAlphaOp (GX_TEVSTAGE5, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVREG1);
  //Stage 6: TEVPREV <- {	-1.598Vm (+1.598Vp), -0.813Vp (+0.813Vm), 0 } = {	(+1.598V), (-0.813V), 0 }
  GX_SetTevKColorSel(GX_TEVSTAGE6,GX_TEV_KCSEL_K2);
  GX_SetTevOrder(GX_TEVSTAGE6, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE6, GX_CC_ZERO, GX_CC_KONST, GX_CC_C2, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE6, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE6, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 7: TEVPREV <- {	((Y'-16)*1.164) +1.598V, ((Y'-16)*1.164) -0.813V, ((Y'-16)*1.164) }
  GX_SetTevKColorSel(GX_TEVSTAGE7,GX_TEV_KCSEL_1);
  GX_SetTevOrder(GX_TEVSTAGE7, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE7, GX_CC_ZERO, GX_CC_ONE, GX_CC_A1, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE7, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE7, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 8: TEVPREV <- {	(Y'-16)*1.164 +1.598V, (Y'-16)*1.164 -0.813V (-.394/2Up), (Y'-16)*1.164 (-2.032/2Um)}
  GX_SetTevKColorSel(GX_TEVSTAGE8,GX_TEV_KCSEL_K3);
  GX_SetTevOrder(GX_TEVSTAGE8, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE8, GX_CC_ZERO, GX_CC_KONST, GX_CC_C1, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE8, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE8, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE8, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 9: TEVPREV <- { (Y'-16)*1.164 +1.598V, (Y'-16)*1.164 -0.813V (-.394Up), (Y'-16)*1.164 (-2.032Um)}
  GX_SetTevKColorSel(GX_TEVSTAGE9,GX_TEV_KCSEL_K3);
  GX_SetTevOrder(GX_TEVSTAGE9, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE9, GX_CC_ZERO, GX_CC_KONST, GX_CC_C1, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE9, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE9, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE9, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 10: TEVPREV <- { (Y'-16)*1.164 +1.598V, (Y'-16)*1.164 -0.813V -.394Up (+.394/2Um), (Y'-16)*1.164 -2.032Um (+2.032/2Up)}
  GX_SetTevKColorSel(GX_TEVSTAGE10,GX_TEV_KCSEL_K3);
  GX_SetTevOrder(GX_TEVSTAGE10, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE10, GX_CC_ZERO, GX_CC_KONST, GX_CC_C0, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_DISABLE, GX_TEVPREV);
  GX_SetTevAlphaIn (GX_TEVSTAGE10, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
  GX_SetTevAlphaOp (GX_TEVSTAGE10, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  //Stage 11: TEVPREV <- { (Y'-16)*1.164 +1.598V, (Y'-16)*1.164 -0.813V -.394Up (+.394Um), (Y'-16)*1.164 -2.032Um (+2.032Up)} = { (Y'-16)*1.164 +1.139V, (Y'-16)*1.164 -0.58V -.394U, (Y'-16)*1.164 +2.032U}
  GX_SetTevKColorSel(GX_TEVSTAGE11,GX_TEV_KCSEL_K3);
  GX_SetTevOrder(GX_TEVSTAGE11, GX_TEXCOORDNULL, GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn (GX_TEVSTAGE11, GX_CC_ZERO, GX_CC_KONST, GX_CC_C0, GX_CC_CPREV);
  GX_SetTevColorOp (GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);
  GX_SetTevKAlphaSel(GX_TEVSTAGE11,GX_TEV_KASEL_1);
  GX_SetTevAlphaIn (GX_TEVSTAGE11, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
  GX_SetTevAlphaOp (GX_TEVSTAGE11, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_ENABLE, GX_TEVPREV);


  GX_LoadTexObj(&gvf->gvf_obj[0], GX_TEXMAP0);
  GX_LoadTexObj(&gvf->gvf_obj[1], GX_TEXMAP1);
  GX_LoadTexObj(&gvf->gvf_obj[2], GX_TEXMAP2);


  GX_Begin(GX_QUADS, GX_VTXFMT0, 4);

  GX_Position3f32(-1.0, -1.0, 0.0);
  GX_Color4u8(0, 255, 0, 255);
  GX_TexCoord2f32(0, 1);
  GX_TexCoord2f32(0, 1);

  GX_Position3f32( 1.0, -1.0, 0.0);
  GX_Color4u8(0, 255, 0, 255);
  GX_TexCoord2f32(1, 1);
  GX_TexCoord2f32(1, 1);

  GX_Position3f32( 1.0,  1.0, 0.0);
  GX_Color4u8(0, 255, 0, 255);
  GX_TexCoord2f32(1, 0);
  GX_TexCoord2f32(1, 0);

  GX_Position3f32(-1.0,  1.0, 0.0);
  GX_Color4u8(0, 255, 0, 255);
  GX_TexCoord2f32(0, 0);
  GX_TexCoord2f32(0, 0);

  GX_End();

  GX_SetNumTexGens(1);

 // setup the vertex descriptor
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
}



/**
 *
 */
static void 
render_video(glw_t *w, video_decoder_t *vd, glw_video_t *gv, glw_rctx_t *rc)
{
  video_decoder_frame_t *fra;
  int width = 0, height = 0;
#if ENABLE_DVD
  dvdspu_decoder_t *dd;
  dvdspu_t *d;
#endif

  /*
   * rescale
   */
 
#if 0 
  if(gv->gv_zoom != 100)
    glScalef(gv->gv_zoom / 100.0f, gv->gv_zoom / 100.0f, 1.0f);
#endif

  fra = gv->gv_fra;

  glw_rescale(rc, vd->vd_aspect);

  if(fra != NULL && glw_is_focusable(w))
    glw_store_matrix(w, rc);

#if 0
  if(rc->rc_alpha > 0.98f) 
    glDisable(GL_BLEND); 
  else
    glEnable(GL_BLEND); 
  #endif

  if(fra != NULL) {

    width = fra->vdf_width[0];
    height = fra->vdf_height[0];

    render_video_1f(gv, vd, fra, rc);
  }

  gv->gv_width  = width;
  gv->gv_height = height;

  //  glEnable(GL_BLEND); 

#if ENABLE_DVD
  dd = vd->vd_dvdspu;
  if(gv->gv_sputex != 0 && dd != NULL && width > 0 &&
     (glw_is_focused(w) || !dd->dd_pci.hli.hl_gi.hli_ss)) {
    d = TAILQ_FIRST(&dd->dd_queue);

    if(d != NULL) {
      glBindTexture(textype, gv->gv_sputex);

      glPushMatrix();

      glScalef(2.0f / width, -2.0f / height, 0.0f);
      glTranslatef(-width / 2, -height / 2, 0.0f);
      
      glColor4f(1.0, 1.0, 1.0, rc->rc_alpha);
      
      glBegin(GL_QUADS);
      
      glTexCoord2f(0.0, 0.0);
      glVertex3f(d->d_x1, d->d_y1, 0.0f);
    
      glTexCoord2f(gv->gv_sputex_width, 0.0);
      glVertex3f(d->d_x2, d->d_y1, 0.0f);
    
      glTexCoord2f(gv->gv_sputex_width, gv->gv_sputex_height);
      glVertex3f(d->d_x2, d->d_y2, 0.0f);
    
      glTexCoord2f(0.0, gv->gv_sputex_height);
      glVertex3f(d->d_x1, d->d_y2, 0.0f);

      glEnd();

      glPopMatrix();
    }
  }
#endif
}

/**
 * 
 */
static void
vdf_purge(video_decoder_t *vd, video_decoder_frame_t *vdf)
{
  gx_video_frame_t *gvf = (gx_video_frame_t *)vdf;
  int i;

  for(i = 0; i < 3; i++)
    free(gvf->gvf_mem[i]);

  free(vdf);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
}



static void
gv_purge_queues(video_decoder_t *vd)
{
  video_decoder_frame_t *vdf;

  while((vdf = TAILQ_FIRST(&vd->vd_avail_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_avail_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloced_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_displaying_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_display_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_display_queue, vdf, vdf_link);
    vdf_purge(vd, vdf);
  }
}


/**
 *
 */
static int
gl_video_widget_event(glw_video_t *gv, event_t *e)
{
  if(event_is_action(e, ACTION_PLAYPAUSE) ||
     event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PAUSE) ||
     event_is_action(e, ACTION_ENTER)) {
    mp_enqueue_event(gv->gv_mp, e);
    return 1;
  }

#if 0
  if(event_is_action(e, ACTION_UP) ||
     event_is_action(e, ACTION_DOWN) ||
     event_is_action(e, ACTION_LEFT) ||
     event_is_action(e, ACTION_RIGHT)) {
    
    if(gv->gv_in_menu) {
      mp_enqueue_event(gv->gv_mp, e);
      return 1;
    }
  }
#endif

  return 0;
}



#if ENABLE_DVD
/**
 *
 */
static int
pointer_event(glw_video_t *gv, glw_pointer_event_t *gpe)
{
  dvdspu_decoder_t *dd = gv->gv_vd->vd_dvdspu;
  pci_t *pci;
  int x, y;
  int32_t button, best, dist, d, mx, my, dx, dy;
  event_t *e;

  if(dd == NULL)
    return 0;

  pci = &dd->dd_pci;

  if(!pci->hli.hl_gi.hli_ss)
    return 1;
  
  x = (0.5 +  0.5 * gpe->x) * (float)gv->gv_width;
  y = (0.5 + -0.5 * gpe->y) * (float)gv->gv_height;

  best = 0;
  dist = 0x08000000; /* >> than  (720*720)+(567*567); */
  
  /* Loop through all buttons */
  for(button = 1; button <= pci->hli.hl_gi.btn_ns; button++) {
    btni_t *button_ptr = &(pci->hli.btnit[button-1]);

    if((x >= button_ptr->x_start) && (x <= button_ptr->x_end) &&
       (y >= button_ptr->y_start) && (y <= button_ptr->y_end)) {
      mx = (button_ptr->x_start + button_ptr->x_end)/2;
      my = (button_ptr->y_start + button_ptr->y_end)/2;
      dx = mx - x;
      dy = my - y;
      d = (dx*dx) + (dy*dy);
      /* If the mouse is within the button and the mouse is closer
       * to the center of this button then it is the best choice. */
      if(d < dist) {
        dist = d;
        best = button;
      }
    }
  }

  if(best == 0)
    return 1;

  switch(gpe->type) {
  case GLW_POINTER_CLICK:
    e = event_create(EVENT_DVD_ACTIVATE_BUTTON, sizeof(event_t) + 1);
    break;

  case GLW_POINTER_MOTION:
    if(dd->dd_curbut == best)
      return 1;

    e = event_create(EVENT_DVD_SELECT_BUTTON, sizeof(event_t) + 1);
    break;

  default:
    return 1;
  }

  e->e_payload[0] = best;
  mp_enqueue_event(gv->gv_mp, e);
  event_unref(e);
  return 1;
}
#endif

/**
 *
 */
static void
gv_update_focusable(video_decoder_t *vd, glw_video_t *gv)
{
  int want_focus = 0;
#if ENABLE_DVD
  dvdspu_decoder_t *dd = gv->gv_vd->vd_dvdspu;

  if(dd != NULL) {
    pci_t *pci = &dd->dd_pci;

    if(pci->hli.hl_gi.hli_ss)
      want_focus = 1;
  }
#endif
  
  glw_set_i(&gv->w, 
	    GLW_ATTRIB_FOCUS_WEIGHT, want_focus ? 1.0 : 0.0, 
	    NULL);
}


/**
 *
 */
static int 
gl_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
			 void *extra)
{
  glw_root_t *gr = w->glw_root;
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;
  glw_rctx_t *rc = extra;

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    gv->gv_fullwindow_check = rc->rc_fullwindow;
    return 0;

  case GLW_SIGNAL_DTOR:
    /* We are going away, flush out all frames (PBOs and textures)
       and destroy zombie video decoder */
#if 0
    if(gv->gv_sputex)
      glDeleteTextures(1, &gv->gv_sputex);
#endif
    gv_purge_queues(vd);
    video_decoder_destroy(vd);
    return 0;

  case GLW_SIGNAL_RENDER:
    render_video(w, vd, gv, extra);
    return 0;

  case GLW_SIGNAL_NEW_FRAME:
    
    if(gv->gv_fullwindow_check) {
      /* We are in fullwindow mode */

      if(gv->gv_fullwindow_reported == 0) {
	prop_add_int(gr->gr_fullwindow_req, 1);
	gv->gv_fullwindow_reported = 1;
	TRACE(TRACE_DEBUG, "GLW", "Video display entering full windowed mode");
      }

    } else {
      /* Not in fullscreen mode */
     
      if(gv->gv_fullwindow_reported) {
	prop_add_int(gr->gr_fullwindow_req, -1);
	gv->gv_fullwindow_reported = 0;
	TRACE(TRACE_DEBUG, "GLW", "Video display leaving fullscreen mode");
      }
    }

    gv->gv_fullwindow_check = 0;

    gv_buffer_allocator(vd);
    gv_new_frame(vd, gv, gr);
    gv_update_focusable(vd, gv);
    return 0;

  case GLW_SIGNAL_EVENT:
    return gl_video_widget_event(gv, extra);

  case GLW_SIGNAL_DESTROY:
    if(gv->gv_fullwindow_reported)
      prop_add_int(gr->gr_fullwindow_req, -1);

    video_playback_destroy(gv->gv_vp);
    video_decoder_stop(vd);
    mp_ref_dec(gv->gv_mp);
    gv->gv_mp = NULL;
    return 0;

#if ENABLE_DVD
  case GLW_SIGNAL_POINTER_EVENT:
    return pointer_event(gv, extra);
#endif

  default:
    return 0;
  }
}


/**
 *
 */
static void
glw_video_init(glw_video_t *gv, glw_root_t *gr)
{
  //  gv->gv_dvdspu = gl_dvdspu_init();
 
  //  LIST_INSERT_HEAD(&gr->gr_be.gbr_video_decoders, gv, gv_global_link);

  gv->gv_zoom = 100;
}



/**
 *
 */
void
glw_video_ctor(glw_t *w, int init, va_list ap)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  const char *filename = NULL;
  prop_t *p, *p2;
  event_t *e;

  if(init) {

    gv->gv_mp = mp_create("Video decoder", "video", MP_VIDEO);

    glw_signal_handler_int(w, gl_video_widget_callback);
    glw_video_init(gv, gr);

    glw_set_i(w, 
	      GLW_ATTRIB_SET_FLAGS, GLW_EVERY_FRAME, 
	      NULL);

    gv->gv_vd = video_decoder_create(gv->gv_mp);
    gv->gv_vd->vd_frame_deliver = glw_video_frame_deliver;
    gv->gv_vp = video_playback_create(gv->gv_mp);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_PROPROOTS:
      p = va_arg(ap, void *);

      p2 = prop_create(p, "video");
      
      prop_link(gv->gv_mp->mp_prop_root, p2);
      
      p = va_arg(ap, void *); // Parent, just throw it away
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL && filename[0] != 0) {
    e = event_create_url(EVENT_PLAY_URL, filename);
    mp_enqueue_event(gv->gv_mp, e);
    event_unref(e);
  }
}

extern void videotiler_asm(void *dst, 
			   const void *src0,
			   const void *src1,
			   const void *src2,
			   const void *src3,
			   int h1,
			   int w1,
			   int rp);

/**
 * Frame delivery from video decoder
 */
static void
glw_video_frame_deliver(video_decoder_t *vd, AVCodecContext *ctx,
			AVFrame *frame, int64_t pts, int epoch, int duration,
			int disable_deinterlacer)
{
  video_decoder_frame_t *vdf;
  int hvec[3], wvec[3];
  int hshift, vshift;
  int i;

  vd->vd_active_frames_needed = 2;

  avcodec_get_chroma_sub_sample(ctx->pix_fmt, &hshift, &vshift);

  wvec[0] = ctx->width;
  wvec[1] = ctx->width >> hshift;
  wvec[2] = ctx->width >> hshift;
  hvec[0] = ctx->height;
  hvec[1] = ctx->height >> vshift;
  hvec[2] = ctx->height >> vshift;

  if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
    return;

  static perftimer_t pt;
  perftimer_start(&pt);

  for(i = 0; i < 3; i++)
    videotiler_asm(vdf->vdf_data[i],
		   frame->data[i] + frame->linesize[i] * 0,
		   frame->data[i] + frame->linesize[i] * 1,
		   frame->data[i] + frame->linesize[i] * 2,
		   frame->data[i] + frame->linesize[i] * 3,
		   vdf->vdf_height[i] / 4,
		   vdf->vdf_width[i]  / 8,
		   4 * frame->linesize[i] - vdf->vdf_width[i]);

  vd->vd_interlaced = 0;
  vdf->vdf_pts = pts;
  vdf->vdf_epoch = epoch;
  vdf->vdf_duration = duration;
  TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
  perftimer_stop(&pt, "framexfer");
}
