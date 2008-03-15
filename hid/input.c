/*
 *  Input handling
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

#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "showtime.h"
#include "input.h"

static pthread_mutex_t ihmutex = PTHREAD_MUTEX_INITIALIZER;
static LIST_HEAD(, inputhandler) inputhandlers;

/*
 *
 */

void
input_init(ic_t *ic)
{
  pthread_cond_init(&ic->ic_cond, NULL);
  pthread_mutex_init(&ic->ic_mutex, NULL);
  TAILQ_INIT(&ic->ic_queue);
}

/*
 *
 */

void
input_postevent(ic_t *ic, inputevent_t *src)
{
  inputevent_t *ie;

  pthread_mutex_lock(&ic->ic_mutex);
  ie = malloc(sizeof(inputevent_t));
  *ie = *src;
  TAILQ_INSERT_TAIL(&ic->ic_queue, ie, link);
  pthread_cond_signal(&ic->ic_cond);
  pthread_mutex_unlock(&ic->ic_mutex);
}

/*
 *
 */

void
input_keystrike(ic_t *ic, int key)
{
  inputevent_t ie;

  ie.u.key = key;
  ie.type = INPUT_KEY;
  input_postevent(ic, &ie);
}

/*
 *
 */

int
input_getevent(ic_t *ic, int wait, inputevent_t *ie, struct timespec *ti)
{
  inputevent_t *src;
  pthread_mutex_lock(&ic->ic_mutex);
  
  if(wait) while((TAILQ_FIRST(&ic->ic_queue)) == NULL) {
    if(ti) {
      if(pthread_cond_timedwait(&ic->ic_cond, &ic->ic_mutex, ti) == ETIMEDOUT)
	goto fail;
    }
    else
      pthread_cond_wait(&ic->ic_cond, &ic->ic_mutex);
  }
  
  src = TAILQ_FIRST(&ic->ic_queue);
  if(src == NULL) {
  fail:
    pthread_mutex_unlock(&ic->ic_mutex);
    return 1;
  }

  TAILQ_REMOVE(&ic->ic_queue, src, link);
  *ie = *src;
  free(src);
  pthread_mutex_unlock(&ic->ic_mutex);
  return 0;
}


void
input_flush_queue(ic_t *ic)
{
  inputevent_t *src;
  pthread_mutex_lock(&ic->ic_mutex);

  while((src = TAILQ_FIRST(&ic->ic_queue)) != NULL) {
    if(src->type >= INPUT_APP)
      src->freefunc(src->type, src->u.ptr);
    TAILQ_REMOVE(&ic->ic_queue, src, link);
    free(src);
  }

  pthread_mutex_unlock(&ic->ic_mutex);
}






/*
 *
 */

int
input_getkey(ic_t *ic, int wait)
{
  inputevent_t ie;

  do {
    if(input_getevent(ic, wait, &ie, NULL) && !wait)
      return 0;
  } while(ie.type != INPUT_KEY);
  return ie.u.key;
}

/*
 *
 */

int
input_getkey_sleep(ic_t *ic, int msec)
{
  struct timeval tv;
  struct timespec ts;
  inputevent_t ie;

  gettimeofday(&tv, NULL);
  ts.tv_sec = tv.tv_sec;
  ts.tv_nsec = tv.tv_usec * 1000;
  
  ts.tv_nsec += msec * 1000000;
  if(ts.tv_nsec > 1000000000) {
    ts.tv_nsec -= 1000000000;
    ts.tv_sec++;
  }

  do {
    if(input_getevent(ic, 1, &ie, &ts))
      return 0;
  } while(ie.type != INPUT_KEY);
  return ie.u.key;
}

/*
 *  Root input handler & events (from keyboard, remote controls, etc)
 */


typedef struct inputhandler {
  LIST_ENTRY(inputhandler) link;
  int pri;
  int (*callback)(inputevent_t *ie);
} inputhandler_t;


static int 
ihcmp(inputhandler_t *a, inputhandler_t *b)
{
  return b->pri - a->pri;
}

void
inputhandler_register(int pri, int (*callback)(inputevent_t *ie))
{
  inputhandler_t *ih;
  ih = malloc(sizeof(inputhandler_t));

  ih->pri = pri;
  ih->callback = callback;
  pthread_mutex_lock(&ihmutex);
  LIST_INSERT_SORTED(&inputhandlers, ih, link, ihcmp);
  pthread_mutex_unlock(&ihmutex);
}




/*
 *
 */

void
input_root_event(inputevent_t *ie)
{
  static int64_t lasttime;
  static int lastkey;
  inputhandler_t *ih;
  uint64_t dclick = 500000;

  pthread_mutex_lock(&ihmutex);

  /* Repeating PREV should be RESTART_TRACK */
  
  if(ie->type == INPUT_KEY && wallclock - lasttime < dclick) {
    switch(ie->u.key) {
    case INPUT_KEY_PREV:
      if(lastkey == INPUT_KEY_PREV)
	ie->u.key = INPUT_KEY_RESTART_TRACK;
      break;

    case INPUT_KEY_STOP:
      if(lastkey == INPUT_KEY_STOP)
	ie->u.key = INPUT_KEY_EJECT;
      break;

    case INPUT_KEY_SEEK_FORWARD:
      if(lastkey == INPUT_KEY_SEEK_FORWARD)
	ie->u.key = INPUT_KEY_SEEK_FAST_FORWARD;
      break;

    case INPUT_KEY_SEEK_BACKWARD:
      if(lastkey == INPUT_KEY_SEEK_BACKWARD)
	ie->u.key = INPUT_KEY_SEEK_FAST_BACKWARD;
      break;
      
    default:
      break;
    }
    lastkey = ie->u.key;
  }

  
  lasttime = wallclock;

  LIST_FOREACH(ih, &inputhandlers, link) {
    if(ih->callback(ie))
      break;
  }
  
  pthread_mutex_unlock(&ihmutex);
}


void
input_key_down(int key)
{
  inputevent_t ie;

  ie.type = INPUT_KEY;
  ie.u.key = key;
  input_root_event(&ie);
}

