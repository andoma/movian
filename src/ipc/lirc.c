/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

#include "main.h"
#include "htsmsg/htsbuf.h"
#include "event.h"


static int lirc_fd;

static const struct {
  const char *name;
  uint16_t action1;
  uint16_t action2;
} lircmap[] = {
  { "Up",           ACTION_UP    },
  { "Down",         ACTION_DOWN  },
  { "Left",         ACTION_LEFT  },
  { "Right",        ACTION_RIGHT },
  { "Enter",        ACTION_ACTIVATE, ACTION_ENTER },
  { "Back",         ACTION_BS, ACTION_NAV_BACK },
  { "Backspace",    ACTION_BS, ACTION_NAV_BACK },
  { "Tab",          ACTION_FOCUS_NEXT },
  { "ShiftTab",     ACTION_FOCUS_PREV },
};

static void *
lirc_thread(void *aux)
{
  char buf[200];
  uint64_t ircode;
  uint32_t repeat;
  char keyname[100];
  int i, r, fd, len, n;
  htsbuf_queue_t q;
  struct pollfd fds;
  event_t *e;

  fd = lirc_fd;

  htsbuf_queue_init(&q, 0);
  fds.fd = fd;
  fds.events = POLLIN;

  while(1) {

    r = poll(&fds, 1, -1);
    if(r > 0) {
      if((r = read(fd, buf, sizeof(buf))) < 1) {
	TRACE(TRACE_ERROR, "lircd", "Read error: %s", strerror(errno));
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
      
      n = sscanf(buf, "%"PRIx64" %x %s", &ircode, &repeat, keyname);
      if(n != 3) {
	TRACE(TRACE_INFO, "lircd", "Invalid LIRC input: \"%s\"", buf);
	continue;
      }
      
      if(keyname[0] && keyname[1] == 0) {
	/* ASCII input */
	e = event_create_int(EVENT_UNICODE, keyname[0]);
      } else {
	e = NULL;
	for(i = 0; i < sizeof(lircmap) / sizeof(lircmap[0]); i++) {
	  if(!strcasecmp(keyname, lircmap[i].name)) {
	    action_type_t av[3] = {
	      lircmap[i].action1,
	      lircmap[i].action2,
	    };
	    if(av[1] != ACTION_NONE)
	      e = event_create_action_multi(av, 2);
	    else
	      e = event_create_action_multi(av, 1);
	    break;
	  }
	}
      }
      if(e == NULL) {
	snprintf(buf, sizeof(buf), "IR+%s", keyname);
	e = event_create_str(EVENT_KEYDESC, buf);
      }
      e->e_flags |= EVENT_KEYPRESS;
      event_to_ui(e);
    }
  }
 out:
  close(fd);
  htsbuf_queue_flush(&q);
  return NULL;
}


/**
 *
 */
static int
lirc_open_socket(const char *path)
{
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un sun;

  sun.sun_family = AF_UNIX;
  snprintf(sun.sun_path, sizeof(sun.sun_path), "%s", path);

  if(connect(fd, (struct sockaddr *)&sun, sizeof(struct sockaddr_un))) {
    close(fd);
    return -1;
  }
  return fd;
}

/**
 *
 */
static void
lirc_open(void)
{
  int fd;

  fd = lirc_open_socket("/var/run/lirc/lircd");
  if(fd == -1)
    fd = lirc_open_socket("/dev/lircd"); // Old path

  if(fd == -1)
    return;

  lirc_fd = fd;
  hts_thread_create_detached("lirc", lirc_thread, NULL,
			     THREAD_PRIO_UI_WORKER_HIGH);
}


INITME(INIT_GROUP_IPC, lirc_open, NULL, 0);
