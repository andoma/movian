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
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <libhts/hts_strtab.h>
#include "showtime.h"
#include "keymapper.h"

static glw_t *keymapper_list;
//static appi_t *keymapper_appi;

/**
 * Based on the keycode, return a descriptive text
 */
static struct strtab keycodenames[] = {
  { "Up",                    GEV_UP },
  { "Down",                  GEV_DOWN },
  { "Left",                  GEV_LEFT },
  { "Right",                 GEV_RIGHT },
  { "Enter",                 GEV_ENTER },
  { "Close",                 EVENT_KEY_CLOSE },
  { "Stop",                  EVENT_KEY_STOP },
  { "PlayPause",             EVENT_KEY_PLAYPAUSE },
  { "Play",                  EVENT_KEY_PLAY },
  { "Pause",                 EVENT_KEY_PAUSE },
  { "VolumeUp",              EVENT_KEY_VOLUME_UP },
  { "VolumeDown",            EVENT_KEY_VOLUME_DOWN },
  { "VolumeMuteToggle",      EVENT_KEY_VOLUME_MUTE_TOGGLE },
  { "Menu",                  EVENT_KEY_MENU },
  { "Back",                  GEV_BACKSPACE },
  { "Select",                EVENT_KEY_SELECT },
  { "Eject",                 EVENT_KEY_EJECT },
  { "Power",                 EVENT_KEY_POWER },
  { "Previous",              EVENT_KEY_PREV },
  { "Next",                  EVENT_KEY_NEXT },
  { "SeekForward",           EVENT_KEY_SEEK_FORWARD },
  { "SeekReverse",           EVENT_KEY_SEEK_BACKWARD },
  { "Quit",                  EVENT_KEY_QUIT },
  { "MainMenu",              EVENT_KEY_MAINMENU },
  { "ChangeView",            EVENT_KEY_SWITCH_VIEW },
  { "Channel+",              EVENT_KEY_CHANNEL_PLUS },
  { "Channel-",              EVENT_KEY_CHANNEL_MINUS },
};

const char *
keycode2str(event_type_t code)
{
  return val2str(code, keycodenames);
}

event_type_t
keystr2code(const char *str)
{
  return str2val(str, keycodenames);
}


#define KEYDESC_HASH_SIZE 101

static unsigned int
keydesc_hashstr(const char *s)
{
  unsigned int hash = 0;
  unsigned char c;
  while(*s) {
    c = *s++;
    hash += (c << 5) + c;
  }
  return hash % KEYDESC_HASH_SIZE;
}

static struct hid_keycode_list keycodes;
static struct hid_keydesc_list keydescs[KEYDESC_HASH_SIZE];

/**
 *
 */
static void
keymapper_post_string(const char *str)
{
  event_keydesc_t *ek;
  int l = strlen(str);

  ek = glw_event_create(EVENT_KEYDESC, sizeof(event_keydesc_t) + l + 1);
  memcpy(ek->desc, str, l);
  ek->desc[l] = 0;
  event_post(&ek->h);
}

/**
 * Resolve a keydesc into a keycode
 */
void
keymapper_resolve(const char *str)
{
  unsigned int hash = keydesc_hashstr(str);
  hid_keydesc_t *hkd;
  hid_keycode_t *hkc;
 
  keymapper_post_string(str);

  LIST_FOREACH(hkd, &keydescs[hash], hkd_hash_link)
    if(!strcmp(hkd->hkd_desc, str))
      break;

  if(hkd == NULL)
    return;

  hkc = hkd->hkd_hkc;
  event_post_simple(hkc->hkc_code);
}


/**
 *
 */
hid_keycode_t *
keymapper_find_by_code(event_type_t val)
{
  hid_keycode_t *hkc;

  LIST_FOREACH(hkc, &keycodes, hkc_link) 
    if(hkc->hkc_code == val)
      break;

  if(hkc == NULL) {
    hkc = malloc(sizeof(hid_keycode_t));
    hkc->hkc_code = val;
    LIST_INIT(&hkc->hkc_descs);
    LIST_INSERT_HEAD(&keycodes, hkc, hkc_link);
  }
  return hkc;
}


