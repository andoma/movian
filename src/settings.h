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

#include "prop/prop.h"
#include "htsmsg/htsmsg.h"

#define SETTINGS_INITIAL_UPDATE 0x1
#define SETTINGS_PASSWORD       0x2 // Make a password entry (hidden display)

typedef void (settings_saver_t)(void *opaque, htsmsg_t *htsmsg);

struct setting;
typedef struct setting setting_t;

typedef struct settings_multiopt {
  const char *id;
  const char *title;
  const char *icon;
} settings_multiopt_t;

prop_t *settings_add_dir(prop_t *parent, prop_t *title,
			 const char *subtype, const char *icon,
			 prop_t *shortdesc);

prop_t *settings_add_dir_cstr(prop_t *parent, const char *title,
			      const char *subtype, const char *icon,
			      const char *shortdesc);

prop_t *settings_get_dirlist(prop_t *parent);

prop_t *settings_create_divider(prop_t *parent, prop_t *caption);



setting_t *settings_create_bool(prop_t *parent, const char *id, 
				prop_t *title, int initial, htsmsg_t *store,
				prop_callback_int_t *cb, void *opaque,
				int flags, prop_courier_t *pc,
				settings_saver_t *saver, void *saver_opaque);

void settings_set_bool(setting_t *s, int v);

void settings_toggle_bool(setting_t *s);

setting_t *settings_create_multiopt(prop_t *parent, const char *id,
				    prop_t *title,
				    prop_callback_string_t *cb, void *opaque);

void settings_multiopt_add_opt(setting_t *parent, const char *id,
			       prop_t *title, int selected);

void settings_multiopt_add_opt_cstr(setting_t *parent, const char *id,
				    const char *title, int selected);

void settings_multiopt_initiate(setting_t *s, htsmsg_t *store,
				settings_saver_t *saver, void *saver_opaque);

setting_t *settings_create_string(prop_t *parent, const char *id, 
				  prop_t *title, const char *initial, 
				  htsmsg_t *store,
				  prop_callback_string_t *cb, void *opaque,
				  int flags, prop_courier_t *pc,
				  settings_saver_t *saver, void *saver_opaque);

setting_t *settings_create_int(prop_t *parent, const char *id, 
			       prop_t *title,
			       int initial, htsmsg_t *store,
			       int min, int max, int step,
			       prop_callback_int_t *cb, void *opaque,
			       int flags, const char *unit,
			       prop_courier_t *pc,
			       settings_saver_t *saver, void *saver_opaque);


void settings_set_int(setting_t *s, int v);

void settings_add_int(setting_t *s, int delta);

prop_t *settings_get_value(setting_t *s);

prop_t *settings_get_node(setting_t *s);

setting_t *settings_create_action(prop_t *parent, const char *id,
				  prop_t *title,
				  prop_callback_t *cb, void *opaque,
				  prop_courier_t *pc);

void setting_destroy(setting_t *s);

void settings_init(void);

void settings_generic_save_settings(void *opaque, htsmsg_t *msg);

void settings_generic_set_bool(void *opaque, int value);

void settings_create_info(prop_t *parent, const char *image,
			  prop_t *description);

extern prop_t *settings_apps;
extern prop_t *settings_sd;

#endif /* SETTINGS_H__ */
