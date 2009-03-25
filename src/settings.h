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

#include "prop.h"
#include <libhts/htsmsg.h>

#define SETTINGS_INITIAL_UPDATE 0x1

struct setting;
typedef struct setting setting_t;

typedef struct settings_multiopt {
  const char *id;
  const char *title;
  const char *icon;
} settings_multiopt_t;

typedef void (setting_callback_bool_t)(void *opaque, int value);

typedef void (setting_callback_string_t)(void *opaque, const char *str);

prop_t *settings_add_dir(prop_t *parent, const char *id, 
			 const char *title, const char *subtype);

prop_t *settings_get_dirlist(prop_t *parent);



setting_t *settings_add_bool(prop_t *parent, const char *id, 
			     const char *title, int initial, htsmsg_t *store,
			     setting_callback_bool_t *cb, void *opaque,
			     int flags);

setting_t *settings_add_multiopt(prop_t *parent, const char *id,
				 const char *title,
				 setting_callback_string_t *cb, void *opaque);

void settings_multiopt_add_opt(setting_t *parent, const char *id,
			       const char *title, int selected);

setting_t *settings_add_string(prop_t *parent, const char *id, 
			       const char *title, const char *initial, 
			       htsmsg_t *store,
			       setting_callback_string_t *cb, void *opaque,
			       int flags);

void settings_init(void);

#endif /* SETTINGS_H__ */
