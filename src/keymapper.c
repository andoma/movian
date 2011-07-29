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
#include "event.h"


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



static keymap_t *global_km;


static const keymap_defmap_t default_keymap[] = { 


  { ACTION_PAGE_UP,                "Prior"},
  { ACTION_PAGE_DOWN,              "Next"},

  { ACTION_TOP,                    "Home"},
  { ACTION_BOTTOM,                 "End"},

  { ACTION_NAV_BACK,               "Alt+Left"},
  { ACTION_NAV_FWD,                "Alt+Right"},

  { ACTION_SEEK_BACKWARD,          "Ctrl+Left"},
  { ACTION_SEEK_FORWARD,           "Ctrl+Right"},

  { ACTION_PREV_TRACK,             "Shift+Ctrl+Left"},
  { ACTION_NEXT_TRACK,             "Shift+Ctrl+Right"},

  { ACTION_ZOOM_UI_INCR,           "Ctrl+plus"},
  { ACTION_ZOOM_UI_DECR,           "Ctrl+minus"},

  { ACTION_RELOAD_UI,              "F5"},
  { ACTION_RELOAD_DATA,            "Shift+F5"},
  { ACTION_FULLSCREEN_TOGGLE,      "F11"},

  { ACTION_PLAYPAUSE,              "MediaPlayPause"},
  { ACTION_PLAY,                   "MediaPlay"},
  { ACTION_PAUSE,                  "MediaPause"},
  { ACTION_STOP,                   "MediaStop"},
  { ACTION_EJECT,                  "MediaEject"},
  { ACTION_RECORD,                 "MediaRecord"},

  { ACTION_VOLUME_DOWN,            "MediaLowerVolume"},
  { ACTION_VOLUME_UP,              "MediaRaiseVolume"},
  { ACTION_VOLUME_MUTE_TOGGLE,     "MediaMute"},

  { ACTION_MENU,                   "F1"},
  { ACTION_SHOW_MEDIA_STATS,       "F2"},
  { ACTION_SYSINFO,                "F3"},

  { ACTION_QUIT,                   "Alt+F4"},

  { ACTION_STANDBY,                "Sleep"},

  { ACTION_NONE, NULL}, 
}; 

static hts_mutex_t km_mutex;


/**
 *
 */
static void
km_save(keymap_t *km)
{
  keymap_entry_t *ke;
  htsmsg_t *m = htsmsg_create_map();
  htsmsg_t *l;
  const char *e;

  LIST_FOREACH(ke, &km->km_entries, ke_link) {
    if((e = action_code2str(ke->ke_action)) == NULL)
      continue;
    
    l = htsmsg_create_list();

    if(ke->ke_keycode1)
      htsmsg_add_str(l, NULL, ke->ke_keycode1);

    if(ke->ke_keycode2)
      htsmsg_add_str(l, NULL, ke->ke_keycode2);

    htsmsg_add_msg(m, e, l);
  }

  htsmsg_store_save(m, "keymaps/%s", km->km_name);
  htsmsg_destroy(m);
}


/**
 *
 */
static void 
km_set_code1(void *opaque, prop_event_t event, ...)
{
  keymap_entry_t *ke = opaque;
  const char *str;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_RSTRING) {
    free(ke->ke_keycode1);
    ke->ke_keycode1 = NULL;
  } else {
    str = rstr_get(va_arg(ap, rstr_t *));

    if(ke->ke_keycode1 != NULL && !strcmp(ke->ke_keycode1, str))
      return;

    free(ke->ke_keycode1);
    ke->ke_keycode1 = strdup(str);
  }
  km_save(ke->ke_km);
}


/**
 *
 */
static void 
km_set_code2(void *opaque, prop_event_t event, ...)
{
  keymap_entry_t *ke = opaque;
  const char *str;

  va_list ap;
  va_start(ap, event);

  if(event != PROP_SET_RSTRING) {
    free(ke->ke_keycode2);
    ke->ke_keycode2 = NULL;
  } else {
    str = rstr_get(va_arg(ap, rstr_t *));

    if(ke->ke_keycode2 != NULL && !strcmp(ke->ke_keycode2, str))
      return;

    free(ke->ke_keycode2);
    ke->ke_keycode2 = strdup(str);
  }
  km_save(ke->ke_km);
}



/**
 *
 */
