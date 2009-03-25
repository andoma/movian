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

#include "showtime.h"

#include "lircd.h"
#include "imonpad.h"

hid_ir_mode_t hid_ir_mode;

/**
 *
 */
static void *
hid_thread(void *aux)
{

  while(1) {
    switch(hid_ir_mode) {
    case HID_IR_NONE:
      sleep(1);
      continue;
      
    case HID_IR_LIRC:
      lircd_proc();
      break;

    case HID_IR_IMONPAD:
      imonpad_proc();
      break;
    }
  }
  return NULL;
}

/**
 *
 */
void
hid_init(void)
{
  hts_thread_t tid;
 
  hts_thread_create(&tid, hid_thread, NULL);
}
