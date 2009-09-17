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

void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;

  if(w->glw_matrix == NULL)
    w->glw_matrix = malloc(sizeof(float) * 16);

  memcpy(w->glw_matrix, rc->rc_be.gbr_model_matrix, sizeof(float) * 12);

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
int
glw_clip_enable(glw_rctx_t *rc, glw_clip_boundary_t how)
{
  // XXX: TODO
  return 0;
}


/**
 *
 */
void
glw_clip_disable(glw_rctx_t *rc, int which)
{
  // XXX: TODO
}


/**
 * XXX: Replace with something more clever
 */
void
glw_widget_project(float *m, float *x1, float *x2, float *y1, float *y2)
{
  *x1 = m[3] - m[0];
  *x2 = m[3] + m[0];
  *y1 = m[7] - m[5];
  *y2 = m[7] + m[5];
}


/**
 *
 */
void
glw_rtt_init(struct glw_root *gr, glw_rtt_t *grtt, int width, int height)
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

  memset(rc, 0, sizeof(glw_rctx_t));
  rc->rc_alpha = 1;
  rc->rc_size_x = grtt->grtt_width;
  rc->rc_size_y = grtt->grtt_height;

  guMtxIdentity(rc->rc_be.gbr_model_matrix);
  guMtxTransApply(rc->rc_be.gbr_model_matrix,
		  rc->rc_be.gbr_model_matrix,
		  0, 0, -2.4142);

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

  extern GXRModeObj *wii_rmode;
  GXRModeObj *rmode = wii_rmode;

  GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
  GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
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


void
glw_blendmode(int mode)
{
  return;

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
