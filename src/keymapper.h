/*
 *  Input key mapper
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

#ifndef KEYMAPPER_H
#define KEYMAPPER_H

#include <arch/threads.h>
#include "prop/prop.h"
#include "ui/ui.h"
#include "event.h"

LIST_HEAD(keymap_entry_list, keymap_entry);


typedef struct keymap_defmap {
  action_type_t kd_action;
  const char *kd_keycode;
} keymap_defmap_t;


/**
 * A keymap entry is used to translate a keycode (ascii-string)
 * into an event
 */
typedef struct keymap_entry {
  LIST_ENTRY(keymap_entry) ke_link;
  char *ke_keycode1;
  char *ke_keycode2;
  action_type_t ke_action;
  prop_t *ke_prop;

  prop_sub_t *ke_sub_keycode;

  struct keymap *ke_km;

} keymap_entry_t;


/**
 * We support having multiple keymaps, one global, and locals per
 * user interface instance
 */
typedef struct keymap {
  struct keymap_entry_list km_entries;
  char *km_name;
  prop_t *km_settings;   /* Pointer to settings in settings tree */
} keymap_t;


/**
 *
 */
struct uii;
struct event;

struct event *keymapper_resolve(const char *str);

keymap_t *keymapper_create(prop_t *settingsparent, const char *name,
			   const char *title, const keymap_defmap_t *def);

void keymapper_init(void);

#endif /* KEYMAPPER_H */
