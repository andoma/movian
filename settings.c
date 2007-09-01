/*
 *  Menu for various general settings
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
#include <ctype.h>

#include <libhts/htscfg.h>
#include <libglw/glw.h>

#include "settings.h"
#include "menu.h"
#include "showtime.h"

int mp_show_extra_info;



static int 
general_extra_info(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  char buf[50];

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Extra media info: %s",
	     mp_show_extra_info ? "Yes" : "No");

    w = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(w != NULL)
      glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;

  case GLW_SIGNAL_CLICK:
    mp_show_extra_info = !mp_show_extra_info;
    return 1;
    
  default:
    return 0;
  }
}



static int 
general_menu_exit(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  switch(signal) {
  case GLW_SIGNAL_CLICK:
    showtime_exit(w->glw_u32);
    return 1;
    
  default:
    return 0;
  }
}


extern void audio_mixer_menu_setup(glw_t *parent);

void
settings_menu_create(glw_t *parent)
{
  glw_t *v;

  v = menu_create_submenu(parent, "icon://settings.png", 
			  "General settings", 0);

  menu_create_item(parent, "icon://suspend.png", "Suspend",
		   general_menu_exit, NULL, 1, 0);

  menu_create_item(parent, "icon://power.png", "Power off",
		   general_menu_exit, NULL, 0, 0);

  menu_create_item(v, NULL, "Extra info", general_extra_info, NULL, 0, 0);

  audio_mixer_menu_setup(v);

}
