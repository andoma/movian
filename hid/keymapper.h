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

#include "event.h"
#include "app.h"

LIST_HEAD(hid_keydesc_list, hid_keydesc);
LIST_HEAD(hid_keycode_list, hid_keycode);


/**
 * Struct for representing a keycode (INPUT_KEY_xxx)
 *
 * Each of these can be mapped to from multiple hid_keydesc:s
 */
typedef struct hid_keycode {
  LIST_ENTRY(hid_keycode) hkc_link;
  struct hid_keydesc_list hkc_descs;
  event_type_t hkc_code;
} hid_keycode_t;

/**
 * Struct for looking up a key description (ASCII string)
 */
typedef struct hid_keydesc {
  char *hkd_desc;
  LIST_ENTRY(hid_keydesc) hkd_hash_link;

  LIST_ENTRY(hid_keydesc) hkd_keycode_link;
  hid_keycode_t *hkd_hkc;

} hid_keydesc_t;


void keymapper_resolve(const char *str);

hid_keycode_t *keymapper_find_by_code(event_type_t val);
hid_keydesc_t *keymapper_find_by_desc(const char *str);
void keymapper_map(hid_keydesc_t *hkm, hid_keycode_t *hkc);

const char *keycode2str(event_type_t code);
event_type_t keystr2code(const char *str);

void keymapper_init(appi_t *ai, glw_t *settings);

#endif /* KEYMAPPER_H */
