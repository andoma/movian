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

/**
 *
 */
typedef struct {
  glw_t w;
  float gd_scale[3];
  float gd_translate[3];
  float gd_rotate[4];
  int16_t gd_padding[4];
} glw_displacement_t;


/**
 *
 */
static void
glw_displacement_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_rctx_t rc0;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;

  int width  = rc->rc_width  - gd->gd_padding[0] - gd->gd_padding[2];
  int height = rc->rc_height - gd->gd_padding[1] - gd->gd_padding[3];

  rc0 = *rc;
  rc0.rc_width  = width;
  rc0.rc_height = height;

  glw_layout0(c, &rc0);
}



/**
 *
 */
static int
glw_displacement_callback(glw_t *w, void *opaque, 
			  glw_signal_t signal, void *extra)
{
  switch(signal) {
  default:
    break;

 case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;
  }

  return 0;
}


/**
 *
 */
static void
glw_displacement_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_t *c;
  glw_rctx_t rc0 = *rc;

  rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
  if(rc0.rc_alpha < GLW_ALPHA_EPSILON)
    return;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;
   
  glw_Translatef(&rc0,
		 gd->gd_translate[0],
		 gd->gd_translate[1],
		 gd->gd_translate[2]);

  glw_Scalef(&rc0, 
	     gd->gd_scale[0],
	     gd->gd_scale[1],
	     gd->gd_scale[2]);

  if(gd->gd_rotate[0])
    glw_Rotatef(&rc0,
		gd->gd_rotate[0],
		gd->gd_rotate[1],
		gd->gd_rotate[2],
		gd->gd_rotate[3]);

  glw_repositionf(&rc0,
		  gd->gd_padding[0],
		  rc->rc_height - gd->gd_padding[1],
		  rc->rc_width  - gd->gd_padding[2],
		  gd->gd_padding[3]);

  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, &rc0);

  glw_render0(c, &rc0);
}


/**
 *
 */
static void
glw_displacement_ctor(glw_t *w)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  gd->gd_scale[0] = 1.0f;
  gd->gd_scale[1] = 1.0f;
  gd->gd_scale[2] = 1.0f;
}



/**
 *
 */
static int
set_float3(glw_t *w, glw_attribute_t attrib, const float *vector,
           glw_style_t *gs)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_TRANSLATION:
    return glw_attrib_set_float3(gd->gd_translate, vector);
  case GLW_ATTRIB_SCALING:
    return glw_attrib_set_float3(gd->gd_scale, vector);
  default:
    return -1;
  }
}



/**
 *
 */
static int
displacement_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                         glw_style_t *gs)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_PADDING:
    return glw_attrib_set_int16_4(gd->gd_padding, v);
  default:
    return -1;
  }
}


/**
 *
 */
static int
set_float4(glw_t *w, glw_attribute_t attrib, const float *vector)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_ROTATION:
    return glw_attrib_set_float4(gd->gd_rotate, vector);
  default:
    return -1;
  }
}



/**
 *
 */
static glw_class_t glw_displacement = {
  .gc_name = "displacement",
  .gc_instance_size = sizeof(glw_displacement_t),
  .gc_ctor = glw_displacement_ctor,
  .gc_layout = glw_displacement_layout,
  .gc_render = glw_displacement_render,
  .gc_signal_handler = glw_displacement_callback,
  .gc_set_float3 = set_float3,
  .gc_set_float4 = set_float4,
  .gc_set_int16_4 = displacement_set_int16_4,
};

GLW_REGISTER_CLASS(glw_displacement);
