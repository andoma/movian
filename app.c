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
#include "apps/launcher/launcher.h"


pthread_mutex_t appi_list_mutex = PTHREAD_MUTEX_INITIALIZER;
struct appi_list appis;

/**
 * Spawn a new application instance
 */
appi_t *
appi_spawn(const char *name, const char *icon)
{
  appi_t *ai;
  abort();

  ai = calloc(1, sizeof(appi_t));


  ai->ai_name = strdup(name);
  ai->ai_icon = strdup(icon);

  //  menu_init_app(ai);

  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, ai->ai_name, ai);

  layout_appi_add(ai);

  return ai;
}


/**
 * Spawn a new application instance
 */
appi_t *
appi_create(const char *name)
{
  appi_t *ai = calloc(1, sizeof(appi_t));

  ai->ai_name = strdup(name);
  input_init(&ai->ai_ic);
  mp_init(&ai->ai_mp, ai->ai_name, ai);
  
  glw_focus_stack_init(&ai->ai_gfs);
  
  return ai;
}

/**
 *
 */
void
appi_link(appi_t *ai)
{
  pthread_mutex_lock(&appi_list_mutex);
  LIST_INSERT_HEAD(&appis, ai, ai_link);
  pthread_mutex_unlock(&appi_list_mutex);
}

/**
 * Init a specific app
 */
static void
app_init(app_t *a)
{
  launcher_app_add(a);
}


/**
 * Load all applications
 */

#define LOADAPP(a)				\
 {						\
   extern app_t app_ ## a;			\
   app_init(&app_ ## a);			\
 }

void
apps_load(void)
{
  launcher_init();

  LOADAPP(clock);
}
