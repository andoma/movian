/*
 *  User interface top include file
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

#ifndef UI_H__
#define UI_H__

#include <libhts/htsq.h>
#include "event.h"
#include "keymapper.h"

LIST_HEAD(ui_list,  ui);
LIST_HEAD(uii_list, uii);


/**
 * User interface instance
 */
typedef struct uii {

  LIST_ENTRY(uii) uii_link;
  
  struct ui *uii_ui;

  keymap_t *uii_km;

} uii_t;


/**
 * User interface class
 */
typedef struct ui {

  const char *ui_title;

  LIST_ENTRY(ui) ui_link;

  uii_t *(*ui_start)(struct ui *ui, const char *arg);

  void (*ui_stop)(uii_t *uii);

  int (*ui_dispatch_event)(uii_t *uii, event_t *e);

} ui_t;




/**
 *
 */
void ui_loop(void);

void ui_exit_showtime(void);

void ui_dispatch_event(event_t *e, const char *buf, uii_t *uii);

#endif /* UI_H__ */
