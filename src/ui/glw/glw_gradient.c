/*
 *  GL Widgets, GLW_GRADIENT widget
 *  Copyright (C) 2010 Andreas Öman
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

#include <libavutil/common.h>

#include "glw.h"
#include "glw_renderer.h"
#include "glw_texture.h"

typedef struct glw_gradient {
  glw_t w;

  float gg_col1[3];
  float gg_col2[3];

  char gg_repaint;

  char gg_gr_initialized;
  char gg_tex_uploaded;

  glw_renderer_t gg_gr[3];
  glw_backend_texture_t gg_tex[3];

  int16_t gg_width;
  int16_t gg_height;
  int16_t gg_height2;
  int16_t gg_tiles;
  int gg_image_flags;
  float gg_alpha_self;
} glw_gradient_t;



static void
glw_gradient_dtor(glw_t *w)
{
  glw_gradient_t *gg = (void *)w;
  int i;

  for(i = 0; i < gg->gg_tex_uploaded; i++)
    glw_tex_destroy(w->glw_root, &gg->gg_tex[i]);

  glw_renderer_free(&gg->gg_gr[i]);
}


/**
 *
 */
static void 
glw_gradient_render(glw_t *w, glw_rctx_t *rc)
{
  glw_gradient_t *gg = (void *)w;
  float a = rc->rc_alpha * w->glw_alpha * gg->gg_alpha_self;

  if(gg->gg_col1[0] < 0.001 &&
     gg->gg_col1[1] < 0.001 &&
     gg->gg_col1[2] < 0.001 &&
     gg->gg_col2[0] < 0.001 &&
     gg->gg_col2[1] < 0.001 &&
     gg->gg_col2[2] < 0.001) {
    return;
  }

  if(a > 0.01) {
    int i;
    for(i = 0; i < gg->gg_gr_initialized ; i++) {
      glw_renderer_draw(&gg->gg_gr[i], w->glw_root, rc, 
			&gg->gg_tex[i], NULL, NULL, a, 0);
    }
  }
}

#define GRAD_BPP 3

#define BEVEL_STRENGTH 32

/**
 *
 */
static void
bevel_top(uint8_t *p0, int w, int h)
{
  int x;
  uint8_t *p = p0;

  for(x = 0; x < w; x++) {
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;
  }
}


/**
 *
 */
static void
bevel_bottom(uint8_t *p0, int w, int h)
{
  int x;
  uint8_t *p = p0 + w * GRAD_BPP * (h - 1);

  for(x = 0; x < w; x++) {
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
  }
}


/**
 *
 */
static void
bevel_left(uint8_t *p0, int w, int h)
{
  uint8_t *p = p0;
  int y;
  for(y = 0; y < h; y++) {
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;
    *p = GLW_MIN((int)*p + BEVEL_STRENGTH, 255);  p++;

    p+= (w - 1) * GRAD_BPP;
  }
}


/**
 *
 */
static void
bevel_right(uint8_t *p0, int w, int h)
{
  uint8_t *p = p0;
  int y;
  for(y = 0; y < h; y++) {
    p+= (w - 1) * GRAD_BPP;
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
    *p = GLW_MAX((int)*p - BEVEL_STRENGTH, 0);  p++;
  }
}


/**
 *
 */
