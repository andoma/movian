/*
 *  Settings framework
 *  Copyright (C) 2008 Andreas Öman
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

#ifndef SETTINGS_H__
#define SETTINGS_H__

#include "ui/glw/glw.h"

struct setting;
typedef struct setting setting_t;


typedef void (setting_callback_bool_t)(void *opaque, int value);

prop_t *settings_add_dir(prop_t *parent, const char *id, 
			     const char *title);

prop_t *settings_get_dirlist(prop_t *parent);



setting_t *settings_add_bool(prop_t *parent, const char *id, 
			     const char *title, int initial, htsmsg_t *store,
			     setting_callback_bool_t *cb, void *opaque);

void settings_init(void);

#endif /* SETTINGS_H__ */
