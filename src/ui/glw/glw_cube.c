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

#include "glw.h"
#include "glw_cube.h"

/*
 *
 */
static int
glw_cube_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c;
  glw_rctx_t *rc, rc0, rc1;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    rc = extra;
    w->glw_extra -= 1;
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

    glw_PushMatrix(&rc0, rc);

    glw_rescale(&rc0, 1.0f);


    glw_Translatef(&rc0, 0, 0, -2.0);

    glw_Rotatef(&rc0, w->glw_extra, 1.1, 0.5f, 1.0f);

    rc1 = rc0;

    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 0, 0, 1, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 90, 0, 1, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 180, 0, 1, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 270, 0, 1, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 90, 1, 0, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    rc1 = rc0;
    glw_PushMatrix(&rc1, &rc0);
    glw_Rotatef(&rc1, 270, 1, 0, 0);
    glw_Translatef(&rc1, 0, 0, 1.0);
    glw_render0(c, &rc1);
    glw_PopMatrix();

    glw_PopMatrix();
    break;
  }
  return 0;
}

void 
glw_cube_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_cube_callback);
}
