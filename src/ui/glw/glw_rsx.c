/*
 *  libglw, OpenGL interface
 *  Copyright (C) 2008 Andreas Öman
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

#include <string.h>

#include "rsx/reality.h"

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"



static realityVertexProgram_old nv40_vp = {
  .in_reg  = 0x00000309,
  .out_reg = 0x0000c001,
  .size = (3*4),
  .data = {
    /* MOV result.position, vertex.position */
    0x40041c6c, 0x0040000d, 0x8106c083, 0x6041ff80,
    /* MOV result.texcoord[0], vertex.texcoord[0] */
    0x401f9c6c, 0x0040080d, 0x8106c083, 0x6041ff9c,
    /* MOV result.texcoord[1], vertex.texcoord[1] */
    0x401f9c6c, 0x0040090d, 0x8106c083, 0x6041ffa1,
  }
};

/*******************************************************************************
 * NV30/NV40/G70 fragment shaders
 */

static realityFragmentProgram nv30_fp = {
  .num_regs = 2,
  .size = (2*4),
  .data = {
    /* TEX R0, fragment.texcoord[0], texture[0], 2D */
    0x17009e00, 0x1c9dc801, 0x0001c800, 0x3fe1c800,
    /* MOV R0, R0 */
    0x01401e81, 0x1c9dc800, 0x0001c800, 0x0001c800,
  }
};


static const float identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};
  


const static float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};

/**
 *
 */
int
glw_rsx_init_context(glw_root_t *gr)
{
  // install fragment shader in rsx memory
  u32 *frag_mem = rsxMemAlign(256, 256);
  realityInstallFragmentProgram(gr->gr_be.be_ctx, &nv30_fp, frag_mem);

  return 0;
}



/**
 *
 */
void
glw_wirebox(glw_root_t *gr, glw_rctx_t *rc)
{

}


/**
 *
 */
void
glw_wirecube(glw_root_t *gr, glw_rctx_t *rc)
{

}


/**
 *
 */
void
glw_renderer_draw(glw_renderer_t *gr, glw_root_t *root,
		  glw_rctx_t *rc, glw_backend_texture_t *be_tex,
		  const glw_rgb_t *rgb, float alpha, int flags)
{
  gcmContextData *ctx = root->gr_be.be_ctx;

  realityLoadVertexProgram_old(ctx, &nv40_vp);
  realityLoadFragmentProgram(ctx, &nv30_fp); 

  realitySetTexture(ctx, 0, &be_tex->tex);

  realityVertexBegin(ctx, REALITY_QUADS);

  realityTexCoord2f(ctx, 0.0, 1.0);
  realityVertex4f(ctx, -1, -1, 0.0, 1.0); 
  
  realityTexCoord2f(ctx, 1.0, 1.0);
  realityVertex4f(ctx, 1, -1, 0.0, 1.0); 
  
  realityTexCoord2f(ctx, 1.0, 0.0);
  realityVertex4f(ctx, 1, 1, 0.0, 1.0); 
  
  realityTexCoord2f(ctx, 0.0, 0.0);
  realityVertex4f(ctx, -1, 1, 0.0, 1.0); 
  
  realityVertexEnd(ctx);
}


/**
 *
 */
void
glw_rtt_init(glw_root_t *gr, glw_rtt_t *grtt, int width, int height,
	     int alpha)
{
}


/**
 *
 */
void
glw_rtt_enter(glw_root_t *gr, glw_rtt_t *grtt, glw_rctx_t *rc)
{
}


/**
 *
 */
void
glw_rtt_restore(glw_root_t *gr, glw_rtt_t *grtt)
{

}


/**
 *
 */
void
glw_rtt_destroy(glw_root_t *gr, glw_rtt_t *grtt)
{
}

