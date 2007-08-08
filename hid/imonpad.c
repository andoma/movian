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



#include <netinet/in.h>
#include <arpa/inet.h>

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


static int adx, ady;
average_t avdx, avdy;


static int hym, hyp, hxm, hxp;

static void *
imonpad_thread(void *aux)
{
  int fd = -1, i;
  uint8_t buf[4];
  uint32_t v;
  int dx, dy;
  inputevent_t ie;

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

    v = ntohl(*(uint32_t *)buf);
    
    if(buf[0] & 0x40 && buf[3] == 0xb7) {
      dx = (buf[1] & 0x08) | (buf[1] & 0x10) >> 2 | 
	(buf[1] & 0x20) >> 4 | (buf[1] & 0x40) >> 6;
      dy = (buf[2] & 0x08) | (buf[2] & 0x10) >> 2 | 
	(buf[2] & 0x20) >> 4 | (buf[2] & 0x40) >> 6;


      if(buf[0] & 0x02) dx |= ~0x10+1;
      if(buf[0] & 0x01) dy |= ~0x10+1;

      adx += dx;
      ady += dy;

      //      printf("%3d, %3d\n", dx, dy);

      if(0) {
	ie.u.xy.x = dx;
	ie.u.xy.y = dy;
	ie.type = INPUT_PAD;
	input_root_event(&ie);
      }

      if(dx < -2 && hxm < 1) {
	hxm = 30 - -dx * 3;
	input_key_down(INPUT_KEY_LEFT);
      }

      if(dy < -2 && hym < 1) {
	hym = 30 - -dy * 3;
	input_key_down(INPUT_KEY_UP);
      }

      if(dx > 2 && hxp < 1) {
	hxp = 30 - dx * 3;
	input_key_down(INPUT_KEY_RIGHT);
      }

      if(dy > 2 && hyp < 1 ) {
	hyp = 30 - dy * 3;
	input_key_down(INPUT_KEY_DOWN);
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



static void *
imonpad_thread2(void *aux)
{
  float x = 0, y = 0;
  int xx, yy;
  inputevent_t ie;

  while(1) {
    usleep(20000);


    x = (x * 2.0 + (float)adx) / 3.0f;
    y = (y * 2.0 + (float)ady) / 3.0f;

    adx = 0;
    ady = 0;

    xx = x;
    yy = y;

    if(xx || yy) {
      ie.u.xy.x = xx;
      ie.u.xy.y = yy;
      ie.type = INPUT_PAD;
      input_root_event(&ie);
    }

    if(hxm > 0)
      hxm--;
    if(hym > 0)
      hym--;

    if(hxp > 0)
      hxp--;
    if(hyp > 0)
      hyp--;
  }
}




void
imonpad_init(void)
{
  static pthread_t ptid;

  has_analogue_pad = 1;

  pthread_create(&ptid, NULL, imonpad_thread, NULL);
  pthread_create(&ptid, NULL, imonpad_thread2, NULL);
}
