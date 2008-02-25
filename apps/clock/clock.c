/*
 *  Clock
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include <layout/layout.h>
#include <layout/layout_forms.h>


const char *months[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int
clock_date_update(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  char buf[30];
  struct tm tm;

  switch(sig) {
  default:
    break;
  case GLW_SIGNAL_PREPARE:
    localtime_r(&walltime, &tm);
    
    snprintf(buf, sizeof(buf), "%d %s", tm.tm_mday, months[tm.tm_mon]);
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    break;
  }
  return 0;
}



static int
clock_time_update(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  char buf[30];
  struct tm tm;

  switch(sig) {
  default:
    break;
  case GLW_SIGNAL_PREPARE:
    localtime_r(&walltime, &tm);
    
    snprintf(buf, sizeof(buf), "%d:%02d",
	     tm.tm_hour, tm.tm_min);
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    break;
  }
  return 0;
}


static glw_t *
buildclock(void)
{
  glw_t *y;

  y = glw_create(GLW_CONTAINER_Y,
		 NULL);
  
  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 8.0,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_SIGNAL_HANDLER, clock_date_update, NULL, 0,
	     NULL);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_SIGNAL_HANDLER, clock_time_update, NULL, 0,
	     NULL);
  return y;
}


static void *
clock_start(void *aux)
{
  appi_t *ai = appi_create("clock");

  ai->ai_widget = buildclock();

  ai->ai_widget_miniature = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "clock_miniature",
	       NULL);

  layout_switcher_appi_add(ai);
  layout_world_appi_show(ai);

  while(1) {
    sleep(1);
  }

  return NULL;
}



static void
clock_spawn(void)
{
  pthread_t ptid;

  pthread_create(&ptid, NULL, clock_start, NULL);
}


app_t app_clock = {
  .app_spawn = clock_spawn,
  .app_name = "Clock",
  .app_model = "clock_start",
};
