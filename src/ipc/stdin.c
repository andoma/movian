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
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <string.h>

#include "main.h"
#include "arch/threads.h"
#include "event.h"


#define ESEQ(n...) (const uint8_t []){n}

const static struct {
  const uint8_t *codes;
  int action;
  int fkey;
} map[] = {
  {ESEQ(0x1b,0x5b,0x41,0x00),               ACTION_UP,    0 },
  {ESEQ(0x1b,0x5b,0x42,0x00),               ACTION_DOWN,  0 },
  {ESEQ(0x1b,0x5b,0x43,0x00),               ACTION_RIGHT, 0 },
  {ESEQ(0x1b,0x5b,0x44,0x00),               ACTION_LEFT,  0 },
  {ESEQ(0x1b,0x4f,0x50,0x00),               0,            0x1 },
  {ESEQ(0x1b,0x4f,0x51,0x00),               0,            0x2 },
  {ESEQ(0x1b,0x4f,0x52,0x00),               0,            0x3 },
  {ESEQ(0x1b,0x4f,0x53,0x00),               0,            0x4 },
  {ESEQ(0x1b,0x5b,0x31,0x35,0x7e,0x00),     0,            0x5 },
  {ESEQ(0x1b,0x5b,0x31,0x37,0x7e,0x00),     0,            0x6 },
  {ESEQ(0x1b,0x5b,0x31,0x38,0x7e,0x00),     0,            0x7 },
  {ESEQ(0x1b,0x5b,0x31,0x39,0x7e,0x00),     0,            0x8 },
  {ESEQ(0x1b,0x5b,0x32,0x30,0x7e,0x00),     0,            0x9 },
  {ESEQ(0x1b,0x5b,0x32,0x31,0x7e,0x00),     0,            0xa },
  {ESEQ(0x1b,0x5b,0x32,0x32,0x7e,0x00),     0,            0xb },
  {ESEQ(0x1b,0x5b,0x32,0x33,0x7e,0x00),     0,            0xc },
  {ESEQ(0x1b,0x5b,0x35,0x7e,0x00),          ACTION_PAGE_UP},
  {ESEQ(0x1b,0x5b,0x36,0x7e,0x00),          ACTION_PAGE_DOWN},
  {ESEQ(0x1b,0x4f,0x48,0x00),               ACTION_TOP},
  {ESEQ(0x1b,0x4f,0x46,0x00),               ACTION_BOTTOM},
};




static void *
stdin_thread(void *aux)
{
  unsigned char c, buffer[64];
  int bufferptr = 0, r, escaped = 0;
  
  while(1) {
    event_t *e = NULL;
    
    if(escaped) {
      struct pollfd fds;
      fds.fd = 0;
      fds.events = POLLIN;
      if(poll(&fds, 1, 100) == 1)
	r = read(0, &c, 1);
      else
	r = 0;
    } else {
      r = read(0, &c, 1);
    }

    if(r == 1) {
      if(bufferptr == sizeof(buffer) - 1)
	bufferptr = 0;
	
      buffer[bufferptr++] = c;
    }
    escaped = 0;

    switch(buffer[0]) {

    case 8:
    case 0x7f:
      e = event_create_action_multi((const action_type_t[]){
	  ACTION_BS, ACTION_NAV_BACK}, 2);
      bufferptr = 0;
      break;

    case 10:
      e = event_create_action_multi((const action_type_t[]){
	  ACTION_ACTIVATE, ACTION_ENTER}, 2);
      bufferptr = 0;
      break;

    case 9:
      e = event_create_action(ACTION_FOCUS_NEXT);
      bufferptr = 0;
      break;

    case 32 ... 0x7e:
      bufferptr = 0;
      e = event_create_int(EVENT_UNICODE, buffer[0]);
      break;

    default:
      bufferptr = 0;
      break;

    case 0x1b:
      if(r == 0) {
	if(bufferptr == 1)
	  e = event_create_action(ACTION_CANCEL);
	bufferptr = 0;
      } else {
	int i;

	escaped = 1;
	buffer[bufferptr] = 0;
	
	for(i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
	  if(!strcmp((const char *)map[i].codes, (const char *)buffer)) {


	    if(map[i].action) {
	      e = event_create_action(map[i].action);
	    } else if(map[i].fkey) {
	      e = event_from_Fkey(map[i].fkey, 0);
	    }
	    break;
	  }
	}

	if(e == NULL) {
#if 0
	  printf("Unmapped esc sequence: ");
	  for(i = 0; i < bufferptr; i++)
	    printf("0x%02x,", buffer[i]);
	  printf("0x00\n");
#endif
	}
      }
      break;
    }
    

    if(e == NULL)
      continue;
    e->e_flags |= EVENT_KEYPRESS;
    event_to_ui(e);
  }
  return NULL;
}

static struct termios termio;

/**
 *
 */
static void
stdin_shutdown_early(void *opaque, int exitcode)
{
  tcsetattr(0, TCSANOW, &termio);
}

static void
stdin_start(void)
{
  if(!gconf.listen_on_stdin)
    return;

  struct termios termio2;

  if(!isatty(0))
    return;
  if(tcgetattr(0, &termio) == -1)
    return;
  termio2 = termio;
  termio2.c_lflag &= ~(ECHO | ICANON);
  if(tcsetattr(0, TCSANOW, &termio2) == -1)
    return;

  shutdown_hook_add(stdin_shutdown_early, NULL, 0);

  hts_thread_create_detached("stdin", stdin_thread, NULL,
			     THREAD_PRIO_UI_WORKER_HIGH);
}


INITME(INIT_GROUP_IPC, stdin_start, NULL, 0);
