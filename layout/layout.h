/*
 *  Common layout functions
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

#ifndef LAYOUT_H
#define LAYOUT_H

#include <libglw/glw.h>
#include "hid/input.h"
#include "app.h"

extern int layout_menu_display;

void layout_hide(appi_t *ai);

glw_t *layout_win_create(const char *name, const char *icon,
			 glw_callback_t *cb, void *opaque);

void layout_register_app(app_t *a);

glw_t *bar_title(const char *str);

appi_t *layout_get_cur_app(void);

void layout_std_draw(float aspect);

void layout_std_create(void);

#endif /* LAYOUT_H */