/**
 *
 */
hid_keydesc_t *
keymapper_find_by_desc(const char *str)
{
  unsigned int hash = keydesc_hashstr(str);
  hid_keydesc_t *hkd;

  LIST_FOREACH(hkd, &keydescs[hash], hkd_hash_link)
    if(!strcmp(hkd->hkd_desc, str))
      break;

  if(hkd == NULL) {
    hkd = malloc(sizeof(hid_keydesc_t));
    hkd->hkd_desc = strdup(str);
    hkd->hkd_hkc = NULL;
    LIST_INSERT_HEAD(&keydescs[hash], hkd, hkd_hash_link);
  }
  return hkd;
}


/**
 *
 */
void
keymapper_map(hid_keydesc_t *hkd, hid_keycode_t *hkc)
{
  if(hkd->hkd_hkc != NULL)
    LIST_REMOVE(hkd, hkd_keycode_link);

  hkd->hkd_hkc = hkc;
  LIST_INSERT_HEAD(&hkc->hkc_descs, hkd, hkd_keycode_link);
}

/**
 *
 */
static void
keymapper_save(void)
{
  hid_keycode_t *hkc;
  hid_keydesc_t *hkd;
  const char *codename;

  htsmsg_t *settings = htsmsg_create();
  htsmsg_t *simple = htsmsg_create();

  LIST_FOREACH(hkc, &keycodes, hkc_link) {
    if((codename = keycode2str(hkc->hkc_code)) == NULL)
      continue;
    
    LIST_FOREACH(hkd, &hkc->hkc_descs, hkd_keycode_link) 
      htsmsg_add_str(simple, codename, hkd->hkd_desc);
  }

  htsmsg_add_msg(settings, "simple", simple);

  hts_settings_save(settings, "keymap");
  htsmsg_destroy(settings);
}


/**
 *
 */
static void
keymapper_add_mapping(const char *event, const char *keycode)
{
  event_type_t val;
  hid_keycode_t *hkc;
  hid_keydesc_t *hkd;

  if((val = keystr2code(event)) == -1)
    return;

  hkc = keymapper_find_by_code(val);
  hkd = keymapper_find_by_desc(keycode);
  keymapper_map(hkd, hkc);
}

/**
 *
 */
static void
keymapper_set_default(void)
{
  keymapper_add_mapping("ChangeView", "x11 - F3");
  keymapper_add_mapping("VolumeMuteToggle", "x11 - F7");
  keymapper_add_mapping("VolumeDown", "x11 - F5");
  keymapper_add_mapping("VolumeUp", "x11 - F6");
  keymapper_add_mapping("Next", "x11 - Next");
  keymapper_add_mapping("Previous", "x11 - Prior");
  keymapper_add_mapping("PlayPause", "x11 - F2");
  keymapper_add_mapping("Select", "x11 - space");
  keymapper_add_mapping("SeekForward", "x11 - F12");
  keymapper_add_mapping("SeekReverse", "x11 - F11");
  keymapper_add_mapping("Quit", "x11 - F10");
  keymapper_add_mapping("Stop", "x11 - End");
  keymapper_add_mapping("Menu", "x11 - F1");
  keymapper_add_mapping("MainMenu", "x11 - Menu");
}


/**
 *
 */
static void
keymapper_load(void)
{
  htsmsg_t *settings;
  htsmsg_t *simple;
  htsmsg_field_t *f;
  hid_keycode_t *hkc;
  hid_keydesc_t *hkd;
  event_type_t val;

  if((settings = hts_settings_load("keymap")) == NULL) {
    keymapper_set_default();
    return;
  }
  if((simple = htsmsg_get_msg(settings, "simple")) != NULL) {
    HTSMSG_FOREACH(f, simple) {
      if(f->hmf_type != HMF_STR)
	continue;

      if((val = keystr2code(f->hmf_name)) == -1)
	continue;
      
      hkc = keymapper_find_by_code(val);
      hkd = keymapper_find_by_desc(f->hmf_str);

      keymapper_map(hkd, hkc);
    }
  }
}


