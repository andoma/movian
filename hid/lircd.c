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
#include "event.h"
#include "coms.h"
#include "showtime.h"
#include "hid/keymapper.h"


static const struct {
  const char *name;
  event_type_t key;
} lircmap[] = {
  { "Up",           GEV_UP    },
  { "Down",         GEV_DOWN  },
  { "Left",         GEV_LEFT  },
  { "Right",        GEV_RIGHT },
  { "Enter",        GEV_ENTER },
  { "Back",         EVENT_KEY_BACK  },
  { "Backspace",    EVENT_KEY_BACK  },
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

  while(1) {
    fp = fopen("/dev/lircd", "r+");
    if(fp == NULL) {
      sleep(1);
      continue;
    }

    while(fgets(buf, sizeof(buf), fp) != NULL) {
      sscanf(buf, "%"PRIx64" %d %s", &ircode, &repeat, keyname);


      if(keyname[0] && keyname[1] == 0) {
	/* ASCII input */
	event_post(glw_event_create_unicode(keyname[0]));
	continue;
      }

      for(i = 0; i < sizeof(lircmap) / sizeof(lircmap[0]); i++) {
	if(!strcasecmp(keyname, lircmap[i].name)) {
	  event_post_simple(lircmap[i].key);
	}
      }

      if(i == sizeof(lircmap) / sizeof(lircmap[0])) {
	/* No hit, send to keymapper */
	snprintf(buf, sizeof(buf), "lirc - %s", keyname);
	keymapper_resolve(buf);
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
