/*
 *  User interface top control
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

#include "ui.h"
#include "ui/glw/glw.h"
#include "libhts/htsthreads.h"

static hts_mutex_t ui_mutex;
static hts_cond_t ui_cond;

static int showtime_running;

/**
 *
 */
void
ui_exit_showtime(void)
{
  hts_mutex_lock(&ui_mutex);
  showtime_running = 0;
  hts_cond_signal(&ui_cond);
  hts_mutex_unlock(&ui_mutex);
}


/**
 *
 */
void
ui_loop(void)
{
  uii_t *uii;


  glw_init_global();

  showtime_running = 1;

  uii = glw_start(NULL);

  hts_mutex_lock(&ui_mutex);


  while(showtime_running) {
    hts_cond_wait(&ui_cond, &ui_mutex);
  }

  hts_mutex_unlock(&ui_mutex);

  uii->uii_ui->ui_stop(uii);
}
