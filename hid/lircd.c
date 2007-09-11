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


static const struct {
  const char *name;
  input_key_t key;
} lircmap[] = {
  { "Vol+", INPUT_KEY_VOLUME_UP },
  { "Vol-", INPUT_KEY_VOLUME_DOWN },
  { "Mute", INPUT_KEY_VOLUME_MUTE },
  { "Eject", INPUT_KEY_EJECT },
  { "Backspace", INPUT_KEY_BACK },
  { "Enter", INPUT_KEY_ENTER },
  { "Ch+", INPUT_KEY_CHANNEL_PLUS },
  { "Ch-", INPUT_KEY_CHANNEL_MINUS },
  { "Play", INPUT_KEY_PLAY },
  { "Pause", INPUT_KEY_PAUSE },
  { "Stop", INPUT_KEY_STOP },
  { "Power", INPUT_KEY_POWER },
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
    fp = open_fd_tcp("127.0.0.1", 8765, NULL, NULL);
    if(fp == NULL) {
      sleep(1);
      continue;
    }

    while(fgets(buf, sizeof(buf), fp) != NULL) {
      sscanf(buf, "%"PRIx64" %d %s", &ircode, &repeat, keyname);
      
      for(i = 0; i < sizeof(lircmap) / sizeof(lircmap[0]); i++) {
	if(!strcasecmp(keyname, lircmap[i].name)) {
	  input_key_down(lircmap[i].key);
	}
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
