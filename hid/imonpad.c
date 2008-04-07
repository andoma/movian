/*
 *  Driver for iMON's pad controller
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
#include <stdarg.h>
#include <math.h>

#include "imonpad.h"
#include "input.h"
#include "showtime.h"
#include "hid/keymapper.h"

/*
 * iMON PAD native decoder
 */

static const struct {
  const char *name;
  uint32_t code;
  input_key_t key;
} imonpadmap[] = {
  {"AppExit",            0x288195B7, 0 },
  {"Record",             0x298115B7, 0 },
  {"Play",               0x2A8115B7, 0 }, 
  {"SlowMotion",         0x29B195B7, 0 },
  {"Rewind",             0x2A8195B7, 0 },
  {"Pause",              0x2A9115B7, 0 },
  {"FastForward",        0x2B8115B7, 0 },
  {"PrevChapter",        0x2B9115B7, 0 },
  {"Stop",               0x2B9715B7, 0 },
  {"NextChapter",        0x298195B7, 0 },
  {"Esc",                0x2BB715B7, 0 },
  {"Eject",              0x299395B7, 0 },
  {"AppLauncher",        0x29B715B7, 0 },
  {"MultiMon",           0x2AB195B7, 0 },
  {"TaskSwitcher",       0x2A9395B7, INPUT_KEY_TASK_DOSWITCH },
  {"Mute",               0x2B9595B7, 0 },
  {"Vol+",               0x28A395B7, 0 },
  {"Vol-",               0x28A595B7, 0 },
  {"Ch+",                0x289395B7, 0 },
  {"Ch-",                0x288795B7, 0 },
  {"Timer",              0x2B8395B7, 0 },
  {"1",                  0x28B595B7, 0 },
  {"2",                  0x2BB195B7, 0 },
  {"3",                  0x28B195B7, 0 },
  {"4",                  0x2A8595B7, 0 },
  {"5",                  0x299595B7, 0 },
  {"6",                  0x2AA595B7, 0 },
  {"7",                  0x2B9395B7, 0 },
  {"8",                  0x2A8515B7, 0 },
  {"9",                  0x2AA115B7, 0 },
  {"0",                  0x2BA595B7, 0 },
  {"ShiftTab",           0x28B515B7, 0 },
  {"Tab",                0x29A115B7, 0 },
  {"MyMovie",            0x2B8515B7, 0 },
  {"MyMusic",            0x299195B7, 0 },
  {"MyPhoto",            0x2BA115B7, 0 },
  {"MyTV",               0x28A515B7, 0 },
  {"Bookmark",           0x288515B7, 0 },
  {"Thumbnail",          0x2AB715B7, 0 },
  {"AspectRatio",        0x29A595B7, 0 },
  {"FullScreen",         0x2AA395B7, 0 },
  {"MyDVD",              0x29A295B7, 0 },
  {"Menu",               0x2BA385B7, 0 },
  {"Caption",            0x298595B7, 0 },
  {"Language",           0x2B8595B7, 0 },
  {"MouseKeyboard",      0x299115B7, 0 },
  {"SelectSpace",        0x2A9315B7, 0 },
  {"MouseMenu",          0x28B715B7, 0 },
  {"MouseRightClick",    0x688481B7, 0 },
  {"Enter",              0x28A195B7, INPUT_KEY_ENTER },
  {"MouseLeftClick",     0x688301B7, 0 },
  {"WindowsKey",         0x2B8195B7, 0 },
  {"Backspace",          0x28A115B7, INPUT_KEY_BACK },
};

static int repeat_rate;
static int repeat_rate0;

#define REPEAT_RATE_SLOWEST 1000000

static void *
imonpad_compute_repeat_rate(void *aux)
{
  repeat_rate = repeat_rate0 = REPEAT_RATE_SLOWEST;

  while(1) {
    usleep(100000);
    repeat_rate = (repeat_rate * 7 + repeat_rate0) / 8;
  }
}






