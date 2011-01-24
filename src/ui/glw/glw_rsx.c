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


/**
 *
 */
typedef struct rsx_vp {
  realityVertexProgram *rvp_binary;

  int rvp_u_modelview;
  int rvp_u_color;

  int rvp_a_position;
  int rvp_a_color;
  int rvp_a_texcoord;

} rsx_vp_t;


/**
 *
 */
typedef struct rsx_fp {
  realityFragmentProgram *rfp_binary;

  int rfp_rsx_location;  // location in RSX memory

} rsx_fp_t;


/**
 *
 */
static int
vp_get_vector_const(realityVertexProgram *vp, const char *name)
{
  int v = realityVertexProgramGetConstant(vp, name);
  if(v == -1)
    return -1;
  realityProgramConst *c = realityVertexProgramGetConstants(vp);
  return c[v].index;
}

/**
 *
 */
static rsx_vp_t *
load_vp(const char *url)
{
  char errmsg[100];
  realityVertexProgram *vp;
  int i;
  const char *name;

  if((vp = fa_quickload(url, NULL, NULL, errmsg, sizeof(errmsg))) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, log);
    return NULL;
  }

  TRACE(TRACE_INFO, "glw", "Loaded Vertex program %s", url);
  TRACE(TRACE_INFO, "glw", "    input mask: %x", 
	realityVertexProgramGetInputMask(vp));
  TRACE(TRACE_INFO, "glw", "   output mask: %x", 
	realityVertexProgramGetOutputMask(vp));

  realityProgramConst *constants;
  constants = realityVertexProgramGetConstants(vp);
  for(i = 0; i < vp->num_const; i++) {
    if(constants[i].name_off)
      name = ((char*)vp)+constants[i].name_off;
    else
      name = "<anon>";

    TRACE(TRACE_INFO, "glw", "  Constant %s @ 0x%x [%f, %f, %f, %f]",
	  name,
	  constants[i].index,
	  constants[i].values[0].f,
	  constants[i].values[1].f,
	  constants[i].values[2].f,
	  constants[i].values[3].f);
  }

  realityProgramAttrib *attributes;
  attributes = realityVertexProgramGetAttributes(vp);
  for(i = 0; i < vp->num_attrib; i++) {
    if(attributes[i].name_off)
      name = ((char*)vp)+attributes[i].name_off;
    else
      name = "<anon>";

    TRACE(TRACE_INFO, "glw", "  Attribute %s @ 0x%x",
	  name, attributes[i].index);
  }

  rsx_vp_t *rvp = calloc(1, sizeof(rsx_vp_t));
  rvp->rvp_binary = vp;

  rvp->rvp_u_modelview = realityVertexProgramGetConstant(vp, "u_modelview");
  rvp->rvp_u_color     = vp_get_vector_const(vp, "u_color");
  TRACE(TRACE_INFO, "glw", "%d %d", rvp->rvp_u_modelview, rvp->rvp_u_color);

  rvp->rvp_a_position = realityVertexProgramGetAttribute(vp, "a_position");
  rvp->rvp_a_color    = realityVertexProgramGetAttribute(vp, "a_color");
  rvp->rvp_a_texcoord = realityVertexProgramGetAttribute(vp, "a_texcoord");
  TRACE(TRACE_INFO, "glw", "%d %d %d",
	rvp->rvp_a_position, rvp->rvp_a_color, rvp->rvp_a_texcoord);

  return rvp;
}

/**
 *
 */
static rsx_fp_t *
load_fp(glw_root_t *gr, const char *url)
{
  char errmsg[100];
  realityFragmentProgram *fp;
  int i;
  const char *name;

  if((fp = fa_quickload(url, NULL, NULL, errmsg, sizeof(errmsg))) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, log);
    return NULL;
  }

  TRACE(TRACE_INFO, "glw", "Loaded fragment program %s", url);
  TRACE(TRACE_INFO, "glw", "  num regs: %d", fp->num_regs);

  realityProgramConst *constants;
  constants = realityFragmentProgramGetConsts(fp);
  for(i = 0; i < fp->num_const; i++) {
    if(constants[i].name_off)
      name = ((char*)fp)+constants[i].name_off;
    else
      name = "<anon>";

    TRACE(TRACE_INFO, "glw", "  Constant %s @ 0x%x [%f, %f, %f, %f]",
	  name,
	  constants[i].index,
	  constants[i].values[0].f,
	  constants[i].values[1].f,
	  constants[i].values[2].f,
	  constants[i].values[3].f);
  }

  realityProgramAttrib *attributes;
  attributes = realityFragmentProgramGetAttribs(fp);
  for(i = 0; i < fp->num_attrib; i++) {
    if(attributes[i].name_off)
      name = ((char*)fp)+attributes[i].name_off;
    else
      name = "<anon>";

    TRACE(TRACE_INFO, "glw", "  Attribute %s @ 0x%x",
	  name, attributes[i].index);
  }

  int offset = rsx_alloc(gr, fp->num_insn * 16, 256);
  uint32_t *buf = rsx_to_ppu(gr, offset);
  TRACE(TRACE_INFO, "glw", "  PPU location: 0x%08x  %d bytes",
	buf, fp->num_insn * 16);
  const uint32_t *src = (uint32_t *)((char*)fp + fp->ucode_off);

  memcpy(buf, src, fp->num_insn * 16);
  TRACE(TRACE_INFO, "glw", "  RSX location: 0x%08x", offset);

  rsx_fp_t *rfp = calloc(1, sizeof(rsx_fp_t));
  rfp->rfp_binary = fp;
  rfp->rfp_rsx_location = offset;

  return rfp;
}

