/*
 *  /dev/input -> events
 *  Copyright (C) 2012 Andreas Öman
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <poll.h>
#include <string.h>

#include "showtime.h"
#include "arch/threads.h"
#include "event.h"

typedef struct de_dev {
  int dd_present;
  int dd_fd;
  int dd_qual;
#define QUAL_LEFT_SHIFT  0x1
#define QUAL_RIGHT_SHIFT 0x2

} de_dev_t;

#define DE_MAXDEVS 16


typedef struct devevent {
  int64_t de_last_scan;

  de_dev_t de_devs[DE_MAXDEVS];

  struct pollfd de_fds[DE_MAXDEVS];
  int de_nfds;

} devevent_t;


static void
update_fds(devevent_t *de)
{
  int i, j = 0;
  for(i = 0; i < DE_MAXDEVS; i++) {
    de_dev_t *dd = &de->de_devs[i];
    if(!dd->dd_present) 
      continue;

    de->de_fds[j].events = POLLIN | POLLERR;
    de->de_fds[j].fd = dd->dd_fd;
    j++;
  }
  de->de_nfds = j;
}


/**
 *
 */
static void
rescan(devevent_t *de)
{
  int i;
  char path[32];
  char name[80];
  for(i = 0; i < DE_MAXDEVS; i++) {
    de_dev_t *dd = &de->de_devs[i];
    
    snprintf(path, sizeof(path), "/dev/input/event%d", i);
    if(dd->dd_present) {
      if(!access(path, O_RDWR))
	continue;

      TRACE(TRACE_INFO, "DE", "Device %s disconnected", path);
      close(dd->dd_fd);
      dd->dd_present = 0;
      
    } else {
      
      dd->dd_fd = open(path, O_RDWR);
      if(dd->dd_fd == -1)
	continue;

      name[sizeof(name) - 1] = 0;
      if(ioctl(dd->dd_fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1)
	strcpy(name, "???");
	
      TRACE(TRACE_INFO, "DE", "Found %s on %s", name, path);
      dd->dd_present = 1;
    }
  }
  update_fds(de);
}


/**
 *
 */
static de_dev_t *
dd_from_fd(devevent_t *de, int fd)
{
  int i;
  for(i = 0; i < DE_MAXDEVS; i++)
    if(de->de_devs[i].dd_present && de->de_devs[i].dd_fd == fd)
      return &de->de_devs[i];
  return NULL;
}

static const wchar_t keymap[128] = 
 //0123456789
 L"  12345678"
  "90-=  qwer"
  "tyuiopå   "
  "asdfghjklö"
  "ä   zxcvbn"
  "m,.-";

static const wchar_t keymap_shift[128] = 
 //0123456789
 L"  !\"#¤%&/("
  ")=?   QWER"
  "TYUIOPÅ   "
  "ASDFGHJKLÖ"
  "Ä   ZXCVBN"
  "M;:_";

static int key_to_action[][3] = {
  {KEY_UP,                 ACTION_UP,               ACTION_MOVE_UP},
  {KEY_DOWN,               ACTION_DOWN,             ACTION_MOVE_DOWN},
  {KEY_RIGHT,              ACTION_RIGHT,            ACTION_MOVE_RIGHT},
  {KEY_LEFT,               ACTION_LEFT,             ACTION_MOVE_LEFT},
  {KEY_TAB,                ACTION_FOCUS_NEXT,       ACTION_FOCUS_PREV},
  {KEY_MUTE,               ACTION_VOLUME_MUTE_TOGGLE},
  {KEY_VOLUMEUP,           ACTION_VOLUME_UP},
  {KEY_VOLUMEDOWN,         ACTION_VOLUME_DOWN},
  {KEY_HOMEPAGE,           ACTION_HOME},
  {KEY_PLAYPAUSE,          ACTION_PLAYPAUSE},
  {KEY_PREVIOUSSONG,       ACTION_SKIP_BACKWARD},
  {KEY_NEXTSONG,           ACTION_SKIP_FORWARD},
  {KEY_STOPCD,             ACTION_STOP},
  {KEY_RECORD,             ACTION_RECORD},
  {KEY_SLEEP,              ACTION_STANDBY},
  {KEY_PAGEUP,             ACTION_PAGE_UP},
  {KEY_PAGEDOWN,           ACTION_PAGE_DOWN},
  {KEY_BACK,               ACTION_NAV_BACK},

  // These should be configurable

  {KEY_MEDIA,              ACTION_MENU},
  {KEY_MENU,               ACTION_ITEMMENU},
  {KEY_PROG1,              ACTION_LOGWINDOW},
  {KEY_PROG2,              ACTION_SHOW_MEDIA_STATS},
  {KEY_PROG3,              ACTION_SYSINFO},
  
  
  {0,0}
};


static void
doqual(de_dev_t *dd, const struct input_event *ie, int code, int qual)
{
  if(ie->code == code) {
    if(ie->value)
      dd->dd_qual |= qual;
    else
      dd->dd_qual &= ~qual;
  }
}

/**
 *
 */
static int
dd_read(de_dev_t *dd)
{
  struct input_event ie;
  event_t *e = NULL;
  int i;

  if(read(dd->dd_fd, &ie, 16) != 16)
    return 1;

  if(ie.type == EV_REL) {
    if(ie.code == REL_WHEEL) {
      int action = ie.value < 0 ? ACTION_DOWN : ACTION_UP;
      int cnt = abs(ie.value);
      if(cnt > 4)
	cnt = 4;

      while(cnt--) {
	e = event_create_action(action);
	event_to_ui(e);
	
      }
    }
    return 0;
  }

  if(ie.type != EV_KEY)
    return 0;

  doqual(dd, &ie, KEY_LEFTSHIFT,  QUAL_LEFT_SHIFT);
  doqual(dd, &ie, KEY_RIGHTSHIFT, QUAL_RIGHT_SHIFT);

  if(ie.value == 0)
    return 0; // release


  int shift = !!(dd->dd_qual & (QUAL_LEFT_SHIFT | QUAL_RIGHT_SHIFT));

  for(i = 0; key_to_action[i][0]; i++) {
    if(key_to_action[i][0] == ie.code) {
      event_to_ui(event_create_action(key_to_action[i][1+shift]));
      return 0;
    }
  }
  
  switch(ie.code) {
  case KEY_SPACE:
    e = event_create_int(EVENT_UNICODE, 32);
    break;

  case KEY_ENTER: 
    e = event_create_action_multi((const action_type_t[]){
	ACTION_ACTIVATE, ACTION_ENTER}, 2);
    break;
    
  case KEY_BACKSPACE:
    e = event_create_action_multi((const action_type_t[]){
	ACTION_BS, ACTION_NAV_BACK}, 2);
    break;
    
  case KEY_F1 ... KEY_F10:
    e = event_from_Fkey(1 + ie.code - KEY_F1, shift);
    break;

  default:
    if(ie.code < 128) {
      int uc = shift ? keymap_shift[ie.code] : keymap[ie.code];
      if(uc > ' ')
	e = event_create_int(EVENT_UNICODE, uc);
    }
    if(e == NULL)
      printf("Unmapped key %d\n", ie.code);
    break;
  }

  if(e != NULL)
    event_to_ui(e);


  return 0;
}


/**
 *
 */
static void
dd_stop(devevent_t *de, de_dev_t *dd)
{
  close(dd->dd_fd);
  dd->dd_present = 0;

  update_fds(de);
}


/**
 *
 */
static void *
devevent_thread(void *aux)
{
  devevent_t *de = calloc(1, sizeof(devevent_t));
  de_dev_t *dd;
  int n, i;
  rescan(de);

  while(1) {
    n = poll(de->de_fds, de->de_nfds, 1000);
    if(n == -1) {
      rescan(de);
      continue;
    }
    
    for(i = 0; i < de->de_nfds && n > 0; i++) {
      if(!de->de_fds[i].revents)
	continue;

      dd = dd_from_fd(de, de->de_fds[i].fd);
      assert(dd != NULL);

      if(de->de_fds[i].revents & POLLIN)
	if(dd_read(dd))
	  dd_stop(de, dd);

      if(de->de_fds[i].revents & POLLERR)
	dd_stop(de, dd);

    }
  }

  return NULL;
}



static void
devevent_start(void)
{
  hts_thread_create_detached("devevent", devevent_thread, NULL,
			     THREAD_PRIO_NORMAL);
}
INITME(INIT_GROUP_IPC, devevent_start);
