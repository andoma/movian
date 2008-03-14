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

/**
 * Create a new application instance
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
 * Destroy an application
 */
void
appi_destroy(appi_t *ai)
{
  mp_deinit(&ai->ai_mp);
  free((void *)ai->ai_name);
  input_flush_queue(&ai->ai_ic);
  free(ai);
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
  LOADAPP(navigator);
}