/**
 *
 */
static void 
keymapper_update_model(hid_keycode_t *hkc, glw_t *w)
{
  char buf[100];
  hid_keydesc_t *hkd;

  if(w == NULL) {
    TAILQ_FOREACH(w, &keymapper_list->glw_childs, glw_parent_link)
      if(w->glw_u32 == hkc->hkc_code)
	break;
    if(w == NULL)
      return;
  }

  if((w = glw_find_by_id(w, "mapping_source", 0)) == NULL)
    return;

  buf[0] = 0;
  LIST_FOREACH(hkd, &hkc->hkc_descs, hkd_keycode_link)
    snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
	     "%s%s", hkd->hkd_desc, 
	     LIST_NEXT(hkd, hkd_keycode_link) ? ", " : "");

  glw_set(w, 
	  GLW_ATTRIB_CAPTION, buf,
	  NULL);
}

/**
 *
 */
static int
eh_keymapper(glw_event_t *ge, void *opaque)
{
  hid_keydesc_t *hkd;
  hid_keycode_t *hkc;
  glw_t *w;
  event_keydesc_t *ekd;
#if 0
  if(keymapper_appi == NULL || !keymapper_appi->ai_active || 
     !glw_is_selected(keymapper_list))
    return 0;
#endif

  switch(ge->ge_type) {
  case GEV_UP:
  case GEV_DOWN:
  case GEV_LEFT:
  case GEV_RIGHT:
  case GEV_ENTER:
    return 0; /* Pass thru those events */

  default:
    return 1;

  case EVENT_KEYDESC:
    break;
  }

  if((w = keymapper_list->glw_selected) == NULL)
    return 0;

  ekd = (void *)ge;

  hkc = keymapper_find_by_code(w->glw_u32);
  hkd = keymapper_find_by_desc(ekd->desc);

  if(hkd->hkd_hkc == hkc)
    return 1; /* No change */
 
  if(hkd->hkd_hkc != NULL) {
    /* Remove previous mapping */
    LIST_REMOVE(hkd, hkd_keycode_link);
    keymapper_update_model(hkd->hkd_hkc, NULL);
  }

  hkd->hkd_hkc = hkc;
  LIST_INSERT_HEAD(&hkc->hkc_descs, hkd, hkd_keycode_link);

  keymapper_update_model(hkc, w);
  keymapper_save();
  return 0;
}


/**
 *
 */
void
keymapper_init(glw_t *settings)
{
#if 0
  glw_t *icon = 
    glw_model_create("theme://settings/keymapper/keymapper-icon.model", NULL,
		     0, NULL);
#endif
  glw_t *tab  = 
    glw_model_create("theme://settings/keymapper/keymapper.model", NULL,
		     0, NULL);
  glw_t *l, *e, *y;
  int i;
  hid_keycode_t *hkc;

  //glw_add_tab(settings, "settings_list", icon, "settings_deck", tab);

  keymapper_load();

  if((l = glw_find_by_id(tab, "keymapper_list", 0)) == NULL)
    return;

  //  keymapper_appi = ai;
  keymapper_list = l;

  for(i = 0; i < sizeof(keycodenames) / sizeof(keycodenames[0]); i++) {

    e = glw_model_create("theme://settings/keymapper/entry.model", l,
			 0, NULL);

    glw_set(e, GLW_ATTRIB_U32, keycodenames[i].val, NULL);

    if((y = glw_find_by_id(e, "mapping_name", 0)) != NULL) {
      glw_set(y,
	      GLW_ATTRIB_CAPTION, keycodenames[i].str,
	      NULL);
      
      hkc = keymapper_find_by_code(keycodenames[i].val);
      if(hkc != NULL)
	keymapper_update_model(hkc, e);
    }
  }

  event_handler_register("keymapper", eh_keymapper, EVENTPRI_KEYMAPPER,
			 NULL);

}
