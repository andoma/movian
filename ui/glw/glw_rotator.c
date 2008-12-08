/*
 *  GL Widgets, Fixed speed rotating widget
 *  Copyright (C) 2007 Andreas Öman
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
#include "glw_rotator.h"


/*
 *
 */
static int
glw_rotator_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;
  glw_rctx_t *rc, rc0;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    rc = extra;
    w->glw_extra -= 5;
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL)
      glw_layout0(c, rc);
    break;

  case GLW_SIGNAL_RENDER:
    rc = extra;

    c = TAILQ_FIRST(&w->glw_childs);
    if(c == NULL)
      break;

    rc0 = *rc;
    rc0.rc_scale_x = 1.0f;
    rc0.rc_scale_y = 1.0f;

    glPushMatrix();
    glScalef(0.8, 0.8, 0.8);
    glw_rescale(rc->rc_scale_x / rc->rc_scale_y, 1.0f);
    glRotatef(w->glw_extra, 0.0, 0.0f, 1.0f);
    glw_render0(c, &rc0);
    glPopMatrix();
    break;
  }
  return 0;
}

void 
glw_rotator_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_rotator_callback);
}
