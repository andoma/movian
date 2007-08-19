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

#include "showtime.h"
#include "app.h"
#include "layout/layout.h"
#include "menu.h"

struct app_queue apps;
struct appi_queue appis;
struct appi_queue appis_hidden;

void
app_init(void)
{
  TAILQ_INIT(&apps);
  TAILQ_INIT(&appis);
  TAILQ_INIT(&appis_hidden);
}

void
app_register(app_t *a)
{
  layout_register_app(a);

  TAILQ_INSERT_TAIL(&apps, a, app_link);
  if(a->app_auto_spawn)
    appi_spawn(a, 0);
}


appi_t *
appi_spawn(app_t *a, int visible)
{
  appi_t *ai;

  TAILQ_FOREACH(ai, &appis_hidden, ai_global_link) {
    if(ai->ai_app == a) {
      if(!visible)
	return ai;

      TAILQ_REMOVE(&appis_hidden, ai, ai_global_link);
      TAILQ_INSERT_TAIL(&appis, ai, ai_global_link);
      printf("%p moved from hidden to visible\n", ai);
      return ai;
    }
  }
  
 if(a->app_max_instances && a->app_cur_instances == a->app_max_instances)
    return NULL;

  ai = calloc(1, sizeof(appi_t));

  ai->ai_app = a;
  a->app_cur_instances++;

  menu_init_app(ai);
#if 0  
  ai->ai_widget = layout_win_create(a->app_name, a->app_icon, 
				    a->app_win_callback, ai);
#endif

  if(a->app_def_aspect == 0)
    ai->ai_req_aspect = 1.0f;

  if(visible) {
    TAILQ_INSERT_TAIL(&appis, ai, ai_global_link);
  } else {
    TAILQ_INSERT_TAIL(&appis_hidden, ai, ai_global_link);
    printf("%s created as hidden one (%p)\n", 
	   a->app_name, ai);
  }

  LIST_INSERT_HEAD(&a->app_instances, ai, ai_app_link);

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, a->app_name, ai, 0);
  ai->ai_app->app_spawn(ai);
  return ai;
}


appi_t *
appi_find(app_t *a, int visible, int create)
{
  appi_t *ai;

  TAILQ_FOREACH(ai, &appis, ai_global_link) {
    if(ai->ai_app == a)
      return ai;
  }

  TAILQ_FOREACH(ai, &appis_hidden, ai_global_link) {
    if(ai->ai_app == a)
      return ai;
  }

  return create ? appi_spawn(a, visible) : NULL;
}



void
appi_hide(appi_t *ai)
{
  TAILQ_REMOVE(&appis, ai, ai_global_link);
  TAILQ_INSERT_TAIL(&appis_hidden, ai, ai_global_link);
}




appi_t *
appi_spawn2(app_t *a, glw_t *p)
{
  appi_t *ai;

  ai = calloc(1, sizeof(appi_t));

  ai->ai_app = a;
  a->app_cur_instances++;

  menu_init_app(ai);

  ai->ai_widget = p;

  if(a->app_def_aspect == 0)
    ai->ai_req_aspect = 1.0f;

  TAILQ_INSERT_TAIL(&appis, ai, ai_global_link);

  LIST_INSERT_HEAD(&a->app_instances, ai, ai_app_link);

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, a->app_name, ai, 0);
  ai->ai_app->app_spawn(ai);
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


