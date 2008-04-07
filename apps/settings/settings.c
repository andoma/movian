/*
 *  Application launcher
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "settings.h"
#include "hid/keymapper.h"
#include "layout/layout.h"
#include "layout/layout_forms.h"
#include "libhts/hts_strtab.h"

static void settings_update_keyevent(hid_keycode_t *hkc, glw_t *w);

static glw_t *keymap_list;
static appi_t *settings_appi;

/**
 * Based on the keycode, return a descriptive text
 */
static struct strtab keycodenames[] = {
  { "Up",                    INPUT_KEY_UP },
  { "Down",                  INPUT_KEY_DOWN },
  { "Left",                  INPUT_KEY_LEFT },
  { "Right",                 INPUT_KEY_RIGHT },
  { "Previous",              INPUT_KEY_PREV },
  { "Next",                  INPUT_KEY_NEXT },
  { "Close",                 INPUT_KEY_CLOSE },
  { "Stop",                  INPUT_KEY_STOP },
  { "PlayPause",             INPUT_KEY_PLAYPAUSE },
  { "Play",                  INPUT_KEY_PLAY },
  { "Pause",                 INPUT_KEY_PAUSE },
  { "VolumeUp",              INPUT_KEY_VOLUME_UP },
  { "VolumeDown",            INPUT_KEY_VOLUME_DOWN },
  { "VolumeMute",            INPUT_KEY_VOLUME_MUTE },
  { "SettingsMenu",          INPUT_KEY_MENU },
  { "Back",                  INPUT_KEY_BACK },
  { "Enter",                 INPUT_KEY_ENTER },
  { "Select",                INPUT_KEY_SELECT },
  { "Eject",                 INPUT_KEY_EJECT },
  { "Power",                 INPUT_KEY_POWER },
  { "SeekForward",           INPUT_KEY_SEEK_FORWARD },
  { "SeekReverse",           INPUT_KEY_SEEK_BACKWARD },
  { "Quit",                  INPUT_KEY_QUIT },
  { "TaskSwitcher",          INPUT_KEY_TASK_SWITCHER },
  { "TaskSwitcherSwitch",    INPUT_KEY_TASK_DOSWITCH },
  { "ChangeView",            INPUT_KEY_SWITCH_VIEW },
};

const char *
keycode2str(input_key_t code)
{
  return val2str(code, keycodenames);
}

input_key_t
keystr2code(const char *str)
{
  return str2val(str, keycodenames);
}


static int ih_keymapper(inputevent_t *ie);

/**
 * Settings main thread
 */
static void *
settings_thread(void *aux)
{
  struct layout_form_entry_list lfelist;
  appi_t *ai;
  glw_t *mini, *m, *t, *l, *x, *y;
  ic_t *ic;
  int i;
  hid_keycode_t *hkc;

  keymapper_load();

  inputhandler_register(10000, ih_keymapper);

  settings_appi = ai = appi_create("Settings");

  ic = &ai->ai_ic;

  ai->ai_widget = glw_create(GLW_CUBESTACK,
			     NULL);
  mini = 
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "settings/miniature",
	       NULL);

  //  inputhandler_register(199, launcher_input_event);
  //  glw_focus_set(&ai->ai_gfs, list);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, ai->ai_widget,
		 GLW_ATTRIB_FILENAME, "settings/root",
		 NULL);

  TAILQ_INIT(&lfelist);
  LFE_ADD(&lfelist, "settings_list");

  /**
   * Keyboard mapper
   */

  t = layout_form_add_tab(m,
			  "settings_list",     "settings/keymap-icon",
			  "settings_container","settings/keymap-tab");
  if(t != NULL) {
    l = glw_find_by_id(t, "keymap_list", 0);
    if(l != NULL) {
      keymap_list = l;
      for(i = 0; i < sizeof(keycodenames) / sizeof(keycodenames[0]); i++) {
	x = glw_create(GLW_MODEL,
		       GLW_ATTRIB_FILENAME, "settings/keymap-entry",
		       GLW_ATTRIB_U32, keycodenames[i].val,
		       GLW_ATTRIB_PARENT, l,
		       NULL);
	if(x == NULL) 
	  break;

	if((y = glw_find_by_id(x, "mapping_name", 0)) != NULL)
	  glw_set(y,
		  GLW_ATTRIB_CAPTION, keycodenames[i].str,
		  NULL);

	hkc = keymapper_find_by_code(keycodenames[i].val);
	if(hkc != NULL)
	  settings_update_keyevent(hkc, x);
      }
    }
    LFE_ADD(&lfelist, "keymap_list");
  }

  layout_form_initialize(&lfelist, m, &ai->ai_gfs, ic, 1);


  layout_switcher_appi_add(ai, mini);

  while(1) {
    pause();
  }

}





/**
 *
 */
void
settings_init(void)
{
  pthread_t ptid;
  pthread_attr_t attr;

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, NULL, settings_thread, NULL);
}




/**
 *
 */
static int
ih_keymapper(inputevent_t *ie)
{
  hid_keydesc_t *hkd;
  hid_keycode_t *hkc;
  glw_t *w;

  if(!settings_appi->ai_active || keymap_list == NULL ||
     !glw_is_tree_focused(keymap_list))
    return 0;

  if(ie->type == INPUT_KEY) {
    switch(ie->u.key) {
    case INPUT_KEY_UP:
    case INPUT_KEY_DOWN:
    case INPUT_KEY_LEFT:
    case INPUT_KEY_RIGHT:
    case INPUT_KEY_ENTER:
    case INPUT_KEY_TASK_DOSWITCH:
      return 0; /* Pass thru those events */
    default:
      return 1; /* Block anything else */
    }
  }

  if(ie->type != INPUT_KEYDESC)
    return 0;

  if((w = keymap_list->glw_selected) == NULL)
    return 0;

  hkc = keymapper_find_by_code(w->glw_u32);
  hkd = keymapper_find_by_desc(ie->u.keydesc);

  if(hkd->hkd_hkc == hkc)
    return 1; /* No change */
 
  if(hkd->hkd_hkc != NULL) {
    /* Remove previous mapping */
    LIST_REMOVE(hkd, hkd_keycode_link);
    settings_update_keyevent(hkd->hkd_hkc, NULL);
  }

  hkd->hkd_hkc = hkc;
  LIST_INSERT_HEAD(&hkc->hkc_descs, hkd, hkd_keycode_link);

  settings_update_keyevent(hkc, w);
  keymapper_save();
  return 0;
}

/**
 *
 */
static void 
settings_update_keyevent(hid_keycode_t *hkc, glw_t *w)
{
  char buf[100];
  hid_keydesc_t *hkd;

  if(w == NULL) {
    TAILQ_FOREACH(w, &keymap_list->glw_childs, glw_parent_link)
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

