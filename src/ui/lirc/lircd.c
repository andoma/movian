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
#include <errno.h>

#include <htsmsg/htsbuf.h>

#include "event.h"
#include "showtime.h"
#include "settings.h"
#include "ui/ui.h"
#include "event.h"

static const struct {
  const char *name;
  action_type_t key;
} lircmap[] = {
  { "Up",           ACTION_UP    },
  { "Down",         ACTION_DOWN  },
  { "Left",         ACTION_LEFT  },
  { "Right",        ACTION_RIGHT },
  { "Enter",        ACTION_ENTER },
  { "Back",         ACTION_BS  },
  { "Backspace",    ACTION_BS  },
};

static int
lircd_start(ui_t *ui, int argc, char *argv[], int primary)
{
  char buf[200];
  uint64_t ircode;
  uint32_t repeat;
  char keyname[100];
  int i, r, fd, len, n;
  htsbuf_queue_t q;
  struct pollfd fds;
  const char *dev = "/dev/lirc";
  uii_t *uii;
  prop_t *p;

  /* Parse options */
  argv++;
  argc--;

  while(argc > 0) {
    if(!strcmp(argv[0], "--device") && argc > 1) {
      dev = argv[1];
      argc -= 2; argv += 2;
      continue;
    } else
      break;
  }

  if((fd = open(dev, O_RDONLY)) == -1) {
    TRACE(TRACE_ERROR, "lircd", "Unable to open %s", dev);
    return 1;
  }

  uii = calloc(1, sizeof(uii_t));
  uii->uii_ui = ui;
  uii_register(uii, primary);

  p = settings_add_dir(NULL, "lircd", "Settings for LIRC", "display");

  htsbuf_queue_init(&q, 0);
  fds.fd = fd;
  fds.events = POLLIN;

  while(1) {

    r = poll(&fds, 1, 1000);
    if(r > 0) {
      if((r = read(fd, buf, sizeof(buf))) < 1) {
	TRACE(TRACE_ERROR, "lircd", "Read error from %s -- %s", dev,
	      strerror(errno));
	break;
      }
      htsbuf_append(&q, buf, r);
    }

    while((len = htsbuf_find(&q, 0xa)) != -1) {
      
      if(len >= sizeof(buf) - 1) {
	TRACE(TRACE_ERROR, "lircd", "Command buffer size exceeded");
	goto out;
      }

      htsbuf_read(&q, buf, len);
      buf[len] = 0;
      
      while(len > 0 && buf[len - 1] < 32)
	buf[--len] = 0;
      htsbuf_drop(&q, 1); /* Drop the \n */
      
      n = sscanf(buf, "%"PRIx64" %d %s", &ircode, &repeat, keyname);
      if(n != 3) {
	TRACE(TRACE_ERROR, "lircd", "Invalid LIRC input: \"%s\"", buf);
	continue;
      }
      
      if(keyname[0] && keyname[1] == 0) {
	/* ASCII input */
	event_dispatch(event_create_unicode(keyname[0]));
	continue;
      }

      for(i = 0; i < sizeof(lircmap) / sizeof(lircmap[0]); i++) {
#if 0
	if(!strcasecmp(keyname, lircmap[i].name)) {
	  event_post_simple(lircmap[i].key);
	}
#endif
      }

      TRACE(TRACE_DEBUG, "imonpad", "Got key %s", buf);

      if(i == sizeof(lircmap) / sizeof(lircmap[0])) {
	/* No hit, send to keymapper */
	snprintf(buf, sizeof(buf), "lirc - %s", keyname);
	//	ui_dispatch_event(NULL, buf, uii);
      }
    }
  }
 out:
  close(fd);
  htsbuf_queue_flush(&q);
  return 0;
}


/**
 *
 */
ui_t lircd_ui = {
  .ui_title = "lircd",
  .ui_start = lircd_start,
};

