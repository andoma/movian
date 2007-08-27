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
#include "audio_ui.h"

static float audio_alpha;
static int show_audio;
static glw_t *audio_widget;

static float audio_vol;
static int audio_mute;

void
audio_render(float alpha)
{
  glw_rctx_t rc;


  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = 16.0 / 9.0 * 20.0f;

  if(audio_alpha < 0.01 || audio_widget == NULL)
    return;

  rc.rc_alpha = audio_alpha * alpha;

  glPushMatrix();

  glTranslatef(0.0, -0.95, 0.0f);
  glScalef(0.75, 0.04, 1.0f);
  
  glw_render(audio_widget, &rc);

  glPopMatrix();
}

void
audio_layout(void)
{
  glw_rctx_t rc;

  if(show_audio > 0)
    show_audio--;

  audio_alpha = (audio_alpha * 15 + 
		 (show_audio || audio_mute ? 1 : 0)) / 16.0f;
  
  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = 16.0 / 9.0 * 20.0f;
  
  glw_layout(audio_widget, &rc);
}

void
audio_ui_vol_changed(float vol, int mute)
{
  show_audio = 100;
  audio_vol = vol;
  audio_mute = mute;
}



/*****************************************************************************
 *
 * Master volume
 *
 */

static int 
audio_mastervol_bar_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  switch(signal) {
    return 0;

  case GLW_SIGNAL_PREPARE:
    w->glw_extra = GLW_LP(3, w->glw_extra, audio_vol);

    if(audio_mute)
      glw_set(w, GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_RED, NULL);
    else
      glw_set(w, GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_GREEN, NULL);

    return 0;

  default:
    return 0;
  }
}


static int 
audio_mastervol_txt_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  char buf[30];

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(audio_mute)
      snprintf(buf, sizeof(buf), "Master volume: Muted");
    else
      snprintf(buf, sizeof(buf), "Master volume: %d%%", 
	       (int)(audio_vol * 100));
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;

  default:
    return 0;
  }
}



void
audio_widget_make(void)
{
  glw_t *w, *z;


  w = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  audio_widget = w;

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, w,
		 NULL);
		 
  glw_create(GLW_BAR,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_SIGNAL_HANDLER, audio_mastervol_bar_callback, NULL, 0,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_SIGNAL_HANDLER, audio_mastervol_txt_callback, NULL, 0,
	     NULL);
}
