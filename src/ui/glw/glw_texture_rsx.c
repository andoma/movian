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

#include "glw.h"
#include "glw_texture.h"

#include "rsx/nv40.h"
#include "rsx/reality.h"

/**
 * Free texture (always invoked in main rendering thread)
 */
void
glw_tex_backend_free_render_resources(glw_root_t *gr, 
				      glw_loadable_texture_t *glt)
{
  glw_tex_destroy(gr, &glt->glt_texture);
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


/**
 *
 */
static void *
realloc_tex(glw_root_t *gr, glw_backend_texture_t *tex, int size)
{
  if(tex->size != size) {

    if(tex->size != 0)
      rsx_free(gr, tex->tex.offset, tex->size);

    tex->size = size;

    if(tex->size != 0)
      tex->tex.offset = rsx_alloc(gr, tex->size, 16);
  }
  return tex->size ? rsx_to_ppu(gr, tex->tex.offset) : NULL;
}

#if 0
/**
 *
 */
static void
init_argb(glw_root_t *gr, glw_backend_texture_t *tex,
	  const uint8_t *src, int linesize,
	  int width, int height, int repeat)
{
  void *mem = realloc_tex(gr, tex, linesize * height);

  memcpy(mem, src, tex->size);
  init_tex(&tex->tex, tex->tex.offset, width, height, linesize, 
	   NV40_3D_TEX_FORMAT_FORMAT_A8R8G8B8, repeat,
	   NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	   NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	   NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	   NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	   );
}
#endif

/**
 *
 */
static void
init_abgr(glw_root_t *gr, glw_backend_texture_t *tex,
	  const uint8_t *src, int linesize,
	  int width, int height, int repeat)
{
  void *mem = realloc_tex(gr, tex, linesize * height);

  memcpy(mem, src, tex->size);
  init_tex(&tex->tex, tex->tex.offset, width, height, linesize,
	   NV40_3D_TEX_FORMAT_FORMAT_A8R8G8B8, repeat,
	   NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	   NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	   NV30_3D_TEX_SWIZZLE_S1_X_Z | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	   NV30_3D_TEX_SWIZZLE_S1_Z_X | NV30_3D_TEX_SWIZZLE_S1_W_W
	   );
}

#if 0
/**
 *
 */
static void
init_rgba(glw_root_t *gr, glw_backend_texture_t *tex,
	  const uint8_t *src, int linesize,
	  int width, int height, int repeat)
{
  void *mem = realloc_tex(gr, tex, linesize * height);

  memcpy(mem, src, tex->size);
  init_tex(&tex->tex, tex->tex.offset, width, height, linesize, 
	   NV40_3D_TEX_FORMAT_FORMAT_A8R8G8B8, repeat,
	   NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	   NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	   NV30_3D_TEX_SWIZZLE_S1_X_Y | NV30_3D_TEX_SWIZZLE_S1_Y_Z |
	   NV30_3D_TEX_SWIZZLE_S1_Z_W | NV30_3D_TEX_SWIZZLE_S1_W_X
	   );
}
#endif

/**
 *
 */
static void
init_rgb(glw_root_t *gr, glw_backend_texture_t *tex,
	 const uint8_t *src, int linesize,
	 int width, int height, int repeat)
{
  int y, x;
  uint32_t *dst = realloc_tex(gr, tex, width * height * 4);

  for(y = 0; y < height; y++) {
    const uint8_t *s = src;
    for(x = 0; x < width; x++) {
      uint8_t r = *s++;
      uint8_t g = *s++;
      uint8_t b = *s++;
      *dst++ = 0xff000000 | (r << 16) | (g << 8) | b;
    }
    src += linesize;
  }

  init_tex(&tex->tex, tex->tex.offset, width, height, width * 4, 
	   NV40_3D_TEX_FORMAT_FORMAT_A8R8G8B8, repeat,
	   NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	   NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	   NV30_3D_TEX_SWIZZLE_S1_X_X | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	   NV30_3D_TEX_SWIZZLE_S1_Z_Z | NV30_3D_TEX_SWIZZLE_S1_W_W
	   );
}


/**
 *
 */
static void
init_i8a8(glw_root_t *gr, glw_backend_texture_t *tex,
	  const uint8_t *src, int linesize,
	  int width, int height, int repeat)
{
  void *mem = realloc_tex(gr, tex, linesize * height);

  memcpy(mem, src, tex->size);
  init_tex(&tex->tex, tex->tex.offset, width, height, linesize, 
	   NV40_3D_TEX_FORMAT_FORMAT_A8L8, repeat,
	   NV30_3D_TEX_SWIZZLE_S0_X_S1 | NV30_3D_TEX_SWIZZLE_S0_Y_S1 |
	   NV30_3D_TEX_SWIZZLE_S0_Z_S1 | NV30_3D_TEX_SWIZZLE_S0_W_S1 |
	   NV30_3D_TEX_SWIZZLE_S1_X_Y | NV30_3D_TEX_SWIZZLE_S1_Y_Y |
	   NV30_3D_TEX_SWIZZLE_S1_Z_Y | NV30_3D_TEX_SWIZZLE_S1_W_X);
}

/**
 *
 */
int
glw_tex_backend_load(glw_root_t *gr, glw_loadable_texture_t *glt,
		     pixmap_t *pm)
{
  int repeat = glt->glt_flags & GLW_TEX_REPEAT;

  glt->glt_xs = pm->pm_width;
  glt->glt_ys = pm->pm_height;
  glt->glt_s = 1;
  glt->glt_t = 1;

  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    init_abgr(gr, &glt->glt_texture, pm->pm_data, pm->pm_linesize,
	      pm->pm_width, pm->pm_height, repeat);
    break;

  case PIXMAP_RGB24:
    init_rgb(gr, &glt->glt_texture, pm->pm_data, pm->pm_linesize,
	      pm->pm_width, pm->pm_height, repeat);
    break;

  case PIXMAP_IA:
    init_i8a8(gr, &glt->glt_texture, pm->pm_data, pm->pm_linesize,
	      pm->pm_width, pm->pm_height, repeat);
    break;

  default:
    return 1;
  }
  return 0;
}

/**
 *
 */
void
glw_tex_upload(glw_root_t *gr, glw_backend_texture_t *tex, 
	       const void *src, int fmt, int width, int height, int flags)
{
  switch(fmt) {
  case GLW_TEXTURE_FORMAT_I8A8:
    init_i8a8(gr, tex, src, width * 2, width, height, flags & GLW_TEX_REPEAT);
    break;

  case GLW_TEXTURE_FORMAT_RGB:
    init_rgb(gr, tex, src, width * 3, width, height, flags & GLW_TEX_REPEAT);
    break;
#if 0
  case GLW_TEXTURE_FORMAT_RGBA:
    init_rgba(gr, tex, src, width * 4, width, height, flags & GLW_TEX_REPEAT);
    break;
#endif
  case GLW_TEXTURE_FORMAT_BGR32:
    init_abgr(gr, tex, src, width * 4, width, height, flags & GLW_TEX_REPEAT);
    break;

  default:
    TRACE(TRACE_ERROR, "GLW", "Unable to upload texture fmt %d, %d x %d",
	  fmt, width, height);
    return;
  }
}


/**
 *
 */
void
glw_tex_destroy(glw_root_t *gr, glw_backend_texture_t *tex)
{
  if(tex->size != 0) {
    rsx_free(gr, tex->tex.offset, tex->size);
    tex->size = 0;
  }
}
