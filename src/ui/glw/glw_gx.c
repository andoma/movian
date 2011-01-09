/*
 *  GL Widgets, common stuff
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

#include <malloc.h>

#include "glw.h"
#include "glw_cursor.h"


/**
 *
 */
void
glw_rctx_init(glw_rctx_t *rc, int width, int height)
{
  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_width  = width;
  rc->rc_height = height;
  rc->rc_alpha = 1.0f;

  guMtxIdentity(rc->rc_mtx);
  guMtxTransApply(rc->rc_mtx, rc->rc_mtx, 0, 0, -1 / tan(45 * M_PI / 360));
}


/**
 *
 */
void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;
  if(rc->rc_inhibit_matrix_store)
    return;

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(float) * 16);

  memcpy(w->glw_matrix, rc->rc_mtx, sizeof(float) * 12);

  if(glw_is_focused(w) && gcp != NULL) {
    gcp->gcp_alpha  = rc->rc_alpha;
    memcpy(gcp->gcp_m, w->glw_matrix, 16 * sizeof(float));
  }
}

/**
 *
 */
int
glw_check_system_features(glw_root_t *gr)
{
  return 0;
}


/**
 *
 */
static const float clip_planes[4][4] = {
  [GLW_CLIP_TOP]    = { 0.0, -1.0, 0.0, 1.0},
  [GLW_CLIP_BOTTOM] = { 0.0,  1.0, 0.0, 1.0},
  [GLW_CLIP_LEFT]   = {-1.0,  0.0, 0.0, 1.0},
  [GLW_CLIP_RIGHT]  = { 1.0,  0.0, 0.0, 1.0},
};


static inline void
mtx_trans_mul_vec4(float *dst, Mtx mt,
		   float x, float y, float z, float w)
{
  dst[0] = mt[0][0] * x + mt[1][0] * y + mt[2][0] * z + 0 * w;
  dst[1] = mt[0][1] * x + mt[1][1] * y + mt[2][1] * z + 0 * w;
  dst[2] = mt[0][2] * x + mt[1][2] * y + mt[2][2] * z + 0 * w;
  dst[3] = mt[0][3] * x + mt[1][3] * y + mt[2][3] * z + 1 * w;
}


/**
 *
 */
int
glw_clip_enable(glw_root_t *gr, glw_rctx_t *rc, glw_clip_boundary_t how)
{
  int i;
  return -1; // for now

  for(i = 0; i < NUM_CLIPPLANES; i++)
    if(!(gr->gr_be.gbr_active_clippers & (1 << i)))
      break;

  if(i == NUM_CLIPPLANES)
    return -1;

  Mtx inv;

  if(!guMtxInverse(rc->rc_mtx, inv))
    return -1;

  mtx_trans_mul_vec4(gr->gr_be.gbr_clip[i], inv, 
		     clip_planes[how][0],
		     clip_planes[how][1],
		     clip_planes[how][2],
		     clip_planes[how][3]);

  gr->gr_be.gbr_active_clippers |= (1 << i);
  return i;
}


/**
 *
 */
void
glw_clip_disable(glw_root_t *gr, glw_rctx_t *rc, int which)
{
  if(which == -1)
    return;

  gr->gr_be.gbr_active_clippers &= ~(1 << which);
}


/**
 * m   Model matrix
 * x   Return x in model space
 * y   Return y in model space
 * p   Mouse pointer at camera z plane
 * dir Mouse pointer direction vector
 */
int
glw_widget_unproject(Mtx mt, float *xp, float *yp, 
		     const float *p, const float *dir)
{
  Mtx inv;
   
  guVector u, v, n, w0, T0, T1, T2, out, I, A, pointer, direction;
  float b;

  A.x = -1;  A.y = -1;  A.z = 0;
  guVecMultiply(mt, &A, &T0);
  A.x =  1;  A.y = -1;  A.z = 0;
  guVecMultiply(mt, &A, &T1);
  A.x =  1;  A.y =  1;  A.z = 0;
  guVecMultiply(mt, &A, &T2);

  guVecSub(&T1, &T0, &u);
  guVecSub(&T2, &T0, &v);

  guVecCross(&u, &v, &n);

  pointer.x = p[0];
  pointer.y = p[1];
  pointer.z = p[2];

  direction.x = dir[0];
  direction.y = dir[1];
  direction.z = dir[2];

  guVecSub(&pointer, &T0, &w0);
  b = guVecDotProduct(&n, &direction);
  if(fabs(b) < 0.000001)
    return 0;

  guVecScale(&direction, &I, -guVecDotProduct(&n, &w0) / b);
  guVecAdd(&I, &pointer, &I);

  if(!guMtxInverse(mt, inv))
    return 0;
  guVecMultiply(inv, &I, &out);
  
  *xp = out.x;
  *yp = out.y;
  return 1;
}

/**
 *
 */
void
glw_rtt_init(struct glw_root *gr, glw_rtt_t *grtt, int width, int height,
	     int alpha)
{
  unsigned int size = ((width + 3) & ~3) * ((height + 3) & ~3) * 4;
  
  GX_InvalidateTexAll();

  grtt->grtt_size = size;
  grtt->grtt_width  = width;
  grtt->grtt_height = height;

  grtt->grtt_texture.mem = memalign(32, size);

  GX_InitTexObj(&grtt->grtt_texture.obj, grtt->grtt_texture.mem,
		width, height, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
}


/**
 *
 */
void
glw_rtt_enter(struct glw_root *gr, glw_rtt_t *grtt, struct glw_rctx *rc)
{
  glw_rctx_init(rc, grtt->grtt_width, grtt->grtt_height);

  GX_SetViewport(0, 0, grtt->grtt_width, grtt->grtt_height, -10, 10);
  GX_SetScissor(0, 0, grtt->grtt_width, grtt->grtt_height);
}


/**
 *
 */
void
glw_rtt_restore(struct glw_root *gr, glw_rtt_t *grtt)
{
  GX_DrawDone();

  GX_SetTexCopySrc(0, 0, grtt->grtt_width, grtt->grtt_height);
  GX_SetTexCopyDst(grtt->grtt_width, grtt->grtt_height, GX_TF_RGBA8, GX_FALSE);

  GX_CopyTex(grtt->grtt_texture.mem, GX_TRUE);
  GX_PixModeSync();  

  DCFlushRange(grtt->grtt_texture.mem, grtt->grtt_size);

  extern GXRModeObj wii_vmode;

  GX_SetViewport(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight, 0, 1);
  GX_SetScissor(0, 0, wii_vmode.fbWidth, wii_vmode.efbHeight);
}


/**
 *
 */
void
glw_rtt_destroy(struct glw_root *gr, glw_rtt_t *grtt)
{
 if(grtt->grtt_texture.mem != NULL) {
    free(grtt->grtt_texture.mem);
    grtt->grtt_texture.mem = NULL;
  }
}

#define glw_rtt_texture(grtt) ((grtt)->grtt_texture)


/**
 * 
 */
void
glw_blendmode(int mode)
{
  switch(mode) {
  case GLW_BLEND_NORMAL:
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, 
		    GX_BL_INVSRCALPHA, GX_LO_CLEAR);
     break;

  case GLW_BLEND_ADDITIVE:
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, 
		    GX_BL_ONE, GX_LO_CLEAR);
    break;
  }
}


/**
 * XXX TODO
 */
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{

}

/**
 * XXX TODO
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{

}



#define float_to_byte(f) GLW_MAX(0, GLW_MIN(255, (int)(f * 255.0)))
