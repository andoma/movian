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

#include <malloc.h>

#include "glw.h"
#include "glw_cursor.h"

void
glw_store_matrix(glw_t *w, glw_rctx_t *rc)
{
  glw_cursor_painter_t *gcp = rc->rc_cursor_painter;
  if(rc->rc_inhibit_matrix_store)
    return;

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
 * m   Model matrix
 * x   Return x in model space
 * y   Return y in model space
 * p   Mouse pointer at camera z plane
 * dir Mouse pointer direction vector
 */
int
glw_widget_unproject(const float *m, float *xp, float *yp, 
		     const float *p, const float *dir)
{
  Mtx mt, inv;
   
  guVector u, v, n, w0, T0, T1, T2, out, I, A, pointer, direction;
  float b;

  memcpy(mt, m, sizeof(float) * 12);

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
glw_wirebox(glw_rctx_t *rc)
{
}


#define float_to_byte(f) GLW_MAX(0, GLW_MIN(255, (int)(f * 255.0)))

/**
 * 
 */
void
glw_render_init(glw_renderer_t *gr, int vertices, int attribs)
{
  gr->gr_stride = 3 + 
    (attribs >= GLW_RENDER_ATTRIBS_TEX       ? 2 : 0) +
    (attribs >= GLW_RENDER_ATTRIBS_TEX_COLOR ? 4 : 0);
    
  gr->gr_buffer = malloc(sizeof(float) * gr->gr_stride * vertices);
  gr->gr_vertices = vertices;
}


/**
 * 
 */
void
glw_render_set_vertices(glw_renderer_t *gr, int vertices)
{
  gr->gr_vertices = vertices;
}


/**
 * 
 */
void
glw_render_free(glw_renderer_t *gr)
{
  free(gr->gr_buffer);
  gr->gr_buffer = NULL;
}


/**
 * 
 */
void
glw_render_vtx_pos(glw_renderer_t *gr, int vertex,
		   float x, float y, float z)
{
  gr->gr_buffer[vertex * gr->gr_stride + 0] = x;
  gr->gr_buffer[vertex * gr->gr_stride + 1] = y;
  gr->gr_buffer[vertex * gr->gr_stride + 2] = z;
}

/**
 * 
 */
void
glw_render_vtx_st(glw_renderer_t *gr, int vertex,
		  float s, float t)
{
  gr->gr_buffer[vertex * gr->gr_stride + 3] = s;
  gr->gr_buffer[vertex * gr->gr_stride + 4] = t;
}

/**
 * 
 */
void
glw_render_vtx_col(glw_renderer_t *gr, int vertex,
		   float r, float g, float b, float a)
{
  gr->gr_buffer[vertex * gr->gr_stride + 5] = float_to_byte(r);
  gr->gr_buffer[vertex * gr->gr_stride + 6] = float_to_byte(g);
  gr->gr_buffer[vertex * gr->gr_stride + 7] = float_to_byte(b);
  gr->gr_buffer[vertex * gr->gr_stride + 8] = float_to_byte(a);
}



/**
 * 
 */
void
glw_render(glw_renderer_t *gr, glw_root_t *root, glw_rctx_t *rc, 
	   int mode, int attribs, glw_backend_texture_t *be_tex,
	   float r, float g, float b, float a)
{
  int i;
  float *buf = gr->gr_buffer;
  int r8, g8, b8, a8;

  GX_LoadPosMtxImm(rc->rc_be.gbr_model_matrix, GX_PNMTX0);
  
  switch(attribs) {
  case GLW_RENDER_ATTRIBS_NONE:
    r8 = float_to_byte(r);
    g8 = float_to_byte(g);
    b8 = float_to_byte(b);
    a8 = float_to_byte(a);

    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);

    GX_Begin(mode, GX_VTXFMT0, gr->gr_vertices);
    
    for(i = 0; i < gr->gr_vertices; i++) {
      GX_Position3f32(buf[0], buf[1], buf[2]);
      GX_Color4u8(r8, g8, b8, a8);
      buf += gr->gr_stride;
    }
    GX_End();

    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    break;


  case GLW_RENDER_ATTRIBS_COLOR:
    a8 = float_to_byte(a);

    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    
    GX_Begin(mode, GX_VTXFMT0, gr->gr_vertices);
 
    for(i = 0; i < gr->gr_vertices; i++) {
      GX_Position3f32(buf[0], buf[1], buf[2]);
      GX_Color4u8(buf[5], buf[6], buf[7], a8);
      buf += gr->gr_stride;
    }
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    break;

  case GLW_RENDER_ATTRIBS_TEX:
    r8 = float_to_byte(r);
    g8 = float_to_byte(g);
    b8 = float_to_byte(b);
    a8 = float_to_byte(a);

    GX_LoadTexObj(&be_tex->obj, GX_TEXMAP0);
    
    GX_Begin(mode, GX_VTXFMT0, gr->gr_vertices);
 
    for(i = 0; i < gr->gr_vertices; i++) {
      GX_Position3f32(buf[0], buf[1], buf[2]);
      GX_Color4u8(r8, g8, b8, a8);
      GX_TexCoord2f32(buf[3], buf[4]);
      buf += gr->gr_stride;
    }
    GX_End();
    break;

  case GLW_RENDER_ATTRIBS_TEX_COLOR:
    GX_LoadTexObj(&be_tex->obj, GX_TEXMAP0);
    
    GX_Begin(mode, GX_VTXFMT0, gr->gr_vertices);
 
    for(i = 0; i < gr->gr_vertices; i++) {
      GX_Position3f32(buf[0], buf[1], buf[2]);
      GX_Color4u8(buf[5], buf[6], buf[7], float_to_byte(buf[8] * a));
      GX_TexCoord2f32(buf[3], buf[4]);
      buf += gr->gr_stride;
    }
    GX_End();
    break;
  }
}
