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

static keymap_t *km_global;

/**
 *
 */
static void
km_save(keymap_t *km)
{
  keymap_entry_t *ke;
  htsmsg_t *m = htsmsg_create();
  const char *e;

  LIST_FOREACH(ke, &km->km_entries, ke_link)
    if((e = event_code2str(ke->ke_event)) != NULL && ke->ke_keycode != NULL)
      htsmsg_add_str(m, e, ke->ke_keycode);
  
  hts_settings_save(m, "keymaps/%s", km->km_name);
  htsmsg_destroy(m);
}






/**
 *
 */
static void 
km_set_code(struct prop_sub *sub, prop_event_t event, ...)
{
  keymap_entry_t *ke = sub->hps_opaque;
  const char *str;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_STRING) {
    free(ke->ke_keycode);
    ke->ke_keycode = NULL;
  } else {
    str = va_arg(ap, char *);

    if(ke->ke_keycode != NULL && !strcmp(ke->ke_keycode, str))
      return;

    free(ke->ke_keycode);
    ke->ke_keycode = strdup(str);
    km_save(ke->ke_km);
  }
  km_save(ke->ke_km);
}


/**
 *
 */
static void
keymapper_entry_add(keymap_t *km, const char *str, const char *eventname,
		    event_type_t e)
{
  keymap_entry_t *ke;
  prop_t *p;

  ke = malloc(sizeof(keymap_entry_t));
  ke->ke_km = km;
  ke->ke_keycode = str ? strdup(str) : NULL;
  ke->ke_event = e;
  LIST_INSERT_HEAD(&km->km_entries, ke, ke_link);

  ke->ke_prop =  prop_create(NULL, NULL);
  prop_set_string(prop_create(ke->ke_prop, "type"), "keymapentry");

  p = prop_create(ke->ke_prop, "keycode");
  if(str != NULL)
    prop_set_string(p, str);
  else
    prop_set_void(p);

  ke->ke_sub_keycode = prop_subscribe(p, NULL, km_set_code, ke, NULL, 0);

  p = prop_create(ke->ke_prop, "event");
  prop_set_string(p, eventname);

  prop_set_parent_ex(ke->ke_prop, prop_create(km->km_settings, "nodes"), NULL);
}


/**
 *
 */
static void
keymapper_create_entries(keymap_t *km)
{
  event_type_t e;
  htsmsg_t *m;
  const char *eventname;

  m = hts_settings_load("keymaps/%s", km->km_name);
  for(e = EVENT_NONE + 1; e < EVENT_last_mappable; e++) {
    if((eventname = event_code2str(e)) != NULL)
      keymapper_entry_add(km, m ? htsmsg_get_str(m, eventname) : NULL,
			  eventname, e);
  }
  htsmsg_destroy(m);
}


/**
 *
 */
event_t *
keymapper_resolve(keymap_t *km, const char *str)
{
  keymap_entry_t *ke;
  event_t *e;

  if(km == NULL)
    km = km_global;

  hts_mutex_lock(&km->km_mutex);

  LIST_FOREACH(ke, &km->km_entries, ke_link)
    if(!strcmp(str, ke->ke_keycode))
      break;

  e = ke != NULL ? event_create_simple(ke->ke_event) : NULL;

  hts_mutex_unlock(&km->km_mutex);
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
keymap_t *
keymapper_create(prop_t *settingsparent, const char *name, const char *title)
{
  keymap_t *km;

  km = calloc(1, sizeof(keymap_t));

  LIST_INIT(&km->km_entries);
  hts_mutex_init(&km->km_mutex);

  km->km_name = strdup(name);
  km->km_settings = settings_add_dir(settingsparent, "keymap", title,
				     "keymap");

  keymapper_create_entries(km);
  return km;
}


/**
 *
 */
void
keymapper_init(void)
{
  km_global = keymapper_create(NULL, "global", "Global keymap");
}
