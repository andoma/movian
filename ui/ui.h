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


/**
 * User interface instance
 */
typedef struct uii {

  LIST_ENTRY(uii) uii_link;
  
  struct ui *uii_ui;

} uii_t;


/**
 * User interface
 */
typedef struct ui {

  LIST_ENTRY(ui) ui_link;

  uii_t *(*ui_start)(char *arg);

  void (*ui_stop)(uii_t *uii);

} ui_t;




/**
 *
 */
void ui_loop(void);

void ui_exit_showtime(void);

#endif /* UI_H__ */
