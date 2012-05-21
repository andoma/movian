/*
 *  Video output on GL surfaces
 *  Copyright (C) 2007-2010 Andreas Öman
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
#include "glw_video_common.h"

#include "rsx/nv40.h"
#include "rsx/reality.h"

#include "video/video_decoder.h"
#include "video/video_playback.h"
#include "video/ps3_vdec.h"


/**
 * gv_surface_mutex must be held
 */
static void
surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  int i;
  
  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++) 
    if(gv->gv_surfaces[i].gvs_offset == gvs->gvs_offset &&
       gvs != &gv->gv_surfaces[i])
      return;  // Memory is shared

  if(gvs->gvs_offset) {
    rsx_free(gvs->gvs_offset, gvs->gvs_size);
    gvs->gvs_offset = 0;
    gvs->gvs_size = 0;
  }
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



static void
init_tex(realityTexture *tex, uint32_t offset,
	 uint32_t width, uint32_t height, uint32_t stride,
	 uint32_t fmt, int repeat, int swizzle)
{
  tex->swizzle = swizzle;
  tex->offset = offset;

  tex->format = fmt |
    NV40_3D_TEX_FORMAT_LINEAR  | 
    NV30_3D_TEX_FORMAT_DIMS_2D |
    NV30_3D_TEX_FORMAT_DMA0 |
    NV30_3D_TEX_FORMAT_NO_BORDER | (0x8000) |
    (1 << NV40_3D_TEX_FORMAT_MIPMAP_COUNT__SHIFT);

  if(repeat) {
    tex->wrap =
      NV30_3D_TEX_WRAP_S_REPEAT |
      NV30_3D_TEX_WRAP_T_REPEAT |
      NV30_3D_TEX_WRAP_R_REPEAT;
  } else {
    tex->wrap =
      NV30_3D_TEX_WRAP_S_CLAMP_TO_EDGE | 
      NV30_3D_TEX_WRAP_T_CLAMP_TO_EDGE | 
      NV30_3D_TEX_WRAP_R_CLAMP_TO_EDGE;
  }

  tex->enable = NV40_3D_TEX_ENABLE_ENABLE;

  tex->filter =
    NV30_3D_TEX_FILTER_MIN_LINEAR |
    NV30_3D_TEX_FILTER_MAG_LINEAR | 0x3fd6;

  tex->width  = width;
  tex->height = height;
  tex->stride = stride;
}

#define ROUND_UP(p, round) (((p) + (round) - 1) & ~((round) - 1))


/**
 *
 */
static void
surface_init(glw_video_t *gv, glw_video_surface_t *gvs,
	     const glw_video_config_t *gvc)
{
  int i;
  int siz[3];

  for(i = 0; i < 3; i++)
    siz[i] = ROUND_UP(gvc->gvc_width[i] * gvc->gvc_height[i], 16);

  gvs->gvs_size = siz[0] + siz[1] + siz[2];
  gvs->gvs_offset = rsx_alloc(gvs->gvs_size, 16);

  gvs->gvs_data[0] = rsx_to_ppu(gvs->gvs_offset);
  gvs->gvs_data[1] = rsx_to_ppu(gvs->gvs_offset + siz[0]);
  gvs->gvs_data[2] = rsx_to_ppu(gvs->gvs_offset + siz[0] + siz[1]);

  int offset = gvs->gvs_offset;
  for(i = 0; i < 3; i++) {

    init_tex(&gvs->gvs_tex[i],
	     offset,
	     gvc->gvc_width[i],
	     gvc->gvc_height[i],
	     gvc->gvc_width[i],
	     NV30_3D_TEX_FORMAT_FORMAT_I8, 0,
	     NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	     NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	     NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	     NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	     );
    offset += siz[i];
  }
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
}


/**
 *
 */
static int
yuvp_init(glw_video_t *gv)
{
  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  int i;

  memset(gv->gv_cmatrix_cur, 0, sizeof(float) * 16);

  for(i = 0; i < gvc->gvc_nsurfaces; i++)
    surface_init(gv, &gv->gv_surfaces[i], gvc);
  return 0;
}





/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  const glw_video_config_t *gvc,
			  struct glw_video_surface_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvs, gvs_link);
  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
}


static const float cmatrix_ITUR_BT_601[16] = {
  1.164400,   1.164400, 1.164400, 0,
  0.000000,  -0.391800, 2.017200, 0,
  1.596000,  -0.813000, 0.000000, 0, 
 -0.874190,   0.531702,-1.085616, 1
};

static const float cmatrix_ITUR_BT_709[16] = {
  1.164400,  1.164400,  1.164400, 0,
  0.000000, -0.213200,  2.112400, 0,
  1.792700, -0.532900,  0.000000, 0,
 -0.972926,  0.301453, -1.133402, 1
};

