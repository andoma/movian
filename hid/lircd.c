/*
 *  Lircd interface
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "lircd.h"
#include "input.h"
#include "coms.h"
#include "showtime.h"
#include "hid/keymapper.h"


static const struct {
  const char *name;
  input_key_t key;
} lircmap[] = {
  { "Up",           INPUT_KEY_UP    },
  { "Down",         INPUT_KEY_DOWN  },
  { "Left",         INPUT_KEY_LEFT  },
  { "Right",        INPUT_KEY_RIGHT },
  { "Enter",        INPUT_KEY_ENTER },
  { "Back",         INPUT_KEY_BACK  },
  { "Backspace",    INPUT_KEY_BACK  },
};

static void *
lirc_thread(void *aux)
{
  FILE *fp;
  char buf[200];
  uint64_t ircode;
  uint32_t repeat;
  char keyname[100];
  int i;
  inputevent_t ie;

  while(1) {
    fp = fopen("/dev/lircd", "r+");
    if(fp == NULL) {
      sleep(1);
      continue;
    }

    while(fgets(buf, sizeof(buf), fp) != NULL) {
      sscanf(buf, "%"PRIx64" %d %s", &ircode, &repeat, keyname);


      if(keyname[0] && keyname[1] == 0) {
	input_key_down(keyname[0]); /* ASCII input */
	continue;
      }

      for(i = 0; i < sizeof(lircmap) / sizeof(lircmap[0]); i++) {
	if(!strcasecmp(keyname, lircmap[i].name)) {
	  input_key_down(lircmap[i].key);
	}
      }

      if(i == sizeof(lircmap) / sizeof(lircmap[0])) {
	/* No hit, build a keydesc */
	ie.type = INPUT_KEYDESC;
	snprintf(ie.u.keydesc, sizeof(ie.u.keydesc),
		 "lirc - %s", keyname);
	keymapper_resolve(&ie);
      }
    }
    fclose(fp);
  }
}


void
lircd_init(void)
{
  static pthread_t ptid;
  pthread_create(&ptid, NULL, lirc_thread, NULL);
}
