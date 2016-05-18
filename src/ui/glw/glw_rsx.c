/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <string.h>

#include <rsx/reality.h>
#include <rsx/nv40.h>

#include "glw.h"
#include "glw_renderer.h"
#include "fileaccess/fileaccess.h"

#define RSX_TRACE(fmt...) // TRACE(TRACE_DEBUG, "RSX", fmt)

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
load_vp(const char *filename)
{
  char errmsg[100];
  buf_t *b;
  int i;
  const char *name;
  char url[512];

  snprintf(url, sizeof(url), "dataroot://res/shaders/rsx/%s", filename);

  if((b = fa_load(url,
                  FA_LOAD_ERRBUF(errmsg, sizeof(errmsg)),
                  NULL)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, errmsg);
    return NULL;
  }

  realityVertexProgram *vp = b->b_ptr;

  RSX_TRACE("Loaded Vertex program %s", url);
  RSX_TRACE("    input mask: %x", 
	realityVertexProgramGetInputMask(vp));
  RSX_TRACE("   output mask: %x", 
	realityVertexProgramGetOutputMask(vp));

  realityProgramConst *constants;
  constants = realityVertexProgramGetConstants(vp);
  for(i = 0; i < vp->num_const; i++) {
    if(constants[i].name_off)
      name = ((char*)vp)+constants[i].name_off;
    else
      name = "<anon>";

    RSX_TRACE("  Constant %s @ 0x%x [%f, %f, %f, %f]",
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

    RSX_TRACE("  Attribute %s @ 0x%x",
	  name, attributes[i].index);
  }

  rsx_vp_t *rvp = calloc(1, sizeof(rsx_vp_t));
  rvp->rvp_binary = vp;

  rvp->rvp_u_modelview = realityVertexProgramGetConstant(vp, "u_modelview");
  rvp->rvp_u_color     = vp_get_vector_const(vp, "u_color");
  rvp->rvp_u_color_offset = vp_get_vector_const(vp, "u_color_offset");
  rvp->rvp_u_blur = vp_get_vector_const(vp, "u_blur");

  RSX_TRACE("%d %d", rvp->rvp_u_modelview, rvp->rvp_u_color);

  rvp->rvp_a_position = realityVertexProgramGetAttribute(vp, "a_position");
  rvp->rvp_a_color    = realityVertexProgramGetAttribute(vp, "a_color");
  rvp->rvp_a_texcoord = realityVertexProgramGetAttribute(vp, "a_texcoord");
  RSX_TRACE("%d %d %d",
	rvp->rvp_a_position, rvp->rvp_a_color, rvp->rvp_a_texcoord);

  return rvp;
}

/**
 *
 */
static rsx_fp_t *
load_fp(glw_root_t *gr, const char *filename)
{
  char errmsg[100];
  buf_t *b;
  int i;
  const char *name;

  char url[512];
  snprintf(url, sizeof(url), "dataroot://res/shaders/rsx/%s", filename);

  if((b = fa_load(url, FA_LOAD_ERRBUF(errmsg, sizeof(errmsg)),
                   NULL)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load shader %s -- %s\n",
	  url, errmsg);
    return NULL;
  }

  realityFragmentProgram *fp = b->b_ptr;
  RSX_TRACE("Loaded fragment program %s", url);
  RSX_TRACE("  num regs: %d", fp->num_regs);

  realityProgramConst *constants;
  constants = realityFragmentProgramGetConsts(fp);
  for(i = 0; i < fp->num_const; i++) {
    if(constants[i].name_off)
      name = ((char*)fp)+constants[i].name_off;
    else
      name = "<anon>";

    RSX_TRACE("  Constant %s @ 0x%x [%f, %f, %f, %f] type=%d",
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

    RSX_TRACE("  Attribute %s @ 0x%x",
	  name, attributes[i].index);
  }

  int offset = rsx_alloc(fp->num_insn * 16, 256);
  uint32_t *buf = rsx_to_ppu(offset);
  RSX_TRACE("  PPU location: 0x%08x  %d bytes",
	buf, fp->num_insn * 16);
  const uint32_t *src = (uint32_t *)((char*)fp + fp->ucode_off);

  memcpy(buf, src, fp->num_insn * 16);
  RSX_TRACE("  RSX location: 0x%08x", offset);

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
      RSX_TRACE("    Texture %d via unit %d",
	    i, rfp->rfp_texunit[i]);
  }

  return rfp;
}


/**
 *
 */
static void
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
static void
rsx_set_fp(glw_root_t *root, rsx_fp_t *rfp)
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
static void
rsx_render_unlocked(glw_root_t *gr)
{
  gcmContextData *ctx = gr->gr_be.be_ctx;
  const float *vertices = gr->gr_vertex_buffer;

  int current_blendmode = GLW_BLEND_NORMAL;

  realityBlendFunc(ctx,
                   NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA |
                   NV30_3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
                   NV30_3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
                   NV30_3D_BLEND_FUNC_DST_ALPHA_ZERO);

  int current_frontface = GLW_CCW;

  realityFrontFace(ctx, REALITY_FRONT_FACE_CCW);

  for(int j = 0; j < gr->gr_num_render_jobs; j++) {
    const glw_render_order_t *ro = gr->gr_render_order + j;
    const glw_render_job_t *rj = ro->job;
    const struct glw_backend_texture *t0 = rj->t0;
    const struct glw_backend_texture *t1 = rj->t1;
    rsx_vp_t *rvp = gr->gr_be.be_vp_1;
    rsx_fp_t *rfp;
    float rgba[4];


    if(unlikely(rj->gpa != NULL)) {

      glw_program_args_t *gpa = rj->gpa;

      if(t1 != NULL) {

        if(gpa->gpa_load_texture != NULL) {
          // Program has specialized code to load textures, run it
          gpa->gpa_load_texture(gr, gpa->gpa_prog, gpa->gpa_aux, t1, 1);
        } else {
          // not supported
        }
      }

      if(t0 != NULL) {

        if(gpa->gpa_load_texture != NULL) {
          // Program has specialized code to load textures, run it
          gpa->gpa_load_texture(gr, gpa->gpa_prog, gpa->gpa_aux, t0, 0);
        } else {
          // not supported
        }
      }

      rfp = gpa->gpa_prog->gp_fragment_program;
      rvp = gpa->gpa_prog->gp_vertex_program;

      rsx_set_vp(gr, rvp);

      if(gpa->gpa_load_uniforms != NULL)
        gpa->gpa_load_uniforms(gr, gpa->gpa_prog, gpa->gpa_aux, rj);

    } else {

      if(t0 == NULL) {

        if(t1 != NULL) {
          rfp = gr->gr_be.be_fp_flat_stencil;

          if(t1->tex.offset == 0 || t1->size == 0)
            continue;

          realitySetTexture(ctx, 0, &t1->tex);
        } else {
          rfp = gr->gr_be.be_fp_flat;

        }

      } else {

        if(t0->tex.offset == 0 || t0->size == 0)
          continue;

        const int doblur = rj->blur > 0.05 ||
          rj->flags & GLW_RENDER_BLUR_ATTRIBUTE;

        realitySetTexture(ctx, 0, &t0->tex);

        if(t1 != NULL) {
          rfp = doblur ? gr->gr_be.be_fp_tex_stencil_blur :
            gr->gr_be.be_fp_tex_stencil;

          if(t1->tex.offset == 0 || t1->size == 0)
            continue;

          realitySetTexture(ctx, 1, &t1->tex);

        } else {
          rfp = doblur ? gr->gr_be.be_fp_tex_blur : gr->gr_be.be_fp_tex;
        }
      }
      rsx_set_vp(gr, rvp);
    }

    realitySetVertexProgramConstant4fBlock(ctx, rvp->rvp_binary,
                                           rvp->rvp_u_modelview,
                                           4,
                                           rj->eyespace ? identitymtx :
                                           (const float *)&rj->m);

    const float alpha = rj->alpha;

    if(likely(rj->blendmode == GLW_BLEND_NORMAL)) {
      rgba[0] = rj->rgb_mul.r;
      rgba[1] = rj->rgb_mul.g;
      rgba[2] = rj->rgb_mul.b;
      rgba[3] = alpha;

    } else {
      rgba[0] = rj->rgb_mul.r * alpha;
      rgba[1] = rj->rgb_mul.g * alpha;
      rgba[2] = rj->rgb_mul.b * alpha;
      rgba[3] = 1;
    }

    if(likely(rvp->rvp_u_color != -1))
      realitySetVertexProgramConstant4f(ctx, rvp->rvp_u_color, rgba);

    if(unlikely(current_blendmode != rj->blendmode)) {
      current_blendmode = rj->blendmode;
      switch(rj->blendmode) {
      case GLW_BLEND_ADDITIVE:
        realityBlendFunc(ctx,
                         NV30_3D_BLEND_FUNC_SRC_RGB_SRC_COLOR,
                         NV30_3D_BLEND_FUNC_DST_RGB_ONE);
        break;
      case GLW_BLEND_NORMAL:
        realityBlendFunc(ctx,
                         NV30_3D_BLEND_FUNC_SRC_RGB_SRC_ALPHA |
                         NV30_3D_BLEND_FUNC_SRC_ALPHA_SRC_ALPHA,
                         NV30_3D_BLEND_FUNC_DST_RGB_ONE_MINUS_SRC_ALPHA |
                         NV30_3D_BLEND_FUNC_DST_ALPHA_ZERO);
        break;
      }
    }

    if(unlikely(current_frontface != rj->frontface)) {
      current_frontface = rj->frontface;
      realityFrontFace(ctx,
                       current_frontface == GLW_CW ? REALITY_FRONT_FACE_CW :
                       REALITY_FRONT_FACE_CCW);
    }

    if(rvp->rvp_u_color_offset != -1) {
      rgba[0] = rj->rgb_off.r;
      rgba[1] = rj->rgb_off.g;
      rgba[2] = rj->rgb_off.b;
      rgba[3] = 0;
      realitySetVertexProgramConstant4f(ctx, rvp->rvp_u_color_offset, rgba);
    }

    if(rfp == gr->gr_be.be_fp_tex_blur) {
      float v[4];
      v[0] = rj->blur;
      v[1] = 1.5 / t0->tex.width;
      v[2] = 1.5 / t0->tex.height;
      v[3] = 0;
      realitySetVertexProgramConstant4f(ctx,  rvp->rvp_u_blur, v);
    }

    rsx_set_fp(gr, rfp);

    realityVertexBegin(ctx, rj->primitive_type);

    const uint16_t *idx = gr->gr_index_buffer + rj->index_offset;

    for(int i = 0; i < rj->num_indices; i++) {

      const float *v = &vertices[idx[i] * VERTEX_SIZE];

      realityAttr4f(ctx,  rvp->rvp_a_texcoord,  v[8], v[9], v[10], v[11]);

      if(unlikely(rvp->rvp_a_color) != -1)
        realityAttr4f(ctx, rvp->rvp_a_color, v[4], v[5], v[6], v[7]);

      realityVertex4f(ctx, v[0], v[1], v[2], v[3]);
    }
    realityVertexEnd(ctx);
  }

}


/**
 *
 */
int
glw_rsx_init_context(glw_root_t *gr)
{
  glw_backend_root_t *be = &gr->gr_be;

  gr->gr_be_render_unlocked = rsx_render_unlocked;

  be->be_vp_1          = load_vp("v1.vp");
  be->be_fp_tex        = load_fp(gr, "f_tex.fp");
  be->be_fp_flat       = load_fp(gr, "f_flat.fp");
  be->be_fp_tex_blur   = load_fp(gr, "f_tex_blur.fp");
  be->be_fp_tex_stencil  = load_fp(gr, "f_tex_stencil.fp");
  be->be_fp_flat_stencil = load_fp(gr, "f_flat_stencil.fp");
  be->be_fp_tex_stencil_blur = load_fp(gr, "f_tex_stencil_blur.fp");

  be->be_yuv2rgb_1f.gp_vertex_program =
  be->be_yuv2rgb_2f.gp_vertex_program = load_vp("yuv2rgb_v.vp");

  be->be_yuv2rgb_1f.gp_fragment_program = load_fp(gr, "yuv2rgb_1f_norm.fp");
  be->be_yuv2rgb_2f.gp_fragment_program = load_fp(gr, "yuv2rgb_2f_norm.fp");

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
 * Not implemented yet
 */
struct glw_program *
glw_make_program(struct glw_root *gr,
		 const char *vertex_shader,
		 const char *fragment_shader)
{
  return NULL;
}

void
glw_destroy_program(struct glw_root *gr, struct glw_program *gp)
{

}