static const float cmatrix_SMPTE_240M[16] = {
  1.164400,  1.164400,  1.164400, 0,
  0.000000, -0.257800,  2.078700, 0,
  1.793900, -0.542500,  0.000000, 0,
 -0.973528,  0.328659, -1.116486, 1
};



/**
 *
 */
static void
gv_color_matrix_set(glw_video_t *gv, const struct frame_info *fi)
{
  const float *f;

  switch(fi->color_space) {
  case AVCOL_SPC_BT709:
    f = cmatrix_ITUR_BT_709;
    break;

  case AVCOL_SPC_BT470BG:
  case AVCOL_SPC_SMPTE170M:
    f = cmatrix_ITUR_BT_601;
    break;

  case AVCOL_SPC_SMPTE240M:
    f = cmatrix_SMPTE_240M;
    break;

  default:
    f = fi->height < 720 ? cmatrix_ITUR_BT_601 : cmatrix_ITUR_BT_709;
    break;
  }

  memcpy(gv->gv_cmatrix_tgt, f, sizeof(float) * 16);
}


static void
gv_color_matrix_update(glw_video_t *gv)
{
  int i;
  for(i = 0; i < 16; i++)
    gv->gv_cmatrix_cur[i] = (gv->gv_cmatrix_cur[i] * 15.0 +
			     gv->gv_cmatrix_tgt[i]) / 16.0;
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
yuvp_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa, *sb, *s;
  media_pipe_t *mp = gv->gv_mp;
  int output_duration;
  int64_t pts = AV_NOPTS_VALUE;
  int frame_duration = gv->w.glw_root->gr_frameduration;
  int epoch = 0;

  gv_color_matrix_update(gv);
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

  } else {
      
    /* There are frames available that we are going to display,
       push back old frames to decoder */
    while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL)
      gv_surface_pixmap_release(gv, s, &gv->gv_cfg_cur, 
				&gv->gv_displaying_queue);

    /* */
    sb = TAILQ_NEXT(sa, gvs_link);
    if(!vd->vd_hold)
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
 *  Video widget render
 */
static void
render_video_quad(int interlace, int width, int height,
		  int bob1, int bob2,
		  glw_root_t *root, rsx_fp_t *rfp,
		  const glw_video_t *gv, glw_rctx_t *rc)
{
  glw_backend_root_t *be = &root->gr_be;
  gcmContextData *ctx = be->be_ctx;
  rsx_vp_t *rvp = be->be_vp_yuv2rgb;
  float x1, x2, y1, y2;
  float b1 = 0, b2 = 0;
  const int bordersize = 3;
  float rgba[4];
  float tc[12];

  if(interlace) {

    x1 = 0 + (bordersize / (float)width);
    y1 = 0 + (bordersize / (float)height);
    x2 = 1 - (bordersize / (float)width);
    y2 = 1 - (bordersize / (float)height);

    b1 = (0.5 * bob1) / (float)height;
    b2 = (0.5 * bob2) / (float)height;

  } else {

    x1 = 0;
    y1 = 0;
    x2 = 1;
    y2 = 1;
  }

  tc[0] = x1;
  tc[1] = y2 - b1;
  tc[2] = y2 - b2;

  tc[3] = x2;
  tc[4] = y2 - b1;
  tc[5] = y2 - b2;

  tc[6] = x2;
  tc[7] = y1 - b1;
  tc[8] = y1 - b2;

  tc[9] = x1;
  tc[10] = y1 - b1;
  tc[11] = y1 - b2;

  rsx_set_vp(root, rvp);

  realitySetVertexProgramConstant4fBlock(ctx, rvp->rvp_binary,
					 rvp->rvp_u_modelview,
					 4, rc->rc_mtx);


  if(rfp->rfp_u_color != -1) {
    rgba[0] = 0;
    rgba[1] = 0;
    rgba[2] = 0;
    rgba[3] = rc->rc_alpha;

    realitySetFragmentProgramParameter(ctx, rfp->rfp_binary,
				       rfp->rfp_u_color, rgba, 
				       rfp->rfp_rsx_location);
  }

  if(rfp->rfp_u_blend != -1) {
    rgba[0] = gv->gv_blend;
    rgba[1] = 0;
    rgba[2] = 0;
    rgba[3] = 0;
    realitySetFragmentProgramParameter(ctx, rfp->rfp_binary,
				       rfp->rfp_u_blend, rgba,
				       rfp->rfp_rsx_location);
  }


  if(rfp->rfp_u_color_matrix != -1) 
    realitySetFragmentProgramParameter(ctx, rfp->rfp_binary,
				       rfp->rfp_u_color_matrix,
				       gv->gv_cmatrix_cur,
				       rfp->rfp_rsx_location);

  rsx_set_fp(root, rfp, 1);

  realityVertexBegin(ctx, REALITY_QUADS);

  realityAttr4f(ctx, rvp->rvp_a_texcoord, tc[0], tc[1], tc[2], 0);
  realityVertex4f(ctx, -1, -1, 0, 1);

  realityAttr4f(ctx, rvp->rvp_a_texcoord, tc[3], tc[4], tc[5], 0);
  realityVertex4f(ctx,  1, -1, 0, 1);

  realityAttr4f(ctx, rvp->rvp_a_texcoord, tc[6], tc[7], tc[8], 0);
  realityVertex4f(ctx,  1,  1, 0, 1);

  realityAttr4f(ctx, rvp->rvp_a_texcoord, tc[9], tc[10], tc[11], 0);
  realityVertex4f(ctx, -1,  1, 0, 1);

  realityVertexEnd(ctx);
}


