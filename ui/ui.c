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

#include <string.h>
#include <libhts/htsthreads.h>

#include "ui.h"
#include "keymapper.h"


static hts_mutex_t ui_mutex;
static hts_cond_t ui_cond;

static int showtime_running;

static struct ui_list uis;
static struct uii_list uiis;

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
static void
ui_initialize(void)
{
#define link_ui(name) do {\
 extern ui_t name ## _ui;\
 LIST_INSERT_HEAD(&uis, &name ## _ui, ui_link);\
}while(0)

#ifdef CONFIG_GLW_FRONTEND_X11
  link_ui(glw_x11);
#endif
}


/**
 *
 */
void
ui_init(void)
{
  ui_t *ui;
  uii_t *uii;

  keymapper_init();

  ui_initialize();

  showtime_running = 1;

  ui = LIST_FIRST(&uis);
  if(ui != NULL) {
    uii = ui->ui_start(ui, NULL);
    LIST_INSERT_HEAD(&uiis, uii, uii_link);
  }
}

/**
 *
 */
void
ui_main_loop(void)
{
  /* Main loop */

  hts_mutex_lock(&ui_mutex);
  while(showtime_running) {
    hts_cond_wait(&ui_cond, &ui_mutex);
  }
  hts_mutex_unlock(&ui_mutex);

  //  uii->uii_ui->ui_stop(uii);
}


/**
 *
 */
void
ui_dispatch_event(event_t *e, const char *buf, uii_t *uii)
{
  int r, l;
  event_keydesc_t *ek;

  if(buf != NULL) {
    l = strlen(buf);
    ek = event_create(EVENT_KEYDESC, sizeof(event_keydesc_t) + l + 1);
    memcpy(ek->desc, buf, l + 1);
    ui_dispatch_event(&ek->h, NULL, uii);

    keymapper_resolve(buf, uii);
  }

  if(e == NULL)
    return;

  if(uii != NULL && uii->uii_ui->ui_dispatch_event != NULL) {
    r = uii->uii_ui->ui_dispatch_event(uii, e);
  } else {

    r = 0;
    
    LIST_FOREACH(uii, &uiis, uii_link) {
      if(uii->uii_ui->ui_dispatch_event != NULL) 
	if((r = uii->uii_ui->ui_dispatch_event(uii, e)) != 0)
	  break;
    }
  }

  if(r == 0) {
    /* Not consumed, drop it into the main event dispatcher */
    event_post(e);
  } else {
    event_unref(e);
  }
}
