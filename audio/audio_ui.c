/*
 *  Audio user interface
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "input.h"
#include "layout/layout.h"
#include "showtime.h"
#include "audio_sched.h"
#include "audio_ui.h"

static float audio_alpha;
static int show_audio;
static glw_t *audio_widget;

void
audio_render(glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  asched_t *as = &audio_scheduler;

  if(show_audio > 0)
    show_audio--;

  audio_alpha = (audio_alpha * 15 + 
		 (show_audio || as->as_mute ? 1 : 0)) / 16.0f;

  if(audio_alpha < 0.01 || audio_widget == NULL)
    return;

  rc0 = *rc;
  rc0.rc_alpha = audio_alpha;

  glDisable(GL_DEPTH_TEST);

  glPushMatrix();

  glTranslatef(0.5, 0.9, 0.0f);
  glScalef(0.2, 0.05, 1.0f);
  
  rc0.rc_aspect *= 4;

  glw_render(audio_widget, &rc0);

  glPopMatrix();

  glEnable(GL_DEPTH_TEST);

}


void
audio_ui_vol_changed(void)
{
  show_audio = 100;
}



/*****************************************************************************
 *
 * Master volume
 *
 */

static int 
miw_mastervol_callback(glw_t *w, glw_signal_t signal, ...)
{
  float x, a, vol;
  glw_rctx_t *rc;
  int mute;
  asched_t *as = glw_get_opaque(w);

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
    return 0;

  case GLW_SIGNAL_EXT_RENDER:
    vol = as->as_mastervol;
    mute = as->as_mute;

    rc = va_arg(ap, void *);
    a = 0.7 * rc->rc_alpha;

    x = (2.0 * vol) - 1.0f;

    glPushMatrix();

    glScalef(0.8, 0.8, 1.0f);

    glEnable(GL_BLEND);
    glBegin(GL_QUADS);

    if(mute)
      glColor4f(1.0f, 0.0f, 0.0f, a);
    else
      glColor4f(0.5f, 1.0f, 0.5f, a);

    glVertex3f(-1.0f, -1.0f, 0.0f);
    glVertex3f(    x, -1.0f, 0.0f);
    glVertex3f(    x,  1.0f, 0.0f);
    glVertex3f(-1.0f,  1.0f, 0.0f);

    if(mute)
      glColor4f(0.6f, 0.0f, 0.0f, a);
    else
      glColor4f(0.3f, 0.6f, 0.3f, a);

    glVertex3f(    x, -1.0f, 0.0f);
    glVertex3f( 1.0f, -1.0f, 0.0f);
    glVertex3f( 1.0f,  1.0f, 0.0f);
    glVertex3f(    x,  1.0f, 0.0f);

    glEnd();
    glDisable(GL_BLEND);

    glPopMatrix();

    return 0;

  default:
    return 0;
  }
}



void
audio_widget_make(asched_t *as)
{
  glw_t *w, *y;


  w = glw_create(GLW_CONTAINER,
		 GLW_ATTRIB_COLOR, GLW_COLOR_BLACK,
		 GLW_ATTRIB_ALPHA_SELF, 0.7f,
		 NULL);

  audio_widget = w;

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, w,
		 NULL);
		 
  glw_create(GLW_TEXT_VECTOR,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, "Master volume",
	     NULL);

  glw_create(GLW_RULER,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  glw_create(GLW_EXT,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_OPAQUE, as,
	     GLW_ATTRIB_CALLBACK, miw_mastervol_callback,
	     NULL);
}
