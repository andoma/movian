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
#include "glw_displacement.h"

/*
 *
 */
static int
glw_displacement_callback(glw_t *w, void *opaque, 
			  glw_signal_t signal, void *extra)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_t *c;
  glw_rctx_t *rc, rc0;
  float xs, ys;
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

  case GLW_SIGNAL_RENDER:
    rc = extra;

    if((c = TAILQ_FIRST(&w->glw_childs)) == NULL)
      break;

    rc0 = *rc;
    
    glw_PushMatrix(&rc0, rc);
      
    glw_Translatef(&rc0, gd->gd_border_xt, gd->gd_border_yt, 0.0f);
    xs = gd->gd_border_xs;
    ys = gd->gd_border_ys;

    glw_Scalef(&rc0, xs, ys, 1.0f);
    rc0.rc_size_x = rc->rc_size_x * xs;
    rc0.rc_size_y = rc->rc_size_y * ys;

    rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
    glw_render0(c, &rc0);
    glw_PopMatrix();
    break;

 case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    c = extra;
    glw_copy_constraints(w, c);
    return 1;

  }
  return 0;
}

void 
glw_displacement_ctor(glw_t *w, int init, va_list ap)
{
  glw_displacement_t *gd = (glw_displacement_t *)w;
  glw_attribute_t attrib;

  if(init)
    glw_signal_handler_int(w, glw_displacement_callback);

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
 
   case GLW_ATTRIB_BORDER:
      gd->gd_border_left   = va_arg(ap, double);
      gd->gd_border_top    = va_arg(ap, double);
      gd->gd_border_right  = va_arg(ap, double);
      gd->gd_border_bottom = va_arg(ap, double);
      //      glw_image_update_constraints(gi);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

}