static int
repaint(glw_gradient_t *gg, glw_root_t *gr, int tile, int w, int h, int tiles)
{
  int x, y, n = showtime_get_ts(), m = 0;
  uint8_t  *p, *pixmap;
  size_t s = h * w * GRAD_BPP;

  if(w < 1 || h < 1)
    return 0;

  p = pixmap = malloc(s);
  for(y = 0; y < h; y++) {
    float a = (float)y / (float)h;
    int r = 65280 * GLW_LERP(a, gg->gg_col1[0], gg->gg_col2[0]);
    int g = 65280 * GLW_LERP(a, gg->gg_col1[1], gg->gg_col2[1]);
    int b = 65280 * GLW_LERP(a, gg->gg_col1[2], gg->gg_col2[2]);

    for(x = 0; x < w; x++) {
      n = n * 1664525 + 1013904223;
      *p++ = (b + (n & 0xff)) >> 8;

      n = n * 1664525 + 1013904223;
      *p++ = (g + (n & 0xff)) >> 8;

      n = n * 1664525 + 1013904223;
      *p++ = (r + (n & 0xff)) >> 8;
    }
    n = n ^ m;
    m = m * 1664525 + 1013904223;
  }

  if(gg->gg_image_flags & GLW_IMAGE_BEVEL_TOP)
    bevel_top(pixmap, w, h);

  if(gg->gg_image_flags & GLW_IMAGE_BEVEL_BOTTOM)
    bevel_bottom(pixmap, w, h);

  glw_tex_upload(gr, &gg->gg_tex[0], pixmap, GLW_TEXTURE_FORMAT_RGB, w, h, 
		 GLW_TEX_REPEAT);

  if(tiles == 3) {
    p = malloc(s);

    memcpy(p, pixmap, s);
    if(gg->gg_image_flags & GLW_IMAGE_BEVEL_LEFT)
      bevel_left(p, w, h);
    glw_tex_upload(gr, &gg->gg_tex[1], p, GLW_TEXTURE_FORMAT_RGB, w, h, 
		   GLW_TEX_REPEAT);

    memcpy(p, pixmap, s);
    if(gg->gg_image_flags & GLW_IMAGE_BEVEL_RIGHT)
      bevel_right(p, w, h);
    glw_tex_upload(gr, &gg->gg_tex[2], p, GLW_TEXTURE_FORMAT_RGB, w, h, 
		   GLW_TEX_REPEAT);

    free(p);
  }
  free(pixmap);
  return 1;
}

#define TILEWIDTH 16

/**
 *
 */
static void 
glw_gradient_layout(glw_t *W, glw_rctx_t *rc)
{
  glw_gradient_t *gg = (void *)W;
  glw_root_t *gr = W->glw_root;
  int w, h, i, tiles;

  w = rc->rc_width;
  h = rc->rc_height;
  tiles = gg->gg_image_flags & (GLW_IMAGE_BEVEL_LEFT | GLW_IMAGE_BEVEL_RIGHT)
    ? 3 : 1;

  for(i = gg->gg_tiles; i < tiles; i++) {
    glw_renderer_init_quad(&gg->gg_gr[i]);
    gg->gg_gr_initialized = tiles;
  }

  if(gg->gg_width != w || gg->gg_height != h || gg->gg_tiles != tiles) {
    gg->gg_width = w;
    gg->gg_height = h;
    gg->gg_tiles = tiles;

    gg->gg_height2 = glw_can_tnpo2(gr) ? h : 1 << (av_log2(h));

    float xs = gr->gr_normalized_texture_coords ? 1.0 : TILEWIDTH;
    float ys = gr->gr_normalized_texture_coords ? 1.0 : gg->gg_height2;
    glw_renderer_t *r;


    if(tiles == 1) {

      r = &gg->gg_gr[0];
  
      float u = xs * rc->rc_width / TILEWIDTH;

      glw_renderer_vtx_pos(r, 0, -1.0, -1.0, 0.0);
      glw_renderer_vtx_st (r, 0,  0.0,  ys);

      glw_renderer_vtx_pos(r, 1,  1.0, -1.0, 0.0);
      glw_renderer_vtx_st (r, 1,  u,    ys);

      glw_renderer_vtx_pos(r, 2,  1.0,  1.0, 0.0);
      glw_renderer_vtx_st (r, 2,  u,    0.0);

      glw_renderer_vtx_pos(r, 3, -1.0,  1.0, 0.0);
      glw_renderer_vtx_st (r, 3,  0.0,  0.0);

    } else {

      r = &gg->gg_gr[0];

      float u = xs * (rc->rc_width - TILEWIDTH * 2) / TILEWIDTH;

      float x1 = -1.0 + 2.0 * TILEWIDTH / gg->gg_width;
      float x2 = -1.0 + 2.0 * (gg->gg_width - TILEWIDTH) / gg->gg_width;
      

      glw_renderer_vtx_pos(r, 0,  x1, -1.0, 0.0);
      glw_renderer_vtx_st (r, 0,  0.0,  ys);

      glw_renderer_vtx_pos(r, 1,  x2, -1.0, 0.0);
      glw_renderer_vtx_st (r, 1,  u,    ys);

      glw_renderer_vtx_pos(r, 2,  x2,  1.0, 0.0);
      glw_renderer_vtx_st (r, 2,  u,    0.0);

      glw_renderer_vtx_pos(r, 3,  x1, 1.0, 0.0);
      glw_renderer_vtx_st (r, 3,  0.0,  0.0);


      r = &gg->gg_gr[1];
      glw_renderer_vtx_pos(r, 0, -1.0, -1.0, 0.0);
      glw_renderer_vtx_st (r, 0,  0.0,  ys);

      glw_renderer_vtx_pos(r, 1,  x1, -1.0, 0.0);
      glw_renderer_vtx_st (r, 1,  xs,    ys);

      glw_renderer_vtx_pos(r, 2,  x1,  1.0, 0.0);
      glw_renderer_vtx_st (r, 2,  xs,    0.0);

      glw_renderer_vtx_pos(r, 3, -1.0,  1.0, 0.0);
      glw_renderer_vtx_st (r, 3,  0.0,  0.0);

      r = &gg->gg_gr[2];
      glw_renderer_vtx_pos(r, 0, x2, -1.0, 0.0);
      glw_renderer_vtx_st (r, 0,  0.0,  ys);

      glw_renderer_vtx_pos(r, 1,  1.0, -1.0, 0.0);
      glw_renderer_vtx_st (r, 1,  xs,    ys);

      glw_renderer_vtx_pos(r, 2,  1.0,  1.0, 0.0);
      glw_renderer_vtx_st (r, 2,  xs,    0.0);

      glw_renderer_vtx_pos(r, 3, x2,  1.0, 0.0);
      glw_renderer_vtx_st (r, 3,  0.0,  0.0);
    }

    gg->gg_repaint = 1;
  }

  if(gg->gg_repaint) {
    gg->gg_repaint = 0;

    repaint(gg, gr, i, TILEWIDTH, gg->gg_height2, gg->gg_tiles);
  }

}