static void *
imonpad_thread(void *aux)
{
  int fd = -1, i, l;
  uint8_t buf[4];
  uint32_t v;
  int dx, dy;
  inputevent_t ie;
  float angle;
  int64_t last_nav_generated_ts = 0, ts, delta;
  int64_t last_nav_sens_ts = 0;
  int k;

  while(1) {
    if(fd == -1) {
      fd = open("/dev/lirc0", O_RDONLY);
      
      if(fd == -1) {
	sleep(1);
	continue;
      }
    }

    if(read(fd, buf, 4) != 4) {
      close(fd);
      fd = -1;
      continue;
    }

    v = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];
    
    if(buf[0] & 0x40 && buf[3] == 0xb7) {
      dx = (buf[1] & 0x08) | (buf[1] & 0x10) >> 2 | 
	(buf[1] & 0x20) >> 4 | (buf[1] & 0x40) >> 6;
      dy = (buf[2] & 0x08) | (buf[2] & 0x10) >> 2 | 
	(buf[2] & 0x20) >> 4 | (buf[2] & 0x40) >> 6;

      if(buf[0] & 0x02) dx |= ~0x10+1;
      if(buf[0] & 0x01) dy |= ~0x10+1;

      ie.u.xy.x = dx;
      ie.u.xy.y = dy;
      ie.type = INPUT_PAD;
      input_root_event(&ie);

      /* Compute angle and vector length */

      if(dx < 0) {
	angle = atanf((float)dy / (float)dx);
      } else if(dx == 0) {
	angle = (dy > 0 ? M_PI : 0) + M_PI * 0.5;
      } else {
	angle = M_PI + atanf((float)dy / (float)dx);
      }
      angle *= 360.0f / (M_PI * 2);
      if(angle < 0)
	angle = angle + 360;

      /* Discard too light pressing on the pad,
	 need to do this after the angle has been computed though*/

#define SENS 6
      switch(dx) {
      case -100 ... -SENS - 1:
	dx += SENS;
	break;
      case -SENS ... SENS:
	dx = 0;
	break;
      case SENS + 1 ... 100:
	dx -= SENS;
	break;
      }

      switch(dy) {
      case -100 ... -SENS - 1:
	dy += SENS;
	break;
      case -SENS ... SENS:
	dy = 0;
	break;
      case SENS + 1 ... 100:
	dy -= SENS;
	break;
      }

      l = sqrt(dx * dx + dy * dy);

      /* If zero (user might still touch the pad though)
	 bail out (ie. act as if nothing happens) */

      if(l == 0)
	continue;

      /* Now, the angle will tell us which input event to generate
	 and the vector length will be the repeat rate threshold */

      /* We feed the repeat rate thru a IIR filter */

      l = REPEAT_RATE_SLOWEST - l * 300000;
      if(l < 50000)
	l = 50000;

      repeat_rate0 = l;

      ts = showtime_get_ts();
      delta = ts - last_nav_sens_ts;
      last_nav_sens_ts = ts;

      if(delta > 300000) {
	/* No activity sensed, reset repeat */
	repeat_rate0 = repeat_rate = REPEAT_RATE_SLOWEST;
      }

      delta = ts - last_nav_generated_ts;

#define ALIMIT 30
      k = 0;

      if(angle > 360 - ALIMIT || angle < 0 + ALIMIT)
	k = INPUT_KEY_LEFT;
      
      if(angle > 90 - ALIMIT && angle < 90 + ALIMIT)
	k = INPUT_KEY_UP;
      
      if(angle > 180 - ALIMIT && angle < 180 + ALIMIT)
	k = INPUT_KEY_RIGHT;
      
      if(angle > 270 - ALIMIT && angle < 270 + ALIMIT)
	k = INPUT_KEY_DOWN;

      if(k && delta > repeat_rate0) {
	last_nav_generated_ts = ts;
	input_key_down(k);
	delta -= repeat_rate0;
      }
      continue;
    }

    for(i = 0; i < sizeof(imonpadmap) / sizeof(imonpadmap[0]); i++) {
      if(v == imonpadmap[i].code) {

	if(imonpadmap[i].key)
	  input_key_down(imonpadmap[i].key);
	else {
	  ie.type = INPUT_KEYDESC;
	  snprintf(ie.u.keydesc, sizeof(ie.u.keydesc),
		   "imonpad - %s", imonpadmap[i].name);
	  keymapper_resolve(&ie);
	}
	break;
      }
    }
  }
}






void
imonpad_init(void)
{
  static pthread_t ptid;

  has_analogue_pad = 1;
  pthread_create(&ptid, NULL, imonpad_thread, NULL);
  pthread_create(&ptid, NULL, imonpad_compute_repeat_rate, NULL);
}
