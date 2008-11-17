/*
 *  Common HID functions
 *  Copyright (C) 2007 Andreas Öman
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
#include <fcntl.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include "ui/glw/glw.h"

#include "showtime.h"

#include "lircd.h"
#include "imonpad.h"
#include "hid.h"

hid_ir_mode_t hid_ir_mode;
extern prop_t *prop_global;

static prop_t *prop_hid_status;

/**
 *
 */
static void *
hid_thread(void *aux)
{

  while(1) {
    switch(hid_ir_mode) {
    case HID_IR_NONE:
      prop_set_string(prop_hid_status, "OK");
      sleep(1);
      continue;
      
    case HID_IR_LIRC:
      lircd_proc(prop_hid_status);
      break;

    case HID_IR_IMONPAD:
      imonpad_proc(prop_hid_status);
      break;
    }
  }
  return NULL;
}

#if 0

/**
 *
 */
static void
hid_save(void)
{
  htsmsg_t *m = htsmsg_create();
  htsmsg_add_u32(m, "rcmode", hid_ir_mode);
  hts_settings_save(m, "hid");
  htsmsg_destroy(m);
}

/**
 *
 */
static void
hid_mode_cb(void *opaque, void *opaque2, int value)
{
  hid_ir_mode = value;
  hid_save();
}
#endif


/**
 *
 */
void
hid_init(glw_t *m)
{
  hts_thread_t tid;
  htsmsg_t *s;
  uint32_t u32;
  glw_t *sel, *icon, *tab;

  prop_t *prop_hid_root = prop_create(NULL, "hid");

  prop_hid_status = prop_create(prop_hid_root, "status");

  icon = glw_model_create(NULL, "theme://settings/hid/hid-icon.model",
			  NULL, 0, NULL);

  tab  = glw_model_create(NULL, "theme://settings/hid/hid.model",
			  NULL, 0, prop_hid_root);
 
  if((s = hts_settings_load("hid")) != NULL) {
    if(!htsmsg_get_u32(s, "rcmode", &u32))
      hid_ir_mode = u32;
    htsmsg_destroy(s);
  }

  
  if((sel = glw_find_by_id(tab, "rcmode", 0)) == NULL)
    return;
#if 0
  glw_selection_add_text_option(sel, "No remote", hid_mode_cb,
				NULL, NULL, HID_IR_NONE,
				HID_IR_NONE == hid_ir_mode);

  glw_selection_add_text_option(sel, "LIRC", hid_mode_cb,
				NULL, NULL, HID_IR_LIRC,
				HID_IR_LIRC == hid_ir_mode);

  glw_selection_add_text_option(sel, "iMon Pad", hid_mode_cb,
				NULL, NULL, HID_IR_IMONPAD,
				HID_IR_IMONPAD == hid_ir_mode);
#endif

  //  glw_add_tab(m, "settings_list", icon, "settings_deck", tab);

  hts_thread_create(&tid, hid_thread, NULL);
}
