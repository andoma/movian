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
  INPUT_KEY_UP = 256,
  INPUT_KEY_DOWN,
  INPUT_KEY_LEFT,
  INPUT_KEY_RIGHT,
  INPUT_KEY_PREV,
  INPUT_KEY_NEXT,
  INPUT_KEY_STOP,
  INPUT_KEY_PLAYPAUSE,
  INPUT_KEY_PLAY,
  INPUT_KEY_PAUSE,
  INPUT_KEY_ROTATE,
  INPUT_KEY_VOLUME_UP,
  INPUT_KEY_VOLUME_DOWN,
  INPUT_KEY_VOLUME_MUTE,
  INPUT_KEY_MENU,
  INPUT_KEY_INCR,
  INPUT_KEY_DECR,
  INPUT_KEY_BACK,
  INPUT_KEY_ENTER,
  INPUT_KEY_SELECT,
  INPUT_KEY_CLOSE,
  INPUT_KEY_EJECT,
  INPUT_KEY_DVD_AUDIO_MENU,
  INPUT_KEY_DVD_SPU_MENU,
  INPUT_KEY_DVD_SPU_OFF,
  INPUT_KEY_DVDUP,
  INPUT_KEY_RESTART_TRACK,
  INPUT_KEY_DELETE,
  INPUT_KEY_CLEAR,
  INPUT_KEY_CLEAR_BUT_CURRENT,
  INPUT_KEY_SAVE,
  INPUT_KEY_LOAD_ALL,
  INPUT_KEY_CHANNEL_PLUS,
  INPUT_KEY_CHANNEL_MINUS,
  INPUT_KEY_POWER,
  INPUT_KEY_GOTO_TV,
  INPUT_KEY_GOTO_PHOTO,
  INPUT_KEY_GOTO_MUSIC,
  INPUT_KEY_GOTO_MOVIES,
  INPUT_KEY_GOTO_DVD,
  INPUT_KEY_SEEK_FORWARD,
  INPUT_KEY_SEEK_BACKWARD,
  INPUT_KEY_SEEK_FAST_FORWARD,
  INPUT_KEY_SEEK_FAST_BACKWARD,
  INPUT_KEY_WIDEZOOM,
  INPUT_KEY_TASKSWITCH,
  INPUT_KEY_APP_LAUNCHER,
  INPUT_KEY_SINGLE_SHOW,
  INPUT_KEY_META_INFO,
  INPUT_KEY_RECORD_ONCE,
  INPUT_KEY_RECORD_DAILY,
  INPUT_KEY_RECORD_WEEKLY,
  INPUT_KEY_RECORD_CANCEL,
  INPUT_KEY_RECORD_TOGGLE,
  INPUT_KEY_QUIT,
} input_key_t;

typedef enum {
  INPUT_NONE = 0,
  INPUT_KEY,
  INPUT_PAD,
  INPUT_SPECIAL,
  
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
  } u;

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