/**
 *
 */
static void
render_video_1f(const glw_video_t *gv, glw_video_surface_t *s,
		glw_rctx_t *rc)
{
  glw_backend_root_t *be = &gv->w.glw_root->gr_be;
  gcmContextData *ctx = be->be_ctx;
  rsx_fp_t *rfp = be->be_fp_yuv2rgb_1f;
  int i;

  for(i = 0; i < 3; i++)
    if(rfp->rfp_texunit[i] != -1)
      realitySetTexture(ctx, rfp->rfp_texunit[i], &s->gvs_tex[i]);

  const glw_video_config_t *gvc = &gv->gv_cfg_cur;

  
  render_video_quad(!!(gvc->gvc_flags & GVC_CUTBORDER), 
		    gvc->gvc_width[0], gvc->gvc_height[0],
		    s->gvs_yshift, 0, gv->w.glw_root, rfp, gv, rc);
}


/**
 *
 */
static void
render_video_2f(const glw_video_t *gv, 
		glw_video_surface_t *sa, glw_video_surface_t *sb,
		glw_rctx_t *rc)
{
  glw_backend_root_t *be = &gv->w.glw_root->gr_be;
  gcmContextData *ctx = be->be_ctx;
  rsx_fp_t *rfp = be->be_fp_yuv2rgb_2f;
  int i;

  for(i = 0; i < 3; i++)
    if(rfp->rfp_texunit[i] != -1)
      realitySetTexture(ctx, rfp->rfp_texunit[i], &sa->gvs_tex[i]);

  for(i = 0; i < 3; i++)
    if(rfp->rfp_texunit[i+3] != -1)
      realitySetTexture(ctx, rfp->rfp_texunit[i+3], &sb->gvs_tex[i]);

  const glw_video_config_t *gvc = &gv->gv_cfg_cur;
  
  render_video_quad(!!(gvc->gvc_flags & GVC_CUTBORDER), 
		    gvc->gvc_width[0], gvc->gvc_height[0],
		    sa->gvs_yshift, sb->gvs_yshift,
		    gv->w.glw_root, rfp, gv, rc);
}


/**
 *
 */
static void
yuvp_render(glw_video_t *gv, glw_rctx_t *rc)
{
  //  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa = gv->gv_sa, *sb = gv->gv_sb;

  if(sa == NULL)
    return;

  if(sb != NULL) {
    render_video_2f(gv, sa, sb, rc);
  } else {
    render_video_1f(gv, sa, rc);
  }
}



/**
 *
 */
static glw_video_engine_t glw_video_opengl = {
  .gve_name = "RSX YUVP fragment shader",
  .gve_newframe = yuvp_newframe,
  .gve_render = yuvp_render,
  .gve_reset = yuvp_reset,
  .gve_init = yuvp_init,
};


/**
 *
 */
