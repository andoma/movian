/*
 *  Browser slideshow
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

#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include "showtime.h"
#include "browser.h"
#include "browser_view.h"

void
browser_slideshow(browser_node_t *cur, glw_t *parent, ic_t *ic)
{
  glw_t *w, *b, *z, *pw = NULL;
  browser_node_t *dir = cur->bn_parent;
  browser_root_t *br  = cur->bn_root;
  browser_node_t *c, **a;
  int cnt, run = 1;
  int64_t type;
  inputevent_t ie;
  int paused = 0;

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, parent,
		 NULL);

  w = glw_create(GLW_SLIDESHOW,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

  a = browser_get_array_of_childs(br, dir);
  for(cnt = 0; (c = a[cnt]) != NULL; cnt++) {
    
    pthread_mutex_lock(&c->bn_ftags_mutex);

    if(c->bn_type == FA_FILE &&
       !filetag_get_int(&c->bn_ftags, FTAG_FILETYPE, &type) &&
       type == FILETYPE_IMAGE) {

      b = glw_create(GLW_BITMAP,
		     GLW_ATTRIB_FILENAME, c->bn_url,
		     GLW_ATTRIB_FLAGS, GLW_KEEP_ASPECT | GLW_BORDER_BLEND,
		     GLW_ATTRIB_VERTEX_BORDERS,  0.01, 0.01, 0.01, 0.01,
		     GLW_ATTRIB_TEXTURE_BORDERS, 0.01, 0.01, 0.01, 0.01,
		     GLW_ATTRIB_PARENT, w,
		     NULL);

      if(c == cur)
	glw_select(w, b);
    }

    pthread_mutex_unlock(&c->bn_ftags_mutex);

    browser_node_deref(c); /* 'c' may be free'd here */
  }

  free(a);

  while(run) {

    if(paused && pw == NULL)
      pw = glw_create(GLW_MODEL,
		      GLW_ATTRIB_PARENT, z,
		      GLW_ATTRIB_FILENAME, "browser/slideshow-paused",
		      NULL);
    else if(!paused && pw) {
      glw_destroy(pw);
      pw = NULL;
    }


    glw_set(w, GLW_ATTRIB_SPEED, paused ? 0.0f : 1.0f, NULL);

    input_getevent(ic, 1, &ie, NULL);

    switch(ie.type) {
    default:
      break;
      
    case INPUT_KEY:
      switch(ie.u.key) {
      default:
	break;
	
      case INPUT_KEY_PLAYPAUSE:
	paused = !paused;
	break;

      case INPUT_KEY_PLAY:
	paused = 0;
	break;

      case INPUT_KEY_PAUSE:
	paused = 1;
	break;

      case INPUT_KEY_BACK:
	run = 0;
	break;

      case INPUT_KEY_NEXT:
	glw_send_signal(w, GLW_SIGNAL_NEXT, NULL);
	break;

      case INPUT_KEY_PREV:
      case INPUT_KEY_RESTART_TRACK:
	glw_send_signal(w, GLW_SIGNAL_PREV, NULL);
	break;
      }
    }
  }

  glw_detach(z);
}
