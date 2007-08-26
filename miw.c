/*
 *  Media info widgets
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

#include <sys/time.h>
#include <sys/stat.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <libglw/glw.h>

#include "input.h"
#include "layout/layout.h"
#include "miw.h"
#include "showtime.h"

/***********************************************
 *
 *
 */


static int 
miw_codec_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_queue_t *mq = opaque;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    glw_set(w, GLW_ATTRIB_CAPTION, mq->mq_info_codec, NULL);
    return 0;
    
  default:
    return 0;
  }
}


static int 
miw_output_type_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_queue_t *mq = opaque;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    glw_set(w, GLW_ATTRIB_CAPTION, mq->mq_info_output_type, NULL);
    return 0;
    
  default:
    return 0;
  }
}

static int 
miw_rate_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_queue_t *mq = opaque;
  char buf[20];
  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(mq->mq_info_rate > 0)
      snprintf(buf, sizeof(buf), "%d kb/s", mq->mq_info_rate);
    else
      buf[0] = 0;
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;
    
  default:
    return 0;
  }
}


static int 
miw_buffer_depth_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_queue_t *mq = opaque;
  char buf[20];
  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Buffers: %d", mq->mq_len);
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;
    
  default:
    return 0;
  }
}






void
miw_add_queue(glw_t *y, media_queue_t *mq, const char *icon)
{
  glw_t *x;
  const float rw = 0.06;

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_FILENAME, icon,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_codec_callback, mq, 0,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 3.0,
	     NULL);
  
  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_rate_callback, mq, 0,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 3.0,
	     NULL);
  
  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_output_type_callback, mq, 0,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 8.0,
	     NULL);
  
  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);


  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_buffer_depth_callback, mq, 0,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_WEIGHT, 3.0,
	     NULL);


  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, rw,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 8.0,
	     NULL);
}



/***********************************************
 *
 *
 */

static int 
miw_playstatus_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_pipe_t *mp = opaque;
  glw_t *c;
  int x;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    x = mp->mp_playstatus;
    
    if(w->glw_u32 == x)
      return 0;

    w->glw_u32 = x;

    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL)
      glw_destroy(c);
    
    switch(x) {
    case MP_STOP:
      glw_create(GLW_BITMAP, GLW_ATTRIB_PARENT, w, 
		 GLW_ATTRIB_FILENAME, "icon://media-playback-stop.png",
		 NULL);
      break;
    case MP_PAUSE:
      glw_create(GLW_BITMAP, GLW_ATTRIB_PARENT, w, 
		 GLW_ATTRIB_FILENAME, "icon://media-playback-pause.png",
		 NULL);
      break;
    case MP_PLAY:
      glw_create(GLW_BITMAP, GLW_ATTRIB_PARENT, w, 
		 GLW_ATTRIB_FILENAME, "icon://media-playback-start.png",
		 NULL);
      break;
    }
    break;

  case GLW_SIGNAL_RENDER:
    c = TAILQ_FIRST(&w->glw_childs);
    if(c != NULL) {
      glw_render(c, va_arg(ap, void *));
    }
    return 1;

  default:
    break;
  }
  va_end(ap);
  return 0;
}


glw_t *
miw_playstatus_create(glw_t *parent, media_pipe_t *mp)
{
  return glw_create(GLW_EXT,
		    GLW_ATTRIB_PARENT, parent,
		    GLW_ATTRIB_SIGNAL_HANDLER, miw_playstatus_callback, mp, 0,
		    NULL);
}





static int 
miw_audiotime_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_pipe_t *mp = opaque;
  char tmp[100];
  char tot[30];
  uint32_t t;

  int s, m, h;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(mp == NULL || mp->mp_total_time == 0) {
      glw_set(w, GLW_ATTRIB_CAPTION, "", NULL);
      return 0;
    }
    t = mp->mp_time_feedback;
    
    if(w->glw_u32 == t)
      return 0;

    w->glw_u32 = t;

    s = w->glw_u32;
    m = s / 60;
    h = s / 3600;

    if(h > 0) {
      snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
    } else {
      snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
    }

    if(mp->mp_total_time > 0) {
      s = mp->mp_total_time;
      m = s / 60;
      h = s / 3600;

      if(h > 0) {
	snprintf(tot, sizeof(tot), 
		 "%s / %d:%02d:%02d", tmp, h, m % 60, s % 60);
      } else {
	snprintf(tot, sizeof(tot), 
		 "%s / %d:%02d", tmp, m % 60, s % 60);
      }
      glw_set(w, GLW_ATTRIB_CAPTION, tot, NULL);
    } else {
      glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);
    }

     break;

  default:
    break;
  }
  va_end(ap);
  return 0;
}




