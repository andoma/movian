/*
 *  GL Widgets, Transitions effects
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

#include <GL/gl.h>

#include "glw.h"
#include "glw_i.h"
#include "glw_transitions.h"

/**
 * Blending
 */
static void
trans_blend(float b, float alpha, glw_rctx_t *rc)
{
  rc->rc_alpha = alpha * (1 - fabs(b));
}


/**
 * Flip along horizontal axis
 */
static void
trans_flip_horizontal(float b, float alpha, glw_rctx_t *rc)
{
  float v;

  if(b > 0) {
    v = 1 - b;
    glTranslatef(0, 0, -1);
    glRotatef((1 - v) * -90, 1, 0, 0);
    glTranslatef(0, 0,  1);
    rc->rc_alpha = alpha * v;
  } else {
    v = -b;
    glTranslatef(0, 0, -1);
    glRotatef(v * 90, 1, 0, 0);
    glTranslatef(0, 0,  1);
    rc->rc_alpha = alpha * (1 - v);
  }
}


/**
 * Flip along vertical axis
 */
static void
trans_flip_vertical(float b, float alpha, glw_rctx_t *rc)
{
  float v;

  if(b > 0) {
    v = 1 - b;
    glTranslatef(0, 0, -1);
    glRotatef((1 - v) * -90, 0, 1, 0);
    glTranslatef(0, 0,  1);
    rc->rc_alpha = alpha * v;
  } else {
    v = -b;
    glTranslatef(0, 0, -1);
    glRotatef(v * 90, 0, 1, 0);
    glTranslatef(0, 0,  1);
    rc->rc_alpha = alpha * (1 - v);
  }
}


/**
 *
 */
static void
trans_slide_horizontal(float b, float alpha, glw_rctx_t *rc)
{
  rc->rc_alpha = alpha * GLW_S(1 - fabs(b));
  glTranslatef(-2 * b, 0, 0);
}


/**
 *
 */
static void
trans_slide_vertical(float b, float alpha, glw_rctx_t *rc)
{
  rc->rc_alpha = alpha * GLW_S(1 - fabs(b));
  glTranslatef(0, 2 * b, 0);
}


/**
 *
 */
const static struct {
  void (*r)(float b, float alpha, glw_rctx_t *rc);
} glw_transition_effects[] = {
  [GLW_TRANS_BLEND]            = { .r = trans_blend            },
  [GLW_TRANS_FLIP_HORIZONTAL]  = { .r = trans_flip_horizontal  },
  [GLW_TRANS_FLIP_VERTICAL]    = { .r = trans_flip_vertical    },
  [GLW_TRANS_SLIDE_HORIZONTAL] = { .r = trans_slide_horizontal },
  [GLW_TRANS_SLIDE_VERTICAL]   = { .r = trans_slide_vertical   },
};


/**
 *
 */
void
glw_transition_render(glw_transition_type_t t, 
		      float b, float alpha, glw_rctx_t *rc)
{
  return glw_transition_effects[t].r(b, alpha, rc);
}
