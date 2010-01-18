/*
 *  GL Widgets, 3D Throbber
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

typedef struct glw_throbber3d {
  glw_t w;

  float angle;
  
  glw_renderer_t renderer;
  int renderer_initialized;

} glw_throbber3d_t;



#define PINWIDTH  0.05
#define PINBOTTOM 0.2
#define PINTOP    1.0

static const struct {
  float x, y, z;
} pin[] = {
  {-PINWIDTH,  PINBOTTOM,   PINWIDTH},
  { PINWIDTH,  PINBOTTOM,   PINWIDTH},
  { PINWIDTH,  PINBOTTOM,  -PINWIDTH},
  {-PINWIDTH,  PINBOTTOM,  -PINWIDTH},

  {-PINWIDTH,  PINTOP,      PINWIDTH},
  { PINWIDTH,  PINTOP,      PINWIDTH},
  { PINWIDTH,  PINTOP,     -PINWIDTH},
  {-PINWIDTH,  PINTOP,     -PINWIDTH},
};

#define pinvtx(n) pin[n].x, pin[n].y, pin[n].z

static const char surfaces[24] = {
  0,1,5,4,
  2,3,7,6,
  3,0,4,7,
  1,2,6,5,
  3,2,1,0,
  4,5,6,7};

/**
 *
 */
static int
glw_throbber_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_throbber3d_t *gt = (glw_throbber3d_t *)w;

  if(signal == GLW_SIGNAL_LAYOUT)
    gt->angle += 2;

  return 0;
}

/**
 *
 */
static void
glw_throbber_render(glw_t *w, glw_rctx_t *rc)
{
  glw_throbber3d_t *gt = (glw_throbber3d_t *)w;
  glw_rctx_t rc0, rc1;
  int i;
  glw_root_t *gr = w->glw_root;
  float a0 = w->glw_alpha * rc->rc_alpha;
  if(a0 < 0.01)
    return;

  if(!gt->renderer_initialized) {
    glw_render_init(&gt->renderer, 24, GLW_RENDER_ATTRIBS_TEX_COLOR);

    for(i = 0; i < 24; i++) {
      glw_render_vtx_pos(&gt->renderer, i, pinvtx((int)surfaces[i]));
      if(i < 8)
	glw_render_vts_col(&gt->renderer, i, 1,1,1,1);
      else
	glw_render_vts_col(&gt->renderer, i, 0.25, 0.25, 0.25, 1);
    }
    gt->renderer_initialized = 1;
  }

  rc0 = *rc;
  glw_PushMatrix(&rc0, rc);
  glw_scale_to_aspect(&rc0, 1.0);

  glw_blendmode(GLW_BLEND_ADDITIVE);

#define NUMPINS 15

  for(i = 1; i < NUMPINS; i++) {
    
    float alpha = (1 - ((float)i / NUMPINS)) * rc->rc_alpha * w->glw_alpha;

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);

    glw_Rotatef(&rc1, 0.1 * gt->angle - i * ((360 / NUMPINS) / 3), 0, 1, 0);
    glw_Rotatef(&rc1,       gt->angle - i *  (360 / NUMPINS),      0, 0, 1);

    glw_render(&gt->renderer, gr, &rc1, 
	       GLW_RENDER_MODE_QUADS,
	       GLW_RENDER_ATTRIBS_COLOR,
	       NULL, 0, 0, 0, alpha);
    glw_PopMatrix();
  }

  glw_PopMatrix();
  glw_blendmode(GLW_BLEND_NORMAL);
}


/**
 *
 */
static glw_class_t glw_throbber3d = {
  .gc_name = "throbber3d",
  .gc_instance_size = sizeof(glw_throbber3d_t),
  .gc_render = glw_throbber_render,
  .gc_signal_handler = glw_throbber_callback,
};

GLW_REGISTER_CLASS(glw_throbber3d);
