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

extern glw_t *layout_root;
extern float layout_switcher_alpha;

/**
 * Functions in layout.c
 */ 
void layout_hide(appi_t *ai);

void layout_draw(float aspect);

void layout_create(void);

void layout_appi_add(appi_t *ai);

void layout_appi_del(appi_t *ai);

void layout_app_add(app_t *a);

void layout_app_del(app_t *a);

/**
 * Functions in layout_world.c
 */ 
void layout_world_create(void);

void layout_world_render(float aspect);

void layout_world_appi_show(appi_t *ai);

/**
 * Functions in layout_switcher.c
 */ 

void layout_switcher_create(void);

void layout_switcher_appi_add(appi_t *ai, glw_t *w);

void layout_switcher_render(float aspect);

#endif /* LAYOUT_H */

