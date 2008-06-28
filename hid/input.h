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

#ifndef INPUT_H
#define INPUT_H

#include <inttypes.h>
#include <sys/queue.h>

typedef enum {
  INPUT_KEY_UP = (1 << 24),
  INPUT_KEY_DOWN,
  INPUT_KEY_LEFT,
  INPUT_KEY_RIGHT,
  INPUT_KEY_PREV,
  INPUT_KEY_NEXT,
  INPUT_KEY_CLOSE,
  INPUT_KEY_STOP,
  INPUT_KEY_PLAYPAUSE,
  INPUT_KEY_PLAY,
  INPUT_KEY_PAUSE,
  INPUT_KEY_VOLUME_UP,
  INPUT_KEY_VOLUME_DOWN,
  INPUT_KEY_VOLUME_MUTE,
  INPUT_KEY_MENU,
  INPUT_KEY_BACK,
  INPUT_KEY_ENTER,
  INPUT_KEY_SELECT,
  INPUT_KEY_EJECT,
  INPUT_KEY_RESTART_TRACK,
  INPUT_KEY_POWER,
  INPUT_KEY_SEEK_FORWARD,
  INPUT_KEY_SEEK_BACKWARD,
  INPUT_KEY_SEEK_FAST_FORWARD,
  INPUT_KEY_SEEK_FAST_BACKWARD,
  INPUT_KEY_QUIT,
  INPUT_KEY_TASK_SWITCHER,
  INPUT_KEY_TASK_DOSWITCH,
  INPUT_KEY_SWITCH_VIEW,
} input_key_t;

typedef enum {
  INPUT_NONE = 0,
  INPUT_KEY,
  INPUT_TS,
  INPUT_PAD,
  INPUT_KEYDESC,
  INPUT_NO_AUDIO,
  INPUT_VEC,             /* vector of u32 */
  INPUT_U32,             /* Generic U32 */
  INPUT_APP,             /* Application specific, 'ptr' will be free'd
			    by freefunc if entry is flushed.
			    NOTE: Applications may stack additional types
			    by having INPUT_APP as a bias */
  
} input_event_type_t;



typedef struct inputevent {

  TAILQ_ENTRY(inputevent) link;

  input_event_type_t type;
  
  union {
    input_key_t key;
    struct {
      float x, y;
    } xy;

    uint32_t u32;
    void *ptr;

    char keydesc[32];

    struct {
      /* TS feedback */
      int64_t dts;
      int64_t pts;
      int     stream;
    } ts;

    struct {
      uint32_t u32[4];
    } vec;

  } u;

  void (*freefunc)(input_event_type_t type, void *ptr);

} inputevent_t;

TAILQ_HEAD(inputevent_queue, inputevent);

typedef struct inputctrl {
  pthread_cond_t ic_cond;
  pthread_mutex_t ic_mutex;
  struct inputevent_queue ic_queue;
} ic_t;

void input_init(ic_t *ic);

void input_keystrike(ic_t *ic, int key);

int input_getkey(ic_t *ic, int wait);

void input_flush_queue(ic_t *ic);

int input_getkey_sleep(ic_t *ic, int msec);

void input_setup(void);

void input_key_down(int key);

int input_getevent(ic_t *ic, int wait, inputevent_t *ie, struct timespec *ti);

void input_postevent(ic_t *ic, inputevent_t *ie);

void input_root_event(inputevent_t *ie);

void inputhandler_register(int pri, int (*callback)(inputevent_t *ie));

#endif /* INPUT_H */
