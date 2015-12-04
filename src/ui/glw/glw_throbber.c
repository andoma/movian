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
#include "glw.h"
#include "glw_renderer.h"

typedef struct glw_throbber3d {
  glw_t w;

  float angle;
  
  glw_renderer_t renderer;

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

static uint16_t surfaces[] = {
  0,1,5,  0,5,4,
  2,3,7,  2,7,6,

  8+3,8+0,8+4,8+3,8+4,8+7,
  8+1,8+2,8+6,8+1,8+6,8+5,
  8+3,8+2,8+1,8+3,8+1,8+0,
  8+4,8+5,8+6,8+4,8+6,8+7,
};

/**
 *
 */
static void
glw_throbber3d_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber3d_t *gt = (glw_throbber3d_t *)w;
  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  gt->angle += 2;
  if(gt->angle > 3600)
    gt->angle -= 3600;
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
glw_throbber3d_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber3d_t *gt = (glw_throbber3d_t *)w;
  glw_rctx_t rc0, rc1;
  int i;
  glw_root_t *gr = w->glw_root;
  float a0 = w->glw_alpha * rc->rc_alpha;
  if(a0 < GLW_ALPHA_EPSILON)
    return;

  if(!glw_renderer_initialized(&gt->renderer)) {
    glw_renderer_init(&gt->renderer, 16, 12, surfaces);

    for(i = 0; i < 16; i++) {
      glw_renderer_vtx_pos(&gt->renderer, i, pinvtx(i & 7));
      if(i < 8)
	glw_renderer_vtx_col(&gt->renderer, i, 1,1,1,1);
      else
	glw_renderer_vtx_col(&gt->renderer, i, 0.25, 0.25, 0.25, 1);
    }
  }

  rc0 = *rc;
  glw_scale_to_aspect(&rc0, 1.0);

  glw_blendmode(gr, GLW_BLEND_ADDITIVE);

#define NUMPINS 15

  for(i = 1; i < NUMPINS; i++) {
    
    float alpha = (1 - ((float)i / NUMPINS)) * rc->rc_alpha * w->glw_alpha;

    rc1 = rc0;
    glw_Rotatef(&rc1, 0.1 * gt->angle - i * ((360 / NUMPINS) / 3), 0, 1, 0);
    glw_Rotatef(&rc1,       gt->angle - i *  (360 / NUMPINS),      0, 0, 1);

    glw_renderer_draw(&gt->renderer, gr, &rc1, 
		      NULL, NULL, NULL, NULL, alpha, 0, NULL);
  }
  glw_blendmode(gr, GLW_BLEND_NORMAL);
}


static void
glw_throbber3d_dtor(glw_t *w)
{
  glw_throbber3d_t *gt = (glw_throbber3d_t *)w;
  glw_renderer_free(&gt->renderer);

}

/**
 *
 */
static glw_class_t glw_throbber3d = {
  .gc_name = "throbber3d",
  .gc_instance_size = sizeof(glw_throbber3d_t),
  .gc_render = glw_throbber3d_render,
  .gc_layout = glw_throbber3d_layout,
  .gc_dtor = glw_throbber3d_dtor,
};

GLW_REGISTER_CLASS(glw_throbber3d);





typedef struct glw_throbber {
  glw_t w;

  float angle;
  
  glw_renderer_t renderer;
  int o;
  glw_rgb_t color;
} glw_throbber_t;


/**
 *
 */
static void
glw_throbber_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber_t *gt = (glw_throbber_t *)w;
  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  glw_need_refresh(w->glw_root, 0);
  gt->angle += 0.5;
  if(gt->angle > 360)
    gt->angle -= 360;
  gt->o++;
}


/**
 *
 */
static void
glw_throbber_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber_t *gt = (glw_throbber_t *)w;
  glw_rctx_t rc0, rc1;
  int i;
  glw_root_t *gr = w->glw_root;
  float a0 = w->glw_alpha * rc->rc_alpha;
  int spokes = 16;

  if(a0 < GLW_ALPHA_EPSILON)
    return;

  if(!glw_renderer_initialized(&gt->renderer)) {
    glw_renderer_init_quad(&gt->renderer);

    glw_renderer_vtx_pos(&gt->renderer, 0, -0.05, 0.4, 0);
    glw_renderer_vtx_pos(&gt->renderer, 1,  0.05, 0.4, 0);
    glw_renderer_vtx_pos(&gt->renderer, 2,  0.05, 1, 0);
    glw_renderer_vtx_pos(&gt->renderer, 3, -0.05, 1, 0);
  }

  rc0 = *rc;
  glw_scale_to_aspect(&rc0, 1.0);


  for(i = 0; i < spokes; i++) {
    
    float a = i * 360.0 / 16;
    float alpha = 1 - (((i + (gt->o / 6)) % spokes) / 16.0);
    alpha = MAX(alpha, 0.1);

    rc1 = rc0;
    glw_Rotatef(&rc1, -gt->angle - a, 0, 0, -1);

    glw_renderer_draw(&gt->renderer, gr, &rc1, 
		      NULL, NULL,
		      &gt->color, NULL, a0 * alpha, 0, NULL);
  }
}


