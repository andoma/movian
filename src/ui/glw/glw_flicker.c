/*
 *  GL Widgets, GLW_FLICKER widget
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
#include "glw_renderer.h"

typedef struct glw_flicker {
  glw_t w;

  int gf_gr_initialized;
  glw_renderer_t gf_gr[2];

  int gf_phase;

} glw_flicker_t;



static void
glw_flicker_dtor(glw_t *w)
{
  glw_flicker_t *gf = (void *)w;

  glw_renderer_free(&gf->gf_gr[0]);
  glw_renderer_free(&gf->gf_gr[1]);
}


/**
 *
 */
static void 
glw_flicker_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_flicker_t *gf = (void *)w;
  float a = rc->rc_alpha * w->glw_alpha;
  float v;

  if(a > 0.01) {
    v = 1.0 * gf->gf_phase + 0.25;

    glw_rgb_t rgb = {v,v,v};
    glw_renderer_draw(&gf->gf_gr[0], w->glw_root, rc,
		      NULL, &rgb, NULL, a, 0, NULL);

    v = 1.0 * !gf->gf_phase + 0.25;
    glw_rgb_t rgb_ = {v,v,v};
    glw_renderer_draw(&gf->gf_gr[1], w->glw_root, rc,
		      NULL, &rgb_, NULL, a, 0, NULL);
  }
}


/**
 *
 */
static void 
glw_flicker_layout(glw_t *W, glw_rctx_t *rc)
{
  glw_flicker_t *gf = (void *)W;
  int i;

  if(!gf->gf_gr_initialized) {
    glw_renderer_init_quad(&gf->gf_gr[0]);
    glw_renderer_init_quad(&gf->gf_gr[1]);

    for(i = 0; i < 2; i++) {
      glw_renderer_vtx_pos(&gf->gf_gr[i], 0, i+-1.0, -1.0, 0.0);
      glw_renderer_vtx_pos(&gf->gf_gr[i], 1, i+ 0.0, -1.0, 0.0);
      glw_renderer_vtx_pos(&gf->gf_gr[i], 2, i+ 0.0,  1.0, 0.0);
      glw_renderer_vtx_pos(&gf->gf_gr[i], 3, i+-1.0,  1.0, 0.0);
    }
  }
  gf->gf_phase = !gf->gf_phase;
}


/**
 *
 */
static int
glw_flicker_callback(glw_t *w, void *opaque, glw_signal_t signal,
		      void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_flicker_layout(w, extra);
    break;
  }
  return 0;
}


/**
 *
 */
static glw_class_t glw_flicker = {
  .gc_name = "flicker",
  .gc_instance_size = sizeof(glw_flicker_t),
  .gc_render = glw_flicker_render,
  .gc_dtor = glw_flicker_dtor,
  .gc_signal_handler = glw_flicker_callback,
};

GLW_REGISTER_CLASS(glw_flicker);

