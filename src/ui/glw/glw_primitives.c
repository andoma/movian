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
#include "glw_texture.h"

typedef struct glw_quad {
  glw_t w;

  glw_rgb_t color;
  glw_renderer_t r;
  rstr_t *fs;
  int16_t q_padding[4];
  int16_t border[4];
  glw_program_args_t gpa;
  int16_t width;
  int16_t height;
  char recompile;
  char relayout;
} glw_quad_t;




/**
 *
 */
static void
glw_quad_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_quad_t *q = (glw_quad_t *)w;

  if(q->recompile) {
    glw_destroy_program(w->glw_root, q->gpa.gpa_prog);
    q->gpa.gpa_prog = glw_make_program(w->glw_root, NULL, rstr_get(q->fs));
    q->recompile = 0;
  }

  glw_rctx_t rc0 = *rc;
  glw_reposition(&rc0, q->q_padding[0], rc->rc_height - q->q_padding[1],
		 rc->rc_width - q->q_padding[2], q->q_padding[3]);

  glw_renderer_draw(&q->r, w->glw_root, &rc0,
		    NULL, NULL,
		    &q->color, NULL, rc->rc_alpha * w->glw_alpha, 0,
		    q->gpa.gpa_prog ? &q->gpa : NULL);
}


/**
 *
 */
static void
glw_quad_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_quad_t *q = (glw_quad_t *)w;

  if(!glw_renderer_initialized(&q->r)) {
    glw_renderer_init_quad(&q->r);
    glw_renderer_vtx_pos(&q->r, 0, -1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 1,  1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 2,  1,  1, 0);
    glw_renderer_vtx_pos(&q->r, 3, -1,  1, 0);

    glw_renderer_vtx_st(&q->r, 0,  0,  1);
    glw_renderer_vtx_st(&q->r, 1,  1,  1);
    glw_renderer_vtx_st(&q->r, 2,  1,  0);
    glw_renderer_vtx_st(&q->r, 3,  0,  0);
  }

}


static uint16_t borderobject[] = {
  4, 1, 0,
  4, 5, 1,
  5, 2, 1,
  5, 6, 2,
  6, 7, 2,
  2, 7, 3,
  8, 5, 4,
  8, 9, 5,
  10, 7, 6,
  10, 11, 7,
  12, 13, 8,
  8, 13, 9,
  13, 10, 9,
  13, 14, 10,
  14, 11, 10,
  14, 15, 11,
};

/**
 *
 */
static void
glw_border_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_quad_t *q = (glw_quad_t *)w;

  if(!glw_renderer_initialized(&q->r)) {
    glw_renderer_init(&q->r, 16, 16, borderobject);
  } else if(q->width == rc->rc_width && q->height == rc->rc_height
            && !q->relayout) {
    return;
  }

  q->width = rc->rc_width;
  q->height = rc->rc_height;
  q->relayout = 0;

  float v[4][2];

  v[0][0] = -1.0f;
  v[1][0] = GLW_MIN(-1.0f + 2.0f * q->border[0] / rc->rc_width, 0.0f);
  v[2][0] = GLW_MAX( 1.0f - 2.0f * q->border[2] / rc->rc_width, 0.0f);
  v[3][0] = 1.0f;

  v[0][1] = 1.0f;
  v[1][1] = GLW_MAX( 1.0f - 2.0f * q->border[1] / rc->rc_height, 0.0f);
  v[2][1] = GLW_MIN(-1.0f + 2.0f * q->border[3] / rc->rc_height, 0.0f);
  v[3][1] = -1.0f;

  int i = 0;
  for(int y = 0; y < 4; y++) {
    for(int x = 0; x < 4; x++) {
      glw_renderer_vtx_pos(&q->r, i, v[x][0], v[y][1], 0.0f);
      i++;
    }
  }
}



/**
 *
 */
static void
glw_quad_init(glw_t *w)
{
  glw_quad_t *q = (void *)w;
  q->color.r = 1.0;
  q->color.g = 1.0;
  q->color.b = 1.0;
}


/**
 *
 */
static int
glw_quad_set_float3(glw_t *w, glw_attribute_t attrib, const float *vector,
                    glw_style_t *gs)
{
  glw_quad_t *q = (glw_quad_t *)w;
  switch(attrib) {
  case GLW_ATTRIB_RGB:
    return glw_attrib_set_rgb(&q->color, vector);
  default:
    return -1;
  }
}


/**
 *
 */
static void
glw_quad_set_fs(glw_t *w, rstr_t *vs)
{
  glw_quad_t *q = (glw_quad_t *)w;
  rstr_set(&q->fs, vs);
  q->recompile = 1;

}


/**
 *
 */
static void
glw_quad_dtor(glw_t *w)
{
  glw_quad_t *q = (glw_quad_t *)w;
  glw_renderer_free(&q->r);
  rstr_release(q->fs);
  glw_destroy_program(w->glw_root, q->gpa.gpa_prog);
}

/**
 *
 */
static int
quad_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                      glw_style_t *gs)
{
  glw_quad_t *q = (glw_quad_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_BORDER:
    if(!glw_attrib_set_int16_4(q->border, v))
      return 0;
    q->relayout = 1;
    return 1;

  case GLW_ATTRIB_PADDING:
    return glw_attrib_set_int16_4(q->q_padding, v);
  default:
    return -1;
  }
}


static glw_class_t glw_quad = {
  .gc_name = "quad",
  .gc_instance_size = sizeof(glw_quad_t),
  .gc_ctor = glw_quad_init,
  .gc_layout = glw_quad_layout,
  .gc_render = glw_quad_render,
  .gc_set_float3 = glw_quad_set_float3,
  .gc_dtor = glw_quad_dtor,
  .gc_set_fs = glw_quad_set_fs,
  .gc_set_int16_4 = quad_set_int16_4,
};
GLW_REGISTER_CLASS(glw_quad);

static glw_class_t glw_border = {
  .gc_name = "border",
  .gc_instance_size = sizeof(glw_quad_t),
  .gc_ctor = glw_quad_init,
  .gc_layout = glw_border_layout,
  .gc_render = glw_quad_render,
  .gc_set_float3 = glw_quad_set_float3,
  .gc_dtor = glw_quad_dtor,
  .gc_set_fs = glw_quad_set_fs,
  .gc_set_int16_4 = quad_set_int16_4,
};
GLW_REGISTER_CLASS(glw_border);









/**
 *
 */
static void
glw_linebox_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_rctx_t rc0 = *rc;
  glw_reposition(&rc0, 1, rc->rc_height, rc->rc_width, 1);
  glw_wirebox(w->glw_root, &rc0);
}


/**
 *
 */
static void
glw_linebox_layout(glw_t *w, const glw_rctx_t *rc)
{
}



static glw_class_t glw_linebox = {
  .gc_name = "linebox",
  .gc_instance_size = sizeof(glw_t),
  .gc_layout = glw_linebox_layout,
  .gc_render = glw_linebox_render,
};



GLW_REGISTER_CLASS(glw_linebox);