static unsigned int v_offset;


/**
 *
 */
int
glw_rsx_init_context(glw_root_t *gr)
{
  gr->gr_be.be_vp_1 = load_vp("bundle://src/ui/glw/rsx/v1.vp");
  gr->gr_be.be_fp_tex = load_fp(gr, "bundle://src/ui/glw/rsx/f_tex.fp");
  gr->gr_be.be_fp_flat = load_fp(gr, "bundle://src/ui/glw/rsx/f_flat.fp");




  v_offset = rsx_alloc(gr, 10 * 4 * sizeof(float), 64);
  float *v = rsx_to_ppu(gr, v_offset);

  v[ 0] = -1.0; v[ 1] = -1.0; v[ 2] =  0.0; v[ 3] = -1.0;
  v[ 4] =  0.0; v[ 5] =  1.0;
  v[ 6] =  1.0; v[ 7] =  1.0; v[ 8] =  1.0; v[ 9] =  1.0;

  v[10] =  1.0; v[11] = -1.0; v[12] =  0.0; v[13] = -1.0;
  v[14] =  1.0; v[15] =  1.0;
  v[16] =  1.0; v[17] =  1.0; v[18] =  1.0; v[19] =  1.0;

  v[20] =  1.0; v[21] =  1.0; v[22] =  0.0; v[23] = -1.0;
  v[24] =  1.0; v[25] =  0.0;
  v[26] =  1.0; v[27] =  1.0; v[28] =  1.0; v[29] =  1.0;

  v[30] = -1.0; v[31] =  1.0; v[32] =  0.0; v[33] = -1.0;
  v[34] =  0.0; v[35] =  0.0;
  v[36] =  1.0; v[37] =  1.0; v[38] =  1.0; v[39] =  1.0;

  TRACE(TRACE_INFO, "GLW", "Vertex buffer location: RSX: 0x%08x  PPU: %p",
	v_offset, v);
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
static void
set_vp(glw_root_t *root, rsx_vp_t *rvp)
{
  if(root->gr_be.be_vp_current == rvp)
    return;
  root->gr_be.be_vp_current = rvp;
  realityLoadVertexProgram(root->gr_be.be_ctx, rvp->rvp_binary);
}


/**
 *
 */
static void
set_fp(glw_root_t *root, rsx_fp_t *rfp)
{
  if(root->gr_be.be_fp_current == rfp)
    return;
  root->gr_be.be_fp_current = rfp;
  realityLoadFragmentProgram(root->gr_be.be_ctx, rfp->rfp_binary,
			     rfp->rfp_rsx_location, 0);
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
  rsx_vp_t *rvp = root->gr_be.be_vp_1;
  rsx_fp_t *rfp = root->gr_be.be_fp_tex;
  float rgba[4];

  set_vp(root, rvp);
  set_fp(root, rfp);


  realitySetVertexProgramConstant4fBlock(ctx, rvp->rvp_binary,
					 rvp->rvp_u_modelview,
					 4, rc->rc_mtx);
  
  if(rgb != NULL) {
    rgba[0] = rgb->r;
    rgba[1] = rgb->g;
    rgba[2] = rgb->b;
  } else {
    rgba[0] = 1;
    rgba[1] = 1;
    rgba[2] = 1;
  }
  rgba[3] = alpha;

  realitySetVertexProgramConstant4f(ctx, rvp->rvp_u_color, rgba);
  realitySetTexture(ctx, 0, &be_tex->tex);
  
  realityBindVertexBufferAttribute(ctx, rvp->rvp_a_position, v_offset, 40, 3, 
				   REALITY_BUFFER_DATATYPE_FLOAT,
				   REALITY_RSX_MEMORY);
  realityBindVertexBufferAttribute(ctx, rvp->rvp_a_color, v_offset+(6*4), 40, 4,
				   REALITY_BUFFER_DATATYPE_FLOAT,
				   REALITY_RSX_MEMORY);
  realityBindVertexBufferAttribute(ctx, rvp->rvp_a_texcoord, v_offset+(4*4), 40, 2,
				   REALITY_BUFFER_DATATYPE_FLOAT,
				   REALITY_RSX_MEMORY);


  realityDrawVertexBuffer(ctx, REALITY_QUADS, 0, 4);
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

