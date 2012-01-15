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

#include <rsx/reality.h>
#include <rsx/nv40.h>

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"

static float identitymtx[16] = {
  1,0,0,0,
  0,1,0,0,
  0,0,1,0,
  0,0,0,1};


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

  if((vp = fa_load(url, NULL, NULL, errmsg, sizeof(errmsg), NULL)) == NULL) {
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
  rvp->rvp_u_color_offset = vp_get_vector_const(vp, "u_color_offset");
  rvp->rvp_u_blur_amount = vp_get_vector_const(vp, "u_blur_amount");
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

  if((fp = fa_load(url, NULL, NULL, errmsg, sizeof(errmsg), NULL)) == NULL) {
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

    TRACE(TRACE_INFO, "glw", "  Constant %s @ 0x%x [%f, %f, %f, %f] type=%d",
	  name,
	  constants[i].index,
	  constants[i].values[0].f,
	  constants[i].values[1].f,
	  constants[i].values[2].f,
	  constants[i].values[3].f,
	  constants[i].type);
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

  rfp->rfp_u_color =
    realityFragmentProgramGetConst(fp, "u_color");

  rfp->rfp_u_color_matrix =
    realityFragmentProgramGetConst(fp, "u_colormtx");

  rfp->rfp_u_blend =
    realityFragmentProgramGetConst(fp, "u_blend");

  for(i = 0; i < 6; i++) {
    char name[8];
    snprintf(name, sizeof(name), "u_t%d", i);
    rfp->rfp_texunit[i] = 
      realityFragmentProgramGetAttrib(fp, name);
    if(rfp->rfp_texunit[i] != -1)
      TRACE(TRACE_INFO, "glw", "    Texture %d via unit %d",
	    i, rfp->rfp_texunit[i]);
  }

  return rfp;
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
rsx_set_vp(glw_root_t *root, rsx_vp_t *rvp)
{
  if(root->gr_be.be_vp_current == rvp)
    return;
  root->gr_be.be_vp_current = rvp;
  realityLoadVertexProgram(root->gr_be.be_ctx, rvp->rvp_binary);
}


/**
 *
 */
void
rsx_set_fp(glw_root_t *root, rsx_fp_t *rfp, int force)
{
  if(root->gr_be.be_fp_current == rfp && !force)
    return;
  root->gr_be.be_fp_current = rfp;
  realityLoadFragmentProgram(root->gr_be.be_ctx, rfp->rfp_binary,
			     rfp->rfp_rsx_location, 0);
}


/**
 *
 */
static void
rsx_render(struct glw_root *gr,
	   Mtx m,
	   struct glw_backend_texture *tex,
	   const struct glw_rgb *rgb_mul,
	   const struct glw_rgb *rgb_off,
	   float alpha, float blur,
	   const float *vertices,
	   int num_vertices,
	   const uint16_t *indices,
	   int num_triangles,
	   int flags)
{
  gcmContextData *ctx = gr->gr_be.be_ctx;
  rsx_vp_t *rvp = gr->gr_be.be_vp_1;
  rsx_fp_t *rfp;
  float rgba[4];

  if(tex == NULL) {

    rfp = gr->gr_be.be_fp_flat;

  } else {

    if(tex->tex.offset == 0 || tex->size == 0)
      return;

    realitySetTexture(ctx, 0, &tex->tex);
    if(blur > 0.05) {
      rfp = gr->gr_be.be_fp_tex_blur;
    } else {
      rfp = gr->gr_be.be_fp_tex;
    }
  }

  rsx_set_vp(gr, rvp);

  realitySetVertexProgramConstant4fBlock(ctx, rvp->rvp_binary,
					 rvp->rvp_u_modelview,
					 4, m ?: identitymtx);
  
  switch(gr->gr_be.be_blendmode) {
  case GLW_BLEND_NORMAL:
    rgba[0] = rgb_mul->r;
    rgba[1] = rgb_mul->g;
    rgba[2] = rgb_mul->b;
    rgba[3] = alpha;
    break;

  case GLW_BLEND_ADDITIVE:
    rgba[0] = rgb_mul->r * alpha;
    rgba[1] = rgb_mul->g * alpha;
    rgba[2] = rgb_mul->b * alpha;
    rgba[3] = 1;
    break;
  }

  realitySetVertexProgramConstant4f(ctx, rvp->rvp_u_color, rgba);

  if(rvp->rvp_u_color_offset != -1) {

    if(rgb_off != NULL) {
      rgba[0] = rgb_off->r;
      rgba[1] = rgb_off->g;
      rgba[2] = rgb_off->b;
    } else {
      rgba[0] = 0;
      rgba[1] = 0;
      rgba[2] = 0;
    }
    rgba[3] = 0;
    realitySetVertexProgramConstant4f(ctx, rvp->rvp_u_color_offset, rgba);
  }

  if(blur > 0.05 && tex != NULL) {
    float v[4];
    v[0] = 1.5 * blur / tex->tex.width;
    v[1] = 1.5 * blur / tex->tex.height;
    v[2] = 0;
    v[3] = 0;

    realitySetVertexProgramConstant4f(ctx,  rvp->rvp_u_blur_amount, v);
  }

  rsx_set_fp(gr, rfp, 0);

  // TODO: Get rid of immediate mode
  realityVertexBegin(ctx, REALITY_TRIANGLES);

  int i;

  if(indices != NULL) {
    for(i = 0; i < num_triangles * 3; i++) {
      const float *v = &vertices[indices[i] * VERTEX_SIZE];
      realityAttr2f(ctx,  rvp->rvp_a_texcoord,  v[3], v[4]);
      realityAttr4f(ctx, rvp->rvp_a_color, v[5], v[6], v[7], v[8]);
      realityVertex4f(ctx, v[0], v[1], v[2], 1);
    }
  } else {
    for(i = 0; i < num_vertices; i++) {
      const float *v = &vertices[i * VERTEX_SIZE];
      realityAttr2f(ctx,  rvp->rvp_a_texcoord,  v[3], v[4]);
      realityAttr4f(ctx, rvp->rvp_a_color, v[5], v[6], v[7], v[8]);
      realityVertex4f(ctx, v[0], v[1], v[2], 1);
    }
  }
  realityVertexEnd(ctx);
}



/**
 *
 */
int
glw_rsx_init_context(glw_root_t *gr)
{
  glw_backend_root_t *be = &gr->gr_be;

  gr->gr_normalized_texture_coords = 1;
  gr->gr_render = rsx_render;
  
  be->be_vp_1 = load_vp("bundle://src/ui/glw/rsx/v1.vp");
  be->be_fp_tex = load_fp(gr, "bundle://src/ui/glw/rsx/f_tex.fp");
  be->be_fp_flat = load_fp(gr, "bundle://src/ui/glw/rsx/f_flat.fp");
  be->be_fp_tex_blur = load_fp(gr, "bundle://src/ui/glw/rsx/f_tex_blur.fp");
  
  be->be_vp_yuv2rgb = load_vp("bundle://src/ui/glw/rsx/yuv2rgb_v.vp");
  be->be_fp_yuv2rgb_1f =
    load_fp(gr, "bundle://src/ui/glw/rsx/yuv2rgb_1f_norm.fp");
  be->be_fp_yuv2rgb_2f =
    load_fp(gr, "bundle://src/ui/glw/rsx/yuv2rgb_2f_norm.fp");

  return 0;
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


/**
 *
 */
void
glw_blendmode(struct glw_root *gr, int mode)
{
  if(mode == gr->gr_be.be_blendmode)
    return;
  gr->gr_be.be_blendmode = mode;

  switch(mode) {
  case GLW_BLEND_ADDITIVE:
    realityBlendFunc(gr->gr_be.be_ctx,
		     NV30_3D_BLEND_FUNC_SRC_RGB_SRC_COLOR,
		     NV30_3D_BLEND_FUNC_DST_RGB_ONE);
    break;
  case GLW_BLEND_NORMAL:
    realityBlendFunc(gr->gr_be.be_ctx,
		     NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA |
		     NV30_3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
		     NV30_3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
		     NV30_3D_BLEND_FUNC_DST_ALPHA_ZERO);
    break;
  }
}


void glw_frontface(struct glw_root *gr, int how)
{
  realityFrontFace(gr->gr_be.be_ctx,
		   how == GLW_CW ? REALITY_FRONT_FACE_CW :
		   REALITY_FRONT_FACE_CCW);
}