static void
glw_throbber_dtor(glw_t *w)
{
  glw_throbber_t *gt = (glw_throbber_t *)w;
  glw_renderer_free(&gt->renderer);

}




static void
glw_throbber_ctor(glw_t *w)
{
  glw_throbber_t *gt = (glw_throbber_t *)w;
  gt->color.r = 1.0;
  gt->color.g = 1.0;
  gt->color.b = 1.0;
}


/**
 *
 */
static int
glw_throbber_set_float3(glw_t *w, glw_attribute_t attrib, const float *rgb,
                        glw_style_t *origin)
{
  glw_throbber_t *gt = (glw_throbber_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_RGB:
    return glw_attrib_set_rgb(&gt->color, rgb);
  default:
    return -1;
  }
}


/**
 *
 */
static glw_class_t glw_throbber = {
  .gc_name = "throbber",
  .gc_instance_size = sizeof(glw_throbber_t),
  .gc_render = glw_throbber_render,
  .gc_layout = glw_throbber_layout,
  .gc_ctor = glw_throbber_ctor,
  .gc_dtor = glw_throbber_dtor,
  .gc_set_float3 = glw_throbber_set_float3,
};

GLW_REGISTER_CLASS(glw_throbber);





typedef struct glw_throbber_tri {
  glw_t w;

  float angle;

  glw_renderer_t renderer;
  int o;
} glw_throbber_tri_t;


/**
 *
 */
static void
glw_throbber_tri_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber_tri_t *gt = (glw_throbber_tri_t *)w;
  if(w->glw_alpha < GLW_ALPHA_EPSILON)
    return;

  glw_need_refresh(w->glw_root, 0);
  gt->angle += 0.8;
  if(gt->angle > 360)
    gt->angle -= 360;
  gt->o++;
}


/**
 *
 */
static void
glw_throbber_tri_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_throbber_tri_t *gt = (glw_throbber_tri_t *)w;
  glw_rctx_t rc0, rc1;
  glw_root_t *gr = w->glw_root;
  float a0 = w->glw_alpha * rc->rc_alpha;

  if(a0 < GLW_ALPHA_EPSILON)
    return;

  if(!glw_renderer_initialized(&gt->renderer)) {
    glw_renderer_init_triangle(&gt->renderer);

    glw_renderer_vtx_pos(&gt->renderer, 0,  -0.866, -1, 0);
    glw_renderer_vtx_col(&gt->renderer, 0,   0.20, 0.25, 0.81, 1);
    glw_renderer_vtx_pos(&gt->renderer, 1,   0.886,  0, 0);
    glw_renderer_vtx_col(&gt->renderer, 1,   0.19, 0.39, 0.90, 1);
    glw_renderer_vtx_pos(&gt->renderer, 2,  -0.866,  1, 0);
    glw_renderer_vtx_col(&gt->renderer, 2,   0.28, 0.58, 0.90, 1);
  }

  rc0 = *rc;
  glw_scale_to_aspect(&rc0, 1.0);

  glw_Rotatef(&rc0, gt->angle / 3, 0, 0, -1);

  rc1 = rc0;
  glw_Translatef(&rc1, -0.886/2, 0.5, 0);
  glw_Scalef(&rc1, 0.5, 0.5, 1.0);
  glw_Rotatef(&rc1, -gt->angle, 0, 0, -1);
  glw_renderer_draw(&gt->renderer, gr, &rc1,
                    NULL, NULL,
                    NULL, NULL, a0, 0, NULL);


  rc1 = rc0;
  glw_Translatef(&rc1, -0.886/2, -0.5, 0);
  glw_Scalef(&rc1, 0.5, 0.5, 1.0);
  glw_Rotatef(&rc1, -gt->angle, 0, 0, -1);
  glw_renderer_draw(&gt->renderer, gr, &rc1,
                    NULL, NULL,
                    NULL, NULL, a0, 0, NULL);

  rc1 = rc0;
  glw_Translatef(&rc1, 0.886/2, 0, 0);
  glw_Scalef(&rc1, 0.5, 0.5, 1.0);
  glw_Rotatef(&rc1, -gt->angle, 0, 0, -1);
  glw_renderer_draw(&gt->renderer, gr, &rc1,
                    NULL, NULL,
                    NULL, NULL, a0, 0, NULL);
}


static void
glw_throbber_tri_dtor(glw_t *w)
{
  glw_throbber_tri_t *gt = (glw_throbber_tri_t *)w;
  glw_renderer_free(&gt->renderer);

}



/**
 *
 */
static glw_class_t glw_throbber_tri = {
  .gc_name = "throbbertri",
  .gc_instance_size = sizeof(glw_throbber_tri_t),
  .gc_render = glw_throbber_tri_render,
  .gc_layout = glw_throbber_tri_layout,
  .gc_dtor = glw_throbber_tri_dtor,
};

GLW_REGISTER_CLASS(glw_throbber_tri);
