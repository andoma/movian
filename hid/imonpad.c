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

/*
 * iMON PAD native decoder
 */

static const struct {
  uint32_t code;
  input_key_t key;
} imonpadmap[] = {
  { 0x288195b7, INPUT_KEY_QUIT },
  { 0x28A395B7, INPUT_KEY_VOLUME_UP },
  { 0x28A595B7, INPUT_KEY_VOLUME_DOWN },
  { 0x2B9595B7, INPUT_KEY_VOLUME_MUTE },
  { 0x299395B7, INPUT_KEY_EJECT },
  { 0x28A115B7, INPUT_KEY_BACK },
  { 0x28A195B7, INPUT_KEY_ENTER },
  { 0x289395B7, INPUT_KEY_CHANNEL_PLUS },
  { 0x288795B7, INPUT_KEY_CHANNEL_MINUS },
  { 0x2A8115B7, INPUT_KEY_PLAY },
  { 0x2A9115B7, INPUT_KEY_PAUSE },
  { 0x2B9715B7, INPUT_KEY_STOP },
  { 0x289155B7, INPUT_KEY_POWER },
  { 0x2b8515b7, INPUT_KEY_GOTO_MOVIES },
  { 0x299195b7, INPUT_KEY_GOTO_MUSIC },
  { 0x2ba115b7, INPUT_KEY_GOTO_PHOTO },
  { 0x28a515b7, INPUT_KEY_GOTO_TV },
  { 0x29a395b7, INPUT_KEY_GOTO_DVD },
  { 0x2b8195b7, INPUT_KEY_MENU },
  { 0x28b755b7, INPUT_KEY_META_INFO },
  { 0x2b8115b7, INPUT_KEY_SEEK_FORWARD },
  { 0x2a8195b7, INPUT_KEY_SEEK_BACKWARD },
  { 0x2a8195b7, INPUT_KEY_SEEK_BACKWARD },
  { 0x2aa395b7, INPUT_KEY_WIDEZOOM },
  { 0x29b715b7, INPUT_KEY_APP_LAUNCHER },
  { 0x2a9395b7, INPUT_KEY_TASKSWITCH },
  { 0x2bb715b7, INPUT_KEY_CLOSE },
  { 0x2ab195b7, INPUT_KEY_SINGLE_SHOW },
  { 0x298195b7, INPUT_KEY_NEXT },
  { 0x2b9115b7, INPUT_KEY_PREV },
  { 0x2a9315b7, INPUT_KEY_SELECT },
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
	input_key_down(imonpadmap[i].key);
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
