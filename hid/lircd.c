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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>

#include <libhts/htsbuf.h>

#include "lircd.h"
#include "event.h"
#include "showtime.h"
#include "hid/hid.h"
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
  { "Back",         GEV_BACKSPACE  },
  { "Backspace",    GEV_BACKSPACE  },
};

void
lircd_proc(prop_t *status)
{
  char buf[200];
  uint64_t ircode;
  uint32_t repeat;
  char keyname[100];
  int i, r, fd, len;
  htsbuf_queue_t q;
  struct pollfd fds;
  const char *dev = "/dev/lirc";

  if((fd = open(dev, O_RDONLY)) == -1) {
    prop_set_stringf(status, "lirc: Unable to open \"%s\"", dev);
    sleep(1);
    return;
  }


  fds.fd = fd;
  fds.events = POLLIN;

  htsbuf_queue_init(&q, 0);

  while(hid_ir_mode == HID_IR_LIRC) {

    r = poll(&fds, 1, 1000);
    if(r > 0) {
      if((r = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) < 1) {
	prop_set_stringf(status, "lirc: Unable to read from \"%s\"", dev);
	break;
      }
      htsbuf_append(&q, buf, r);
    }

    while((len = htsbuf_find(&q, 0xa)) != -1) {
      
      if(len >= sizeof(buf) - 1) {
	prop_set_stringf(status, "lirc: Command buffer size exceeded");
	goto out;
      }

      htsbuf_read(&q, buf, len);
      buf[len] = 0;
      
      while(len > 0 && buf[len - 1] < 32)
	buf[--len] = 0;
      htsbuf_drop(&q, 1); /* Drop the \n */
      
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
  }
 out:
  close(fd);
  htsbuf_queue_flush(&q);;
}
