/*
 *  Based on rc_zoom, display a proportial amount of second child.
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

#include "glw.h"
#include "glw_expander.h"

/*
 *
 */
static int
glw_expander_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *a, *b;
  glw_rctx_t *rc = extra, rc0;
  float z;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, w->glw_exp_req);

    if((a = TAILQ_FIRST(&w->glw_childs)) == NULL) {
      break;
    }

    if((b = TAILQ_NEXT(a, glw_parent_link)) == NULL) {
      a->glw_parent_pos.y = 0;
      a->glw_parent_scale.x = 1.0f;
      a->glw_parent_scale.y = 1.0f;
      a->glw_parent_scale.z = 1.0f;
      glw_layout0(a, rc);
      break;
    }

    rc0 = *rc;
    z = rc->rc_zoom;

    a->glw_parent_scale.x = 1.0f;
    a->glw_parent_scale.y = 1.0 / (z + 1);
    a->glw_parent_pos.y   = 1.0f - a->glw_parent_scale.y;
    rc0.rc_size_y = rc->rc_size_y * a->glw_parent_scale.y;

    glw_layout0(a, &rc0);
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, rc0.rc_exp_req);
    
    //    if(rc->rc_zoom < 0.01)
    //      break;

    b->glw_parent_pos.y   = -1.0 + (0.5 * z * 2 / (z + 1));
    b->glw_parent_scale.x = 1.0f;
    b->glw_parent_scale.y = 1 - a->glw_parent_scale.y;

    rc0.rc_size_y = rc->rc_size_y * b->glw_parent_scale.y;
    glw_layout0(b, &rc0);
    rc->rc_exp_req = GLW_MAX(rc->rc_exp_req, rc0.rc_exp_req);
    break;
    
  case GLW_SIGNAL_RENDER:

    if((a = TAILQ_FIRST(&w->glw_childs)) == NULL)
      break;
    if((b = TAILQ_NEXT(a, glw_parent_link)) == NULL) {
      glw_render0(a, rc);
      break;
    }
    
    rc0 = *rc;
    glw_render_TS(a, &rc0, rc);
    
    if(rc->rc_zoom < 0.01)
      break;

    rc0.rc_alpha = rc->rc_alpha * GLW_MIN(1.0f, rc->rc_zoom);
    glw_render_TS(b, &rc0, rc);
    break;
  }
  return 0;
}


void 
glw_expander_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_expander_callback);
}