static void
keymapper_entry_add(keymap_t *km, const char *kc1, const char *kc2,
		    const char *eventname, action_type_t a)
{
  keymap_entry_t *ke;
  prop_t *p, *src;
  
  ke = malloc(sizeof(keymap_entry_t));
  ke->ke_km = km;
  ke->ke_keycode1 = kc1 ? strdup(kc1) : NULL;
  ke->ke_keycode2 = kc2 ? strdup(kc2) : NULL;
  ke->ke_action = a;
  LIST_INSERT_HEAD(&km->km_entries, ke, ke_link);

  ke->ke_prop =  prop_create_root(NULL);
  src = prop_create(ke->ke_prop, "model");

  prop_set_string(prop_create(src, "type"), "keymapentry");

  p = prop_create(src, "keycode1");
  prop_set_string(prop_create(src, "keycode1"), kc1);

  ke->ke_sub_keycode = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_CALLBACK, km_set_code1, ke,
		   PROP_TAG_ROOT, p,
		   NULL);


  p = prop_create(src, "keycode2");
  prop_set_string(prop_create(src, "keycode2"), kc2);

  ke->ke_sub_keycode = 
    prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		   PROP_TAG_CALLBACK, km_set_code2, ke,
		   PROP_TAG_ROOT, p,
		   NULL);


  p = prop_create(prop_create(src, "metadata"), "title");
  prop_set_string(p, eventname);

  if(prop_set_parent(ke->ke_prop, prop_create(km->km_settings, "nodes")))
    abort();
}


/**
 *
 */
static void
keymapper_create_entries(keymap_t *km, const keymap_defmap_t *def)
{
  action_type_t e;
  htsmsg_t *m;
  const char *eventname;
  const char *kc1, *kc2;
  int i;

  m = htsmsg_store_load("keymaps/%s", km->km_name);
  for(e = ACTION_mappable_begin + 1; e < ACTION_mappable_end; e++) {
    if((eventname = action_code2str(e)) == NULL) {
      fprintf(stderr, "Action %d (0x%x) lacks string representation\n",
	      e, e);
      continue;
    }
    kc1 = kc2 = NULL;

    if(m != NULL) {

      htsmsg_t *l = htsmsg_get_list(m, eventname);

      if(l == NULL) {
	kc1 = htsmsg_get_str(m, eventname);
      } else {
	htsmsg_field_t *f = TAILQ_FIRST(&l->hm_fields);
	if(f && f->hmf_type == HMF_STR) {
	  kc1 = f->hmf_str;
	  f = TAILQ_NEXT(f, hmf_link);
	}
	if(f && f->hmf_type == HMF_STR)
	  kc2 = f->hmf_str;
      }
    } else if(def != NULL) {
      for(i = 0; def[i].kd_keycode != NULL; i++)
	if(def[i].kd_action == e) {
	  kc1 = def[i].kd_keycode;
	  break;
	}
    }
    keymapper_entry_add(km, kc1, kc2, eventname, e);
  }
  htsmsg_destroy(m);
}


/**
 *
 */
static event_t *
keymapper_resolve0(keymap_t *km, const char *str)
{
  keymap_entry_t *ke;

#define MAX_ACTIONS 32

  action_type_t vec[MAX_ACTIONS];
  int vecptr = 0;

  LIST_FOREACH(ke, &km->km_entries, ke_link) {
    if(ke->ke_keycode1 != NULL && !strcasecmp(str, ke->ke_keycode1) &&
       vecptr < MAX_ACTIONS)
      vec[vecptr++] = ke->ke_action;
    else if(ke->ke_keycode2 != NULL && !strcasecmp(str, ke->ke_keycode2) &&
	    vecptr < MAX_ACTIONS)
      vec[vecptr++] = ke->ke_action;
  }

  if(vecptr == 0)
    return NULL;

  return event_create_action_multi(vec, vecptr);
}


/**
 *
 */
event_t *
keymapper_resolve(const char *str)
{
  event_t *e;
  hts_mutex_lock(&km_mutex);
  e = keymapper_resolve0(global_km, str);
  hts_mutex_unlock(&km_mutex);
  return e;
}


/**
 *
 */
static keymap_t *
keymapper_create(prop_t *settingsparent, const char *name, prop_t *title,
		 const keymap_defmap_t *def)
{
  keymap_t *km;
  prop_t *desc = _p("Configure mapping of keyboard and remote controller keys");
  km = calloc(1, sizeof(keymap_t));

  LIST_INIT(&km->km_entries);

  km->km_name = strdup(name);
  km->km_settings =
    prop_create(settings_add_dir(settingsparent, title,
				 "keymap", NULL, desc),
		"model");

  keymapper_create_entries(km, def);
  return km;
}


/**
 *
 */
void
keymapper_init(void)
{
  hts_mutex_init(&km_mutex);
  global_km = keymapper_create(NULL, "global", _p("Keymapper"), default_keymap);
}
