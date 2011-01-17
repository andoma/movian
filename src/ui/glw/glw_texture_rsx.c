/*
 *  GL Widgets, Texture loader
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

#include <assert.h>
#include <unistd.h>
#include <string.h>

#include <libswscale/swscale.h>

#include "glw.h"
#include "glw_texture.h"

#include "rsx/nv40.h"
#include "rsx/reality.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_loadable_texture_t *glt)
{
  if(glt->glt_texture.mem != NULL) {
    TRACE(TRACE_ERROR, "GLW", "glw_tex_backend_free_render_resources()");
    glt->glt_texture.mem = NULL;
  }
}


/**
 * Free resources created by glw_tex_backend_decode()
 */
void
glw_tex_backend_free_loader_resources(glw_loadable_texture_t *glt)
{
}


/**
 * Invoked on every frame when status == VALID
 */
void
glw_tex_backend_layout(glw_root_t *gr, glw_loadable_texture_t *glt)
{

}


static void
init_tex(realityTexture *tex, uint32_t offset,
	 uint32_t width, uint32_t height, uint32_t stride,
	 uint32_t fmt, int repeat)
{
  tex->swizzle =
    NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
    NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
    NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
    NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W ;
  
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


/**
 *
 */
static void
init_argb(glw_backend_texture_t *tex, const uint8_t *src, int linesize,
	  int width, int height, int repeat)
{
  int buffersize = linesize * height;
  uint32_t offset = 0;

  tex->mem = rsxMemAlign(16, buffersize);
  realityAddressToOffset(tex->mem, &offset);

  TRACE(TRACE_DEBUG, "GLW", "Init ARGB %d x %d, buffer=%d bytes @ %p [%x]",
	width, height, buffersize, tex->mem, offset);

  memcpy(tex->mem, src, buffersize);
  init_tex(&tex->tex, offset, width, height, linesize, 
	   NV40_3D_TEX_FORMAT_FORMAT_A8R8G8B8, repeat);
  
}

/**
 *
 */
int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     AVPicture *pict, int pix_fmt, 
		     int src_w, int src_h,
		     int req_w, int req_h)
{
  TRACE(TRACE_DEBUG, "GLW", "Texture load %d x %d => %d x %d",
	src_w, src_h, req_w, req_h);

  int need_rescale = req_w != src_w || req_h != src_h;
  int repeat = glt->glt_flags & GLW_TEX_REPEAT;
  switch(pix_fmt) {

  case PIX_FMT_ARGB:
    if(need_rescale)
      break;
    glt->glt_xs = src_w;
    glt->glt_ys = src_h;
    init_argb(&glt->glt_texture, pict->data[0], pict->linesize[0],
	      src_w, src_h, repeat);
    return 0;
  default:
    TRACE(TRACE_DEBUG, "GLW", "Can't deal with pixfmt %d", pix_fmt);
    return -1;
  }
  TRACE(TRACE_DEBUG, "GLW", "Can't scale");
  return -1;
}

/**
 *
 */
void
glw_tex_upload(const glw_root_t *gr, glw_backend_texture_t *tex, 
	       const void *src, int fmt, int width, int height, int flags)
{
  TRACE(TRACE_DEBUG, "GLW", "Texture upload %d x %d", width, height);
}


/**
 *
 */
void
glw_tex_destroy(glw_backend_texture_t *tex)
{
  if(tex->mem != 0) {
    TRACE(TRACE_ERROR, "GLW", "glw_tex_destroy()");
    tex->mem = NULL;
  }
}
