/*
 *  GL Widgets, Displacement widget
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

/**
 *
 */
typedef struct {
  glw_t w;

  float gd_padding_left;
  float gd_padding_right;
  float gd_padding_top;
  float gd_padding_bottom;

  float gd_scale_x;
  float gd_scale_y;
  float gd_scale_z;

  float gd_translate_x;
  float gd_translate_y;
  float gd_translate_z;
  
  float gd_rotate_a;
  float gd_rotate_x;
  float gd_rotate_y;
  float gd_rotate_z;

} glw_displacement_t;



/**
 *
 */
static int
glw_displacement_callback(glw_t *w, void *opaque, 
			  glw_signal_t signal, void *extra)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_t *c;
  glw_rctx_t *rc, rc0;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
      break;

    rc = extra;
    int width  = rc->rc_width - gd->gd_padding_left - gd->gd_padding_right;
    int height = rc->rc_height - gd->gd_padding_top - gd->gd_padding_bottom;

    rc0 = *rc;
    rc0.rc_width  = width;
    rc0.rc_height = height;
    
    glw_layout0(c, &rc0);
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
  if(rc0.rc_alpha < 0.01)
    return;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;
   
  glw_Translatef(&rc0,
		 gd->gd_translate_x,
		 gd->gd_translate_y,
		 gd->gd_translate_z);

  glw_Scalef(&rc0, 
	     gd->gd_scale_x,
	     gd->gd_scale_y,
	     gd->gd_scale_z);

  if(gd->gd_rotate_a)
    glw_Rotatef(&rc0, 
		gd->gd_rotate_a,
		gd->gd_rotate_x,
		gd->gd_rotate_y,
		gd->gd_rotate_z);

  glw_repositionf(&rc0,
		  gd->gd_padding_left,
		  rc->rc_height - gd->gd_padding_top,
		  rc->rc_width  - gd->gd_padding_right,
		  gd->gd_padding_bottom);

  if(glw_is_focusable(w))
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
  gd->gd_scale_x = 1;
  gd->gd_scale_y = 1;
  gd->gd_scale_z = 1;
}



/**
 *
 */
static void
set_translation(glw_t *w, const float *xyz)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  gd->gd_translate_x = xyz[0];
  gd->gd_translate_y = xyz[1];
  gd->gd_translate_z = xyz[2];
}


/**
 *
 */
static void
set_scaling(glw_t *w, const float *xyz)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;

  gd->gd_scale_x = xyz[0];
  gd->gd_scale_y = xyz[1];
  gd->gd_scale_z = xyz[2];
}


/**
 *
 */
static void
set_padding(glw_t *w, const int16_t *v)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  
  gd->gd_padding_left   = v[0];
  gd->gd_padding_top    = v[1];
  gd->gd_padding_right  = v[2];
  gd->gd_padding_bottom = v[3];
}


/**
 *
 */
static void
set_rotation(glw_t *w, const float *v)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  
  gd->gd_rotate_a = v[0];
  gd->gd_rotate_x = v[1];
  gd->gd_rotate_y = v[2];
  gd->gd_rotate_z = v[3];
}


/**
 *
 */
static glw_class_t glw_displacement = {
  .gc_name = "displacement",
  .gc_instance_size = sizeof(glw_displacement_t),
  .gc_ctor = glw_displacement_ctor,
  .gc_render = glw_displacement_render,
  .gc_signal_handler = glw_displacement_callback,
  .gc_set_translation = set_translation,
  .gc_set_scaling = set_scaling,
  .gc_set_padding = set_padding,
  .gc_set_rotation = set_rotation,
};

GLW_REGISTER_CLASS(glw_displacement);
