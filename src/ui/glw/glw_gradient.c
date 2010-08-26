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

#include "glw.h"
#include "glw_texture.h"

typedef struct glw_gradient {
  glw_t w;

  float gg_col1[3];
  float gg_col2[3];

  int gg_repaint;

  int gg_gr_initialized;
  glw_renderer_t gg_gr;

  int gg_tex_uploaded;
  glw_backend_texture_t gg_tex;

  int gg_width;
  int gg_height;

} glw_gradient_t;



static void
glw_gradient_dtor(glw_t *w)
{
  glw_gradient_t *gg = (void *)w;

  if(gg->gg_tex_uploaded)
    glw_tex_destroy(&gg->gg_tex);

  if(gg->gg_gr_initialized)
    glw_render_free(&gg->gg_gr);
}


/**
 *
 */
static void 
glw_gradient_render(glw_t *w, glw_rctx_t *rc)
{
  glw_gradient_t *gg = (void *)w;
  float a = rc->rc_alpha * w->glw_alpha;

  if(gg->gg_col1[0] < 0.001 &&
     gg->gg_col1[1] < 0.001 &&
     gg->gg_col1[2] < 0.001 &&
     gg->gg_col2[0] < 0.001 &&
     gg->gg_col2[1] < 0.001 &&
     gg->gg_col2[2] < 0.001) {
    return;
  }
  if(a > 0.01) {
    glw_render(&gg->gg_gr, w->glw_root, rc, 
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
	       &gg->gg_tex, 1, 1, 1, a);
  }
}


/**
 *
 */
static void
repaint(glw_gradient_t *gg, glw_root_t *gr)
{
  int h = gg->gg_height;
  int w = gg->gg_width;
  int x, y, n = showtime_get_ts(), m = 0;
  uint8_t  *p, *pixmap = malloc(h * w * 4 * sizeof(uint8_t));

  p = pixmap;
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
      *p++ = 255;
    }
    n = n ^ m;
    m = m * 1664525 + 1013904223;
  }

  glw_tex_upload(gr, &gg->gg_tex, pixmap, GLW_TEXTURE_FORMAT_RGBA, w, h, 
		 GLW_TEX_REPEAT);
  gg->gg_tex_uploaded = 1;
  free(pixmap);
}


/**
 *
 */
static void 
glw_gradient_layout(glw_t *W, glw_rctx_t *rc)
{
  glw_gradient_t *gg = (void *)W;
  glw_root_t *gr = W->glw_root;
  int w, h;

  if(!gg->gg_gr_initialized) {
    glw_render_init(&gg->gg_gr, 4, GLW_RENDER_ATTRIBS_TEX);
    gg->gg_gr_initialized = 1;
  }
  w = 16;
  h = rc->rc_size_y;

  if(gg->gg_width != w || gg->gg_height != h) {
    gg->gg_width = w;
    gg->gg_height = h;

    float xs = gr->gr_normalized_texture_coords ? 1.0 : gg->gg_width;
    float ys = gr->gr_normalized_texture_coords ? 1.0 : gg->gg_height;
  
    xs *= rc->rc_size_x / gg->gg_width;

    glw_render_vtx_pos(&gg->gg_gr, 0, -1.0, -1.0, 0.0);
    glw_render_vtx_st (&gg->gg_gr, 0,  0.0,  ys);

    glw_render_vtx_pos(&gg->gg_gr, 1,  1.0, -1.0, 0.0);
    glw_render_vtx_st (&gg->gg_gr, 1,  xs,   ys);

    glw_render_vtx_pos(&gg->gg_gr, 2,  1.0,  1.0, 0.0);
    glw_render_vtx_st (&gg->gg_gr, 2,  xs,   0.0);

    glw_render_vtx_pos(&gg->gg_gr, 3, -1.0,  1.0, 0.0);
    glw_render_vtx_st (&gg->gg_gr, 3,  0.0,  0.0);
    gg->gg_repaint = 1;
  }

  if(gg->gg_repaint) {
    gg->gg_repaint = 0;
    repaint(gg, gr);
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
glw_gradient_set(glw_t *w, int init, va_list ap)
{
  glw_gradient_t *gg = (glw_gradient_t *)w;
  glw_attribute_t attrib;

  if(init) {
    gg->gg_height = 1024;
    gg->gg_width = 8;
  }


  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_COLOR1:
      gg->gg_col1[0] = va_arg(ap, double);
      gg->gg_col1[1] = va_arg(ap, double);
      gg->gg_col1[2] = va_arg(ap, double);
      gg->gg_col1[0] = GLW_CLAMP(gg->gg_col1[0], 0, 1);
      gg->gg_col1[1] = GLW_CLAMP(gg->gg_col1[1], 0, 1);
      gg->gg_col1[2] = GLW_CLAMP(gg->gg_col1[2], 0, 1);
      gg->gg_repaint = 1;
      break;

    case GLW_ATTRIB_COLOR2:
      gg->gg_col2[0] = va_arg(ap, double);
      gg->gg_col2[1] = va_arg(ap, double);
      gg->gg_col2[2] = va_arg(ap, double);
      gg->gg_col2[0] = GLW_CLAMP(gg->gg_col2[0], 0, 1);
      gg->gg_col2[1] = GLW_CLAMP(gg->gg_col2[1], 0, 1);
      gg->gg_col2[2] = GLW_CLAMP(gg->gg_col2[2], 0, 1);
      gg->gg_repaint = 1;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);
}



/**
 *
 */
static glw_class_t glw_gradient = {
  .gc_name = "gradient",
  .gc_flags = GLW_UNCONSTRAINED,
  .gc_instance_size = sizeof(glw_gradient_t),
  .gc_render = glw_gradient_render,
  .gc_set = glw_gradient_set,
  .gc_dtor = glw_gradient_dtor,
  .gc_signal_handler = glw_gradient_callback,
};

GLW_REGISTER_CLASS(glw_gradient);