static int 
miw_audiobar_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  media_pipe_t *mp = opaque;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if(mp->mp_total_time < 1) {
      w->glw_alpha = 0.0;
      break;
    } else {
      w->glw_alpha = 1.0;
    }

    w->glw_extra = (float)mp->mp_time_feedback / (float)mp->mp_total_time;
    break;

  default:
    break;
  }
  va_end(ap);
  return 0;
}


glw_t *
miw_audiotime_create(glw_t *parent, media_pipe_t *mp, float weight, 
		     glw_alignment_t align)
{
  glw_t *z;
  
  z = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_WEIGHT, weight,
		 GLW_ATTRIB_PARENT, parent,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
	     GLW_ATTRIB_WEIGHT, 2.0f,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_audiotime_callback, mp, 0,
	     NULL);


  glw_create(GLW_BAR,
	     GLW_ATTRIB_COLOR, GLW_COLOR_LIGHT_GREEN,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_SIGNAL_HANDLER, miw_audiobar_callback, mp, 0,
	     NULL);
 


  return z;
}



/*****************************************************************************
 *
 * Peak analyzers
 *
 */

#if 0

static int 
miw_peakmeter_callback(glw_t *w, glw_signal_t signal, ...)
{
  audio_ctx_t *actx = &actx0;
  float a, y, v;
  glw_rctx_t *rc;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
    return 0;

  case GLW_SIGNAL_EXT_RENDER:
    rc = va_arg(ap, void *);
    a = 0.7 * rc->rc_alpha;
    v = actx->peak[glw_get_u32(w)];
    if(v > 1)
      v = 1;

    if(w->glw_extra < v)
      w->glw_extra = v;
    else if(w->glw_extra > 0)
      w->glw_extra -= 0.05;
    
    v = w->glw_extra;

    y = -1 + v * 2;

    glPushMatrix();

    glw_scale_and_rotate(rc->rc_aspect, 0.8, 0.0);

    glEnable(GL_BLEND);
    glBegin(GL_QUADS);

    glColor4f(v, 1 - v, 0.0f, a);

    glVertex3f(-1.0f, -1.0f, 0.0f);
    glVertex3f( 1.0f, -1.0f, 0.0f);
    glVertex3f( 1.0f,     y, 0.0f);
    glVertex3f(-1.0f,     y, 0.0f);

    glColor4f(0.1f, 0.1f, 0.1f, a);

    glVertex3f(-1.0f,     y, 0.0f);
    glVertex3f( 1.0f,     y, 0.0f);
    glVertex3f( 1.0f,  1.0f, 0.0f);
    glVertex3f(-1.0f,  1.0f, 0.0f);

    glEnd();
    glDisable(GL_BLEND);

    glPopMatrix();

    return 0;

  default:
    return 0;
  }

}


/**************************************************************************
 *
 *
 *
 */


glw_t *
miw_peakmeters_create(glw_t *parent)
{
  glw_t *x, *y;
  int i;
  struct {
    const char *name;
    int ch;

  } chmap[] = {
    { "SL", 2  },
    { "L",  0  },
    { "C",  4  },
    { "R",  1  },
    { "SR", 3  },
    { "LFE", 5 },
  };

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, parent,
		 NULL);
	
  for(i = 0; i < 6; i++) {
    y = glw_create(GLW_CONTAINER_Y,
		   GLW_ATTRIB_PARENT, x,
		   NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_CAPTION, chmap[i].name,
	       NULL);

    glw_create(GLW_EXT,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_CALLBACK, miw_peakmeter_callback,
	       GLW_ATTRIB_U32, chmap[i].ch,
	       NULL);
  }

  return x;
}

#endif





/*
 *
 */

glw_t *
miw_loading(glw_t *parent, const char *what)
{
  glw_t *y, *w;

  y = glw_create(GLW_CONTAINER_Y, 
		 GLW_ATTRIB_PARENT, parent,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, "Please wait...",
	     NULL);

  w = glw_create(GLW_ROTATOR, 
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_BITMAP, 
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_FILENAME, "icon://loading.png",
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, what,
	     NULL);

  return y;
}




glw_t *
meta_container(glw_t *p, float weight)
{
  p = glw_create(GLW_CONTAINER,
		 GLW_ATTRIB_WEIGHT, weight,
		 GLW_ATTRIB_PARENT, p,
		 NULL);

  return glw_create(GLW_CONTAINER_Y,
		    GLW_ATTRIB_PARENT, p,
		    NULL);
}