void
glw_video_input_yuvp(glw_video_t *gv,
		     uint8_t * const data[], const int pitch[],
		     const frame_info_t *fi)
{
  int hvec[3], wvec[3];
  int i, h, w;
  uint8_t *src;
  uint8_t *dst;
  int tff;
  int hshift, vshift;
  glw_video_surface_t *s;
  const int parity = 0;

  avcodec_get_chroma_sub_sample(fi->pix_fmt, &hshift, &vshift);

  wvec[0] = fi->width;
  wvec[1] = fi->width >> hshift;
  wvec[2] = fi->width >> hshift;
  hvec[0] = fi->height >> fi->interlaced;
  hvec[1] = fi->height >> (vshift + fi->interlaced);
  hvec[2] = fi->height >> (vshift + fi->interlaced);

  if(glw_video_configure(gv, &glw_video_opengl, wvec, hvec, 3,
			 fi->interlaced ? (GVC_YHALF | GVC_CUTBORDER) : 0))
    return;
  
  gv_color_matrix_set(gv, fi);

  if((s = glw_video_get_surface(gv)) == NULL)
    return;

  if(!fi->interlaced) {

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      src = data[i];
      dst = s->gvs_data[i];
 
      while(h--) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i];
      }
    }

    glw_video_put_surface(gv, s, fi->pts, fi->epoch, fi->duration, 0);

  } else {

    int duration = fi->duration >> 1;

    tff = fi->tff ^ parity;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = data[i]; 
      dst = s->gvs_data[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i] * 2;
      }
    }
    
    glw_video_put_surface(gv, s, fi->pts, fi->epoch, duration, !tff);

    if((s = glw_video_get_surface(gv)) == NULL)
      return;

    for(i = 0; i < 3; i++) {
      w = wvec[i];
      h = hvec[i];
      
      src = data[i] + pitch[i];
      dst = s->gvs_data[i];
      
      while(h -= 2 > 0) {
	memcpy(dst, src, w);
	dst += w;
	src += pitch[i] * 2;
      }
    }
    
    glw_video_put_surface(gv, s, fi->pts + duration, fi->epoch, duration, tff);
  }
}


/**
 *
 */
static glw_video_engine_t glw_video_rsxmem = {
  .gve_name = "RSX GPU MEM fragment shader",
  .gve_newframe = yuvp_newframe,
  .gve_render = yuvp_render,
  .gve_reset = yuvp_reset,
  .gve_init = yuvp_init,
};

/**
 *
 */
void
glw_video_input_rsx_mem(glw_video_t *gv, void *frame,
			const frame_info_t *fi)
{
  rsx_video_frame_t *rvf = frame;
  int hvec[3], wvec[3];
  int i;
  int hshift, vshift;
  glw_video_surface_t *gvs;

  avcodec_get_chroma_sub_sample(fi->pix_fmt, &hshift, &vshift);

  wvec[0] = fi->width;
  wvec[1] = fi->width >> hshift;
  wvec[2] = fi->width >> hshift;
  hvec[0] = fi->height >> fi->interlaced;
  hvec[1] = fi->height >> (vshift + fi->interlaced);
  hvec[2] = fi->height >> (vshift + fi->interlaced);


  if(glw_video_configure(gv, &glw_video_rsxmem, wvec, hvec, 3,
			 fi->interlaced ? (GVC_YHALF | GVC_CUTBORDER) : 0))
    return;
  
  gv_color_matrix_set(gv, fi);

  if((gvs = glw_video_get_surface(gv)) == NULL)
    return;

  surface_reset(gv, gvs);

  gvs->gvs_size = rvf->rvf_size;
  gvs->gvs_offset = rvf->rvf_offset;

  int offset = gvs->gvs_offset;

  if(fi->interlaced) {
    // Interlaced

    for(i = 0; i < 3; i++) {
      int w = wvec[i];
      int h = hvec[i];

      init_tex(&gvs->gvs_tex[i],
	       offset + !fi->tff * wvec[i],
	       w, h, w*2,
	       NV30_3D_TEX_FORMAT_FORMAT_I8, 0,
	       NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	       NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	       NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	       NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	       );
      offset += w * (fi->height >> (i ? vshift : 0));
    }
    glw_video_put_surface(gv, gvs, fi->pts, fi->epoch, fi->duration/2, 0);

    if((gvs = glw_video_get_surface(gv)) == NULL)
      return;
  
    surface_reset(gv, gvs);

    gvs->gvs_size = rvf->rvf_size;
    gvs->gvs_offset = rvf->rvf_offset;

    offset = gvs->gvs_offset;

    for(i = 0; i < 3; i++) {
      int w = wvec[i];
      int h = hvec[i];

      init_tex(&gvs->gvs_tex[i],
	       offset + !!fi->tff * wvec[i],
	       w, h, w*2,
	       NV30_3D_TEX_FORMAT_FORMAT_I8, 0,
	       NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	       NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	       NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	       NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	       );
      offset += w * (fi->height >> (i ? vshift : 0));
    }

    glw_video_put_surface(gv, gvs, fi->pts + fi->duration, fi->epoch,
			  fi->duration/2, 0);

  } else {
    // Progressive

    for(i = 0; i < 3; i++) {
      int w = wvec[i];
      int h = hvec[i];

      init_tex(&gvs->gvs_tex[i],
	       offset,
	       w,h,w,
	       NV30_3D_TEX_FORMAT_FORMAT_I8, 0,
	       NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	       NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	       NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	       NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	       );
      offset += w * h;
    }
    glw_video_put_surface(gv, gvs, fi->pts, fi->epoch, fi->duration, 0);
  }
}

