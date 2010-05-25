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
#include "glw.h"
#include "misc/perftimer.h"

#include "glw_video_common.h"

/**
 *
 */
typedef struct gx_video_frame {
  video_decoder_frame_t gvf_vdf;

  GXTexObj gvf_obj[3];
  void *gvf_mem[3];
  int gvf_size[3];

} gx_video_frame_t;


/**
 *  Buffer allocator
 */
void
glw_video_buffer_allocator(video_decoder_t *vd)
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


/**
 *
 */
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



void 
glw_video_new_frame(video_decoder_t *vd, glw_video_t *gv, glw_root_t *gr)
{
  video_decoder_frame_t *fra, *frb;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  struct video_decoder_frame_queue *dq;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  output_duration = glw_video_compute_output_duration(vd, frame_duration);

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
	glw_video_enqueue_for_display(vd, fra, dq);
    }
    if(frb != NULL && frb->vdf_duration == 0)
      glw_video_enqueue_for_display(vd, frb, dq);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    glw_video_compute_avdiff(gr, vd, mp, pts, epoch);

#if ENABLE_DVD
    glw_video_spu_layout(vd, &gv->gv_spu, gr, pts);
#endif
    glw_video_sub_layout(vd, &gv->gv_sub, gr, pts, (glw_t *)gv);
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


  if(vdf->vdf_width[0] == 0)
    return;
  
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

  // setup the vertex descriptor
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

  GX_SetNumTevStages(1);
  GX_SetNumChans(1);

 GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

  GX_SetNumTexGens(1);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);


}



/**
 *
 */
void 
glw_video_render(glw_t *w, glw_rctx_t *rc) 
{ 
  glw_video_t *gv = (glw_video_t *)w; 
  video_decoder_t *vd = gv->gv_vd; 
  video_decoder_frame_t *fra = gv->gv_fra;

  glw_scale_to_aspect(rc, vd->vd_aspect);

  if(fra != NULL && glw_is_focusable(w))
    glw_store_matrix(w, rc);

  if(fra != NULL) {
    gv->gv_width  = fra->vdf_width[0];
    gv->gv_height = fra->vdf_height[0];
    render_video_1f(gv, vd, fra, rc);
  } else {
    gv->gv_width = gv->gv_height = 0;
  }

  glw_rctx_t rc0 = *rc;
  glw_PushMatrix(&rc0, rc);
  glw_Scalef(&rc0, 2.0f / gv->gv_width, -2.0f / gv->gv_height, 1.0f);
  glw_Translatef(&rc0, -gv->gv_width / 2.0, -gv->gv_height / 2.0, 0.0f);

  if(gv->gv_width > 0 && (glw_is_focused(w) || !vd->vd_pci.hli.hl_gi.hli_ss))
    gvo_render(&gv->gv_spu, w->glw_root, &rc0);

  gvo_render(&gv->gv_sub, w->glw_root, &rc0);

  glw_PopMatrix();

  if(gv->gv_sub.gvo_child != NULL)
    glw_render0(gv->gv_sub.gvo_child, rc);
}

/**
 * 
 */
void
glw_video_framepurge(video_decoder_t *vd, video_decoder_frame_t *vdf)
{
  gx_video_frame_t *gvf = (gx_video_frame_t *)vdf;
  int i;

  for(i = 0; i < 3; i++)
    free(gvf->gvf_mem[i]);

  free(vdf);
  assert(vd->vd_active_frames > 0);
  vd->vd_active_frames--;
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
void
glw_video_frame_deliver(struct video_decoder *vd,
			uint8_t * const data[],
			const int linesize[],
			int width,
			int height,
			int pix_fmt,
			int64_t pts,
			int epoch,
			int duration,
			int flags)
{
  video_decoder_frame_t *vdf;
  int hvec[3], wvec[3];
  int hshift, vshift;
  int i;

  if(data == NULL) {
    // Blackout
    hvec[0] = wvec[0] = 0;
    hvec[1] = wvec[1] = 0;
    hvec[2] = wvec[2] = 0;

    if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
      return;

    vdf->vdf_pts = AV_NOPTS_VALUE;
    vdf->vdf_epoch = epoch;
    vdf->vdf_duration = 1;
    vdf->vdf_cutborder = 0;
    TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
    return;
  }

  vd->vd_active_frames_needed = 2;

  avcodec_get_chroma_sub_sample(pix_fmt, &hshift, &vshift);

  wvec[0] = width;
  wvec[1] = width >> hshift;
  wvec[2] = width >> hshift;
  hvec[0] = height;
  hvec[1] = height >> vshift;
  hvec[2] = height >> vshift;

  if((vdf = vd_dequeue_for_decode(vd, wvec, hvec)) == NULL)
    return;

  static perftimer_t pt;
  perftimer_start(&pt);

  for(i = 0; i < 3; i++)
    videotiler_asm(vdf->vdf_data[i],
		   data[i] + linesize[i] * 0,
		   data[i] + linesize[i] * 1,
		   data[i] + linesize[i] * 2,
		   data[i] + linesize[i] * 3,
		   vdf->vdf_height[i] / 4,
		   vdf->vdf_width[i]  / 8,
		   4 * linesize[i] - vdf->vdf_width[i]);

  vdf->vdf_pts = pts;
  vdf->vdf_epoch = epoch;
  vdf->vdf_duration = duration;
  TAILQ_INSERT_TAIL(&vd->vd_display_queue, vdf, vdf_link);
  perftimer_stop(&pt, "framexfer");
}
