/*
 *  GL Widgets, GLW_MIRROR widget
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
#include "glw_mirror.h"

/*
 *
 */

const static GLdouble clip_bottom[4] = {0.0, 1.0, 0.0, 1.0};

static int
glw_mirror_callback(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  glw_t *c = TAILQ_FIRST(&w->glw_childs);
  glw_rctx_t rc0, *rc = extra;
  float a;

  if(c == NULL)
    return 0;

  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_LAYOUT:
    glw_layout0(c, extra);
    return 1;

  case GLW_SIGNAL_RENDER:

    glPushMatrix();
    glTranslatef(w->glw_displacement.x,
		 w->glw_displacement.y,
		 w->glw_displacement.z);

    if(rc->rc_fullscreen > 0.99) {
      glw_render0(c, extra);
      glPopMatrix();
      return 1;
    }

   /* Render bottom plate */

    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);

    a = (1 - rc->rc_fullscreen) * rc->rc_alpha;

    glColor3f(a, a, a);

    glVertex3f(-10.0f, -1.0f, 1.0f);
    glVertex3f( 10.0f, -1.0f, 1.0f);
    glColor3f(0, 0, 0.0);
    glVertex3f( 20.0f, -1.0f, -10.0f);
    glVertex3f(-20.0f, -1.0f, -10.0f);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);

    rc0 = *rc;

    glClipPlane(GL_CLIP_PLANE5, clip_bottom);
    glEnable(GL_CLIP_PLANE5);

    glw_render0(c, &rc0);

    glTranslatef(0, -1, 0);
    /* invert model matrix along XZ plane for mirror effect */
    glScalef(1.0f, -1.0f, 1.0f);

    glTranslatef(0, 1, 0);

    rc0 = *rc;
    rc0.rc_alpha *= 0.1;

    glClipPlane(GL_CLIP_PLANE5, clip_bottom);
    glEnable(GL_CLIP_PLANE5);

    glw_render0(c, &rc0);
    glPopMatrix();

    glDisable(GL_CLIP_PLANE5);
    return 1;

  case GLW_SIGNAL_EVENT:
    return glw_signal0(c, GLW_SIGNAL_EVENT, extra);
  }
  return 0;
}


void 
glw_mirror_ctor(glw_t *w, int init, va_list ap)
{
  if(init)
    glw_signal_handler_int(w, glw_mirror_callback);
}

