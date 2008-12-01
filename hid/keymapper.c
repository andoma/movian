/*
 *  Key mapper
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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "showtime.h"
#include "keymapper.h"
#include "settings.h"

static keymap_t km_global;

/**
 *
 */
event_t *
keymapper_resolve(keymap_t *km, const char *str)
{
  keymap_entry_t *ke;
  event_t *e;

  if(km == NULL)
    km = &km_global;

  hts_mutex_lock(&km->km_mutex);

  LIST_FOREACH(ke, &km->km_entries, ke_link)
    if(!strcmp(str, ke->ke_keycode))
      break;

  e = ke != NULL ? event_create_simple(ke->ke_event) : NULL;

  hts_mutex_lock(&km->km_mutex);
  return e;
}


/**
 *
 */
void
keymapper_deliver(keymap_t *km, const char *str)
{
  event_t *e = keymapper_resolve(km, str);
  if(e != NULL)
    event_post(e);
}


/**
 *
 */
void
keymapper_init(keymap_t *km, prop_t *settingsparent, const char *title)
{
  if(km == NULL)
    km = &km_global;

  LIST_INIT(&km->km_entries);
  hts_mutex_init(&km->km_mutex);

  km->km_settings = settings_add_dir(settingsparent, "keymap", title);
}
