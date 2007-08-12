/*
 *  User menues
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

#ifndef MENU_H
#define MENU_H

#include "app.h"

void menu_init_app(appi_t *ai);

glw_t *menu_create_submenu(glw_t *p, const char *icon, const char *title,
			   int first);

int menu_input(appi_t *ai, inputevent_t *ie);

void menu_render(appi_t *ai, float alpha);

void menu_layout(appi_t *ai);

void menu_destroy(appi_t *ai);

glw_t *menu_create_item(glw_t *p, const char *icon, const char *title,
			glw_callback_t *cb, void *opaque, uint32_t u32,
			int first);

glw_t *menu_push_top_menu(appi_t *ai, const char *title);

void menu_pop_top_menu(appi_t *ai);

appi_t *menu_find_ai(glw_t *w);

int menu_post_key_pop_and_hide(glw_t *w, glw_signal_t signal, ...);

#define MENU_ASPECT 0.4

#endif /* MENU_H */
