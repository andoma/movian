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

  int16_t gd_border_left;
  int16_t gd_border_right;
  int16_t gd_border_top;
  int16_t gd_border_bottom;

  float gd_border_xs;
  float gd_border_ys;
  float gd_border_xt;
  float gd_border_yt;

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
  float bl, bt, br, bb;
  float cvex[2][2];

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = extra;

    bl = gd->gd_border_left;
    bt = gd->gd_border_top;
    br = gd->gd_border_right;
    bb = gd->gd_border_bottom;

    cvex[0][0] = GLW_MIN(-1.0 + 2.0 * bl / rc->rc_size_x, 0.0);
    cvex[1][0] = GLW_MAX( 1.0 - 2.0 * br / rc->rc_size_x, 0.0);
    cvex[0][1] = GLW_MAX( 1.0 - 2.0 * bt / rc->rc_size_y, 0.0);
    cvex[1][1] = GLW_MIN(-1.0 + 2.0 * bb / rc->rc_size_y, 0.0);
    
    gd->gd_border_xt = (cvex[1][0] + cvex[0][0]) * 0.5f;
    gd->gd_border_yt = (cvex[0][1] + cvex[1][1]) * 0.5f;
    gd->gd_border_xs = (cvex[1][0] - cvex[0][0]) * 0.5f;
    gd->gd_border_ys = (cvex[0][1] - cvex[1][1]) * 0.5f;

    if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
      break;
    
    rc0 = *rc;
    rc0.rc_size_x = rc->rc_size_x * gd->gd_border_xs;
    rc0.rc_size_y = rc->rc_size_y * gd->gd_border_ys;
    
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
glw_displacement_render(glw_t *w, glw_rctx_t *rc)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_t *c;
  glw_rctx_t rc0 = *rc;
  float xs, ys;

  if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
    return;
   
  glw_PushMatrix(&rc0, rc);

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

  glw_Translatef(&rc0, gd->gd_border_xt, gd->gd_border_yt, 0.0f);
  xs = gd->gd_border_xs;
  ys = gd->gd_border_ys;

  glw_Scalef(&rc0, xs, ys, 1.0f);
  rc0.rc_size_x = rc->rc_size_x * xs;
  rc0.rc_size_y = rc->rc_size_y * ys;

  rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
  glw_render0(c, &rc0);
  glw_PopMatrix();
}

/**
 *
 */
static void 
glw_displacement_set(glw_t *w, int init, va_list ap)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_attribute_t attrib;

  if(init) {
    gd->gd_scale_x = 1;
    gd->gd_scale_y = 1;
    gd->gd_scale_z = 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
 
   case GLW_ATTRIB_BORDER:
      gd->gd_border_left   = va_arg(ap, double);
      gd->gd_border_top    = va_arg(ap, double);
      gd->gd_border_right  = va_arg(ap, double);
      gd->gd_border_bottom = va_arg(ap, double);
      break;

   case GLW_ATTRIB_ROTATION:
     gd->gd_rotate_a = va_arg(ap, double);
     gd->gd_rotate_x = va_arg(ap, double);
     gd->gd_rotate_y = va_arg(ap, double);
     gd->gd_rotate_z = va_arg(ap, double);
     break;

   case GLW_ATTRIB_TRANSLATION:
     gd->gd_translate_x = va_arg(ap, double);
     gd->gd_translate_y = va_arg(ap, double);
     gd->gd_translate_z = va_arg(ap, double);
     break;

   case GLW_ATTRIB_SCALING:
     gd->gd_scale_x = va_arg(ap, double);
     gd->gd_scale_y = va_arg(ap, double);
     gd->gd_scale_z = va_arg(ap, double);
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
static glw_class_t glw_displacement = {
  .gc_name = "displacement",
  .gc_instance_size = sizeof(glw_displacement_t),
  .gc_set = glw_displacement_set,
  .gc_render = glw_displacement_render,
  .gc_signal_handler = glw_displacement_callback,
};

GLW_REGISTER_CLASS(glw_displacement);
