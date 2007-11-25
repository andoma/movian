/*
 *  Application handing
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

#include <string.h>

#include "showtime.h"
#include "app.h"
#include "layout/layout.h"
#include "menu.h"

struct appi_list appis;


appi_t *
appi_spawn(const char *name, const char *icon)
{
  appi_t *ai;

  ai = calloc(1, sizeof(appi_t));

  LIST_INSERT_HEAD(&appis, ai, ai_global_link);

  ai->ai_name = strdup(name);
  ai->ai_icon = strdup(icon);

  menu_init_app(ai);

  ai->ai_req_aspect = 1.0f;

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, ai->ai_name, ai);

  layout_register_appi(ai);

  return ai;
}

/*
 *
 */

int 
appi_widget_post_key(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  int r = 0;
  inputevent_t *ie;
  appi_t *ai = opaque;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    if(ai->ai_no_input_events) {
      /* app cannot handle input events, we handle BACK for it */
      if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_BACK)
	layout_hide(ai);
      break;
    }

    input_postevent(&ai->ai_ic, ie);
    r = 1;
  default:
    break;
  }
  va_end(ap);
  return r;
}


