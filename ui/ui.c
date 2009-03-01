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

#include "showtime.h"
#include "ui.h"
#include "keymapper.h"


static hts_mutex_t ui_mutex;
static hts_cond_t ui_cond;

static int showtime_retcode = -1;

static struct ui_list uis;
static struct uii_list uiis;

static LIST_HEAD(, deferred) deferreds;

static int ui_event_handler(event_t *e, void *opaque);

/**
 *
 */
void
ui_exit_showtime(int retcode)
{
  hts_mutex_lock(&ui_mutex);
  showtime_retcode = retcode;
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
  
  hts_mutex_init(&ui_mutex);
  hts_cond_init(&ui_cond);

  keymapper_init();

  ui_initialize();

  ui = LIST_FIRST(&uis);
  if(ui != NULL) {
    uii = ui->ui_start(ui, NULL);
    LIST_INSERT_HEAD(&uiis, uii, uii_link);
  }
}

/**
 * Showtime mainloop
 *
 * We also drive a low resolution timer framework from here
 */
int
ui_main_loop(void)
{
  time_t now;
  struct timespec ts;
  deferred_t *d;
  deferred_callback_t *dc;

  ts.tv_nsec = 0;

  /* Register an event handler  */
  event_handler_register("uimain", ui_event_handler, EVENTPRI_MAIN, NULL);

  hts_mutex_lock(&ui_mutex);

  while(1) {

    if(showtime_retcode != -1)
      break;

    time(&now);
    
    while((d = LIST_FIRST(&deferreds)) != NULL && d->d_expire <= now) {
      dc = d->d_callback;
      LIST_REMOVE(d, d_link);
      d->d_callback = NULL;
      dc(d->d_opaque);
    }

    if((d = LIST_FIRST(&deferreds)) != NULL) {
      ts.tv_sec = d->d_expire;
      hts_cond_wait_timeout(&ui_cond, &ui_mutex, &ts);
    } else {
      hts_cond_wait(&ui_cond, &ui_mutex);
    }
  }

  hts_mutex_unlock(&ui_mutex);

  //  uii->uii_ui->ui_stop(uii);

  return showtime_retcode;
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


/**
 * Catch events used for exiting
 */
static int
ui_event_handler(event_t *e, void *opaque)
{
  int v = 0;
  switch(e->e_type) {
  default:
    return 0;

  case EVENT_CLOSE:
    v = 0;
    break;

  case EVENT_QUIT:
    v = 0;
    break;

  case EVENT_POWER:
    v = 10;
    break;
  }

  ui_exit_showtime(v);
  return 1;
}


/**
 *
 */
static int
deferredcmp(deferred_t *a, deferred_t *b)
{
  if(a->d_expire < b->d_expire)
    return -1;
  else if(a->d_expire > b->d_expire)
    return 1;
 return 0;
}


/**
 *
 */
void
deferred_arm_abs(deferred_t *d, deferred_callback_t *callback, void *opaque,
		 time_t when)
{
  hts_mutex_lock(&ui_mutex);

  if(d->d_callback != NULL)
    LIST_REMOVE(d, d_link);
    
  d->d_callback = callback;
  d->d_opaque = opaque;
  d->d_expire = when;

  LIST_INSERT_SORTED(&deferreds, d, d_link, deferredcmp);

  hts_mutex_unlock(&ui_mutex);
}

/**
 *
 */
void
deferred_arm(deferred_t *d, deferred_callback_t *callback,
	     void *opaque, int delta)
{
  time_t now;
  time(&now);
  
  deferred_arm_abs(d, callback, opaque, now + delta);
}

/**
 *
 */
void
deferred_disarm(deferred_t *d)
{
  if(d->d_callback) {
    LIST_REMOVE(d, d_link);
    d->d_callback = NULL;
  }
}
