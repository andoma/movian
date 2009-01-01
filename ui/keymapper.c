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
static void 
km_set_code(struct prop_sub *sub, prop_event_t event, ...)
{
  keymap_entry_t *ke = sub->hps_opaque;
  const char *str;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_STRING)
    return;

  str = va_arg(ap, char *);

  if(strcmp(ke->ke_keycode, str)) {
    free(ke->ke_keycode);
    ke->ke_keycode = strdup(str);
  }
}


/**
 *
 */
static void
km_set_event(struct prop_sub *sub, prop_event_t event, ...)
{
  keymap_entry_t *ke = sub->hps_opaque;
  const char *str;
  event_type_t e;
  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_STRING)
    return;

  str = va_arg(ap, char *);
  e = event_str2code(str);
  
  if(ke->ke_event != e) {
    ke->ke_event = e;
  }
}


/**
 *
 */
static void
keymapper_entry_add(keymap_t *km, const char *str, event_type_t e)
{
  keymap_entry_t *ke;
  prop_t *p;

  if(km == NULL)
    km = &km_global;

  hts_mutex_lock(&km->km_mutex);

  ke = malloc(sizeof(keymap_entry_t));
  ke->ke_keycode = strdup(str);
  ke->ke_event = e;
  LIST_INSERT_HEAD(&km->km_entries, ke, ke_link);

  ke->ke_prop =  prop_create(NULL, NULL);
  prop_set_string(prop_create(ke->ke_prop, "type"), "keymapentry");

  p = prop_create(ke->ke_prop, "keycode");
  prop_set_string(p, str);
  ke->ke_sub_keycode = prop_subscribe(p, NULL, km_set_code, ke, NULL, 0);

  p = prop_create(ke->ke_prop, "event");
  prop_set_string(p, event_code2str(e));
  ke->ke_sub_event = prop_subscribe(p, NULL, km_set_event, ke, NULL, 0);


  prop_set_parent_ex(ke->ke_prop, prop_create(km->km_settings, "nodes"),
		     km->km_subscription);

  hts_mutex_unlock(&km->km_mutex);
}


/**
 *
 */
static void 
km_subscribe_callback(struct prop_sub *sub, prop_event_t event, ...)
{
  prop_t *p;
  keymap_t *km = sub->hps_opaque;

  va_list ap;
  va_start(ap, event);

  p = va_arg(ap, prop_t *);

  switch(event) {
  default:
    break;

  case PROP_REQ_NEW_CHILD:
    keymapper_entry_add(km, "<unset>", EVENT_NONE);
    break;

  case PROP_REQ_DELETE:
    prop_destroy(p);
    break;
  }

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
    km = &km_global;

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
void
keymapper_init(keymap_t *km, prop_t *settingsparent, const char *title)
{
  if(km == NULL)
    km = &km_global;

  LIST_INIT(&km->km_entries);
  hts_mutex_init(&km->km_mutex);

  km->km_settings = settings_add_dir(settingsparent, "keymap", title,
				     "keymap");

  prop_set_int(prop_create(km->km_settings, "mayadd"), 1);

  km->km_subscription = 
    prop_subscribe(prop_create(km->km_settings, "nodes"), NULL,
		   km_subscribe_callback, km, NULL, 0);

  keymapper_entry_add(km, "x11-hehe", EVENT_MAINMENU);
}
