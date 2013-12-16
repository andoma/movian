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
 * gv_surface_mutex must be held
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  int i;

  for(i = 0; i < 3; i++)
    free(gvs->gvs_mem[i]);
}


/**
 *
 */
static void
yuvp_reset(glw_video_t *gv)
{
  int i;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(gv, &gv->gv_surfaces[i]);
}



/**
 *
 */
static void
surface_init(glw_video_t *gv, glw_video_surface_t *gvs,
	     const glw_video_config_t *gvc)
{
  int i;

  for(i = 0; i < 3; i++) {

    gvs->gvs_size[i] = gvc->gvc_width[i] * gvc->gvc_height[i];
    gvs->gvs_mem[i]  = memalign(32, gvs->gvs_size[i]);

    TRACE(TRACE_DEBUG, "Wii", "%p[%d]: Alloc %d bytes = %p", 
	  gvs, i, gvs->gvs_size[i], gvs->gvs_mem[i]);

    if(gvs->gvs_mem[i] == NULL)
      abort();
      
    gvs->gvs_data[i] = gvs->gvs_mem[i];

    GX_InitTexObj(&gvs->gvs_obj[i], gvs->gvs_mem[i], 
		  gvc->gvc_width[i], gvc->gvc_height[i],
		  GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  }
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
}


/**
 *
 */
static void
yuvp_init(glw_video_t *gv)
{
  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  int i;

  for(i = 0; i < gvc->gvc_nsurfaces; i++)
    surface_init(gv, &gv->gv_surfaces[i], gvc);
}



/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  const glw_video_config_t *gvc,
			  struct glw_video_surface_queue *fromqueue)
{
  hts_mutex_lock(&gv->gv_surface_mutex);
  TAILQ_REMOVE(fromqueue, gvs, gvs_link);
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
  hts_mutex_unlock(&gv->gv_surface_mutex);
}



/**
 *
 */
static int64_t
gv_compute_blend(glw_video_t *gv, glw_video_surface_t *sa,
		 glw_video_surface_t *sb, int output_duration)
{
  int64_t pts;
  int x;

  if(sa->gvs_duration >= output_duration) {
  
    gv->gv_sa = sa;
    gv->gv_sb = NULL;

    pts = sa->gvs_pts;

    sa->gvs_duration -= output_duration;
    sa->gvs_pts      += output_duration;

  } else if(sb != NULL) {

    gv->gv_sa = sa;
    gv->gv_sb = sb;
    gv->gv_blend = (float) sa->gvs_duration / (float)output_duration;

    if(sa->gvs_duration + 
       sb->gvs_duration < output_duration) {

      sa->gvs_duration = 0;
      pts = sb->gvs_pts;

    } else {
      pts = sa->gvs_pts;
      x = output_duration - sa->gvs_duration;
      sb->gvs_duration -= x;
      sb->gvs_pts      += x;
    }
    sa->gvs_duration = 0;

  } else {
    gv->gv_sa = sa;
    gv->gv_sb = NULL;
    sa->gvs_pts      += output_duration;

    pts = sa->gvs_pts;
  }

  return pts;
}


/**
 *
 */
static int64_t
yuvp_newframe(glw_video_t *gv, video_decoder_t *vd)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa, *sb, *s;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = 0;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  output_duration = glw_video_compute_output_duration(vd, frame_duration);

  
  /* Find new surface to display */
  sa = TAILQ_FIRST(&gv->gv_decoded_queue);
  if(sa == NULL) {
    /* No frame available */
    sa = TAILQ_FIRST(&gv->gv_displaying_queue);
    if(sa != NULL) {
      /* Continue to display last frame */
      gv->gv_sa = sa;
      gv->gv_sa = NULL;
    } else {
      gv->gv_sa = NULL;
      gv->gv_sa = NULL;
    }

    pts = AV_NOPTS_VALUE;
      
  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
    while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL)
      gv_surface_pixmap_release(gv, s, &gv->gv_cfg_cur, 
				&gv->gv_displaying_queue);

    /* */
    sb = TAILQ_NEXT(sa, gvs_link);
    pts = gv_compute_blend(gv, sa, sb, output_duration);
    epoch = sa->gvs_epoch;

    if(!vd->vd_hold || sb != NULL) {
      if(sa != NULL && sa->gvs_duration == 0)
	glw_video_enqueue_for_display(gv, sa, &gv->gv_decoded_queue);
    }
    if(sb != NULL && sb->gvs_duration == 0)
      glw_video_enqueue_for_display(gv, sb, &gv->gv_decoded_queue);
  }

  if(pts != AV_NOPTS_VALUE) {
    pts -= frame_duration * 2;
    glw_video_compute_avdiff(gr, vd, mp, pts, epoch);
  }
  return pts; 
}




/**
 * Render
 */
static void
render_video_1f(glw_video_t *gv, glw_video_surface_t *sa, glw_rctx_t *rc)
{
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


  GX_LoadTexObj(&sa->gvs_obj[0], GX_TEXMAP0);
  GX_LoadTexObj(&sa->gvs_obj[1], GX_TEXMAP1);
  GX_LoadTexObj(&sa->gvs_obj[2], GX_TEXMAP2);


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
static void 
yuvp_render(glw_video_t *gv, glw_rctx_t *rc) 
{ 
  glw_video_surface_t *sa = gv->gv_sa;

  if(sa != NULL)
    render_video_1f(gv, sa, rc);
}

#if 0
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
#endif



/**
 *
 */
static glw_video_engine_t glw_video_gx = {
  .gve_name = "GX YUVP renderer",
  .gve_newframe = yuvp_newframe,
  .gve_render = yuvp_render,
  .gve_reset = yuvp_reset,
  .gve_init = yuvp_init,
};




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
glw_video_input_yuvp(glw_video_t *gv,
		     uint8_t * const data[], const int pitch[],
		     int width, int height, int pix_fmt,
		     int64_t pts, int epoch, int duration, int flags)
{
  int hvec[3], wvec[3];
  int i;
  int hshift, vshift;
  glw_video_surface_t *s;
  const int ilace = 0; // !!(flags & VD_INTERLACED);

  avcodec_get_chroma_sub_sample(pix_fmt, &hshift, &vshift);

  wvec[0] = width;
  wvec[1] = width >> hshift;
  wvec[2] = width >> hshift;
  hvec[0] = height >> ilace;
  hvec[1] = height >> (vshift + ilace);
  hvec[2] = height >> (vshift + ilace);

  if(glw_video_configure(gv, &glw_video_gx, wvec, hvec, 3,
			 ilace ? GVC_CUTBORDER : 0))
    return;
  
  if((s = glw_video_get_surface(gv)) == NULL)
    return;


  static perftimer_t pt;
  perftimer_start(&pt);

  for(i = 0; i < 3; i++)
    videotiler_asm(s->gvs_data[i],
		   data[i] + pitch[i] * 0,
		   data[i] + pitch[i] * 1,
		   data[i] + pitch[i] * 2,
		   data[i] + pitch[i] * 3,
		   wvec[i] / 4,
		   hvec[i]  / 8,
		   4 * pitch[i] - hvec[i]);

  glw_video_put_surface(gv, s, pts, epoch, duration, 0);
  perftimer_stop(&pt, "framexfer");
}
