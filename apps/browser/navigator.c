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

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"

#include "browser.h"
#include "browser_file.h"
#include "browser_view.h"
#include "navigator.h"


typedef struct navigator {
  appi_t *nav_ai;
} navigator_t;


static int
navigator_root_widget(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  navigator_t *nav = opaque;
  appi_t *ai = nav->nav_ai;
  inputevent_t *ie;

  va_list ap;
  va_start(ap, sig);
  
  switch(sig) {
  default:
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&ai->ai_ic, ie);
    return 1;
  }
  va_end(ap);
  return 0;
}

/**
 *
 */
static void *
nav_start(void *aux)
{
  browser_root_t *br;
  browser_node_t *bn;
  navigator_t *nav = alloca(sizeof(navigator_t));
  appi_t *ai = appi_create("navigator");
  inputevent_t ie;

  memset(nav, 0, sizeof(navigator_t));
  nav->nav_ai = ai;

  ai->ai_widget =
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_SIGNAL_HANDLER, navigator_root_widget, nav, 1000,
	       NULL);

  ai->ai_widget_miniature = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "browser_miniature",
	       NULL);

  br = browser_root_create("/", &browser_file_protocol);
  bn = br->br_root;

  browser_view_expand_node(bn, ai->ai_widget, &ai->ai_gfs);
  bn->bn_protocol->bp_scan(bn);

  layout_switcher_appi_add(ai);
  layout_world_appi_show(ai);

  while(1) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);
    
    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:

      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_ENTER:
	bn = browser_view_get_current_selected_node(ai->ai_widget);
	printf("ENTER: bn = %p\n", bn);
	if(bn == NULL)
	  break;
	browser_view_expand_node(bn, ai->ai_widget, &ai->ai_gfs);
	bn->bn_protocol->bp_scan(bn);
	browser_node_deref(bn);
	break;

      case INPUT_KEY_BACK:
	bn = browser_view_get_current_node(ai->ai_widget);
	if(bn == NULL)
	  break;

	if(bn->bn_parent != NULL) 
	  browser_view_collapse_node(bn, &ai->ai_gfs);
	browser_node_deref(bn);
	break;

      }
    }
  }

  return NULL;
}



static void
nav_spawn(void)
{
  pthread_t ptid;
  pthread_create(&ptid, NULL, nav_start, NULL);
}


app_t app_navigator = {
  .app_spawn = nav_spawn,
  .app_name = "Navigator",
  .app_model = "browser_start",
};
