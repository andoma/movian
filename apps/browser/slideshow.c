/*
 *  Generic browser
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

#define _GNU_SOURCE
#include <pthread.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "browser.h"
#include "navigator.h"

void
nav_slideshow(nav_t *n, navdir_t *nd, naventry_t *ne0)
{
  appi_t *ai = n->n_ai;
  int run = 1;
  naventry_t *ne;
  inputevent_t ie;
  glw_t *x;

  ai->ai_req_fullscreen = AI_FS_BLANK;

  glw_lock();

  if(nd->nd_slideshow != NULL)
    glw_destroy(nd->nd_slideshow);

  nd->nd_slideshow = glw_create(GLW_HLIST,
				GLW_ATTRIB_SIDEKICK_SCALE, 0.05,
				NULL);

  glw_unlock();

  n->n_slideshow_mode = 1;

  /* Create full scale images, notice that the actual load wont take
     place until the widget is layouted */

  TAILQ_FOREACH(ne, &nd->nd_childs, ne_parent_link) {
    if(ne->ne_mi.mi_type != MI_IMAGE)
      continue;

    x = glw_create(GLW_BITMAP, 
		   GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		   GLW_ATTRIB_BORDER_WIDTH, 0.01,
		   GLW_ATTRIB_FILENAME, ne->ne_url,
		   GLW_ATTRIB_PARENT, nd->nd_slideshow,
		   NULL);

    if(ne == ne0)
      nd->nd_slideshow->glw_selected = x;
      
  }

  while(run) {
    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {

      case INPUT_KEY_RIGHT:
	glw_nav_signal(nd->nd_slideshow, GLW_SIGNAL_RIGHT);
	break;

      case INPUT_KEY_LEFT:
	glw_nav_signal(nd->nd_slideshow, GLW_SIGNAL_LEFT);
	break;

      case INPUT_KEY_CLOSE:
	  layout_hide(ai);
      case INPUT_KEY_BACK:
      case INPUT_KEY_SELECT:
	run = 0;
	break;

      default:
	break;
      }
      break;
    }
  }

  ai->ai_req_fullscreen = 0;

  n->n_slideshow_mode = 0;
}