/**
 *
 */
static int
glw_gradient_callback(glw_t *w, void *opaque, glw_signal_t signal,
		      void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_gradient_layout(w, extra);
    break;
  }
  return 0;
}

/**
 *
 */
static void 
glw_gradient_ctor(glw_t *w)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  gg->gg_height = -1;
  gg->gg_width = -1;
  gg->gg_alpha_self = 1;
}


/**
 *
 */
static void
mod_image_flags(glw_t *w, int set, int clr)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  gg->gg_image_flags = (gg->gg_image_flags | set) & ~clr;
  gg->gg_repaint = 1;
}



/**
 *
 */
static void
set_color1(glw_t *w, const float *rgb)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  gg->gg_col1[0] = GLW_CLAMP(rgb[0], 0, 1);
  gg->gg_col1[1] = GLW_CLAMP(rgb[1], 0, 1);
  gg->gg_col1[2] = GLW_CLAMP(rgb[2], 0, 1);
  gg->gg_repaint = 1;
}


/**
 *
 */
static void
set_color2(glw_t *w, const float *rgb)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  gg->gg_col2[0] = GLW_CLAMP(rgb[0], 0, 1);
  gg->gg_col2[1] = GLW_CLAMP(rgb[1], 0, 1);
  gg->gg_col2[2] = GLW_CLAMP(rgb[2], 0, 1);
  gg->gg_repaint = 1;
}


/**
 *
 */
static void
set_alpha_self(glw_t *w, float f)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  gg->gg_alpha_self = f;
}



/**
 *
 */
static glw_class_t glw_gradient = {
  .gc_name = "gradient",
  .gc_flags = GLW_UNCONSTRAINED,
  .gc_instance_size = sizeof(glw_gradient_t),
  .gc_render = glw_gradient_render,
  .gc_ctor = glw_gradient_ctor,
  .gc_dtor = glw_gradient_dtor,
  .gc_signal_handler = glw_gradient_callback,
  .gc_set_color1 = set_color1,
  .gc_set_color2 = set_color2,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_alpha_self = set_alpha_self,
};

GLW_REGISTER_CLASS(glw_gradient);

