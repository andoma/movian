/*
 *  GL Widgets, OpenGL rendering
 *  Copyright (C) 2009 Andreas Öman
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

#include "config.h"

#include "glw.h"

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
glw_render_free(glw_renderer_t *gr)
{
  free(gr->gr_buffer);
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
glw_render_vts_col(glw_renderer_t *gr, int vertex,
		   float r, float g, float b, float a)
{
  gr->gr_buffer[vertex * gr->gr_stride + 5] = r;
  gr->gr_buffer[vertex * gr->gr_stride + 6] = g;
  gr->gr_buffer[vertex * gr->gr_stride + 7] = b;
  gr->gr_buffer[vertex * gr->gr_stride + 8] = a;
}



/**
 * 
 */
void
glw_render(glw_renderer_t *gr, glw_rctx_t *rc, int mode, int attribs,
	   glw_backend_texture_t *be_tex,
	   float r, float g, float b, float a)
{
  int i;
  float *buf = gr->gr_buffer;
  int r8, g8, b8, a8;

  GX_LoadPosMtxImm(rc->rc_be.gbr_model_matrix, GX_PNMTX0);
  
  switch(attribs) {
  case GLW_RENDER_ATTRIBS_NONE:
    r8 = r * 255.0; g8 = g * 255.0; b8 = b * 255.0; a8 = a * 255.0;

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

  case GLW_RENDER_ATTRIBS_TEX:
    r8 = r * 255.0; g8 = g * 255.0; b8 = b * 255.0; a8 = a * 255.0;

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
    a8 = a * 255.0;

    GX_LoadTexObj(&be_tex->obj, GX_TEXMAP0);
    
    GX_Begin(mode, GX_VTXFMT0, gr->gr_vertices);
 
    for(i = 0; i < gr->gr_vertices; i++) {
      GX_Position3f32(buf[0], buf[1], buf[2]);
      GX_Color4u8(buf[5] * 255.0, buf[6] * 255.0, buf[7] * 255.0, a8);
      GX_TexCoord2f32(buf[3], buf[4]);
      buf += gr->gr_stride;
    }
    GX_End();
    break;
  }
}
