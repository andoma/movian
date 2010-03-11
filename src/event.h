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

#ifndef EVENT_H__
#define EVENT_H__

#include <inttypes.h>
#include "misc/queue.h"
#include "arch/threads.h"
#include "arch/atomic.h"


typedef enum {

  ACTION_NONE = 0,
  ACTION_UP,
  ACTION_DOWN,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_ENTER,
  ACTION_BS,

  ACTION_NAV_FWD,
  ACTION_NAV_BACK,

  ACTION_FOCUS_NEXT,  /* TAB */
  ACTION_FOCUS_PREV,  /* Shift + TAB */

  ACTION_PAGE_UP,
  ACTION_PAGE_DOWN,

  ACTION_TOP,
  ACTION_BOTTOM,

  ACTION_INCR,
  ACTION_DECR,

  ACTION_CLOSE,
  ACTION_STOP,
  ACTION_PLAYPAUSE,
  ACTION_PLAY,
  ACTION_PAUSE,
  ACTION_VOLUME_UP,
  ACTION_VOLUME_DOWN,
  ACTION_VOLUME_MUTE_TOGGLE,
  ACTION_MENU,
  ACTION_SELECT,
  ACTION_EJECT,
  ACTION_SLEEP,
  ACTION_QUIT,
  ACTION_RESTART_TRACK,
  ACTION_PREV_TRACK,
  ACTION_NEXT_TRACK,
  ACTION_SEEK_FORWARD,
  ACTION_SEEK_BACKWARD,
  ACTION_SEEK_FAST_FORWARD,
  ACTION_SEEK_FAST_BACKWARD,
  ACTION_HOME,
  ACTION_SWITCH_VIEW,
  ACTION_CHANNEL_NEXT,
  ACTION_CHANNEL_PREV,
  ACTION_FULLSCREEN_TOGGLE,

  ACTION_ZOOM_UI_INCR,
  ACTION_ZOOM_UI_DECR,

  ACTION_RELOAD_UI,

  ACTION_SHOW_MEDIA_STATS,

  ACTION_SHUFFLE,
  ACTION_REPEAT,

  ACTION_RECORD,

  ACTION_last_mappable
} action_type_t;



typedef enum {
  EVENT_OFFSET = 5000,
  EVENT_ACTION_VECTOR,
  EVENT_UNICODE,
  EVENT_KEYDESC,
  EVENT_AUDIO_CLOCK,
  EVENT_VIDEO_CLOCK,
  EVENT_PLAYQUEUE_ENQ,
  EVENT_PLAYQUEUE_JUMP,
  EVENT_TV,            /* TV specific events, see tv.h */
  EVENT_SEEK,
  EVENT_EOF,           /* End of file */
  EVENT_PLAY_URL,
  EVENT_EXIT,
  EVENT_DVD_PCI,
  EVENT_DVD_,
  EVENT_DVD_SELECT_BUTTON,
  EVENT_DVD_ACTIVATE_BUTTON,  /* "Press" button */
  EVENT_OPENURL,

  EVENT_MP_NO_LONGER_PRIMARY,
  EVENT_MP_IS_PRIMARY,

  EVENT_INTERNAL_PAUSE,      /* Internal pause with a playback module */

  EVENT_CURRENT_PTS,

  EVENT_SELECT_TRACK,

} event_type_t;



TAILQ_HEAD(event_q, event);

/**
 *
 */
typedef struct event_queue {
  struct event_q eq_q;
  hts_cond_t eq_cond;
  hts_mutex_t eq_mutex;
} event_queue_t;


/**
 *
 */
typedef struct event {
  int     e_refcount;
  int     e_mapped;
  event_type_t e_type_x;
  void (*e_dtor)(struct event *e);
  TAILQ_ENTRY(event) e_link;
  char e_payload[0];
} event_t;


/**
 *
 */
typedef struct event_unicode {
  event_t h;
  int sym;
} event_unicode_t;



/**
 *
 */
typedef struct event_openurl {
  event_t h;
  char *url;
  char *type;
  char *parent;
} event_openurl_t;


/**
 *
 */
typedef struct event_select_track {
  event_t h;
  char *id;
} event_select_track_t;


/**
 *
 */
typedef struct event_action_vector {
  event_t h;
  int num;
  action_type_t actions[0];
} event_action_vector_t;


void event_generic_dtor(event_t *e);

void *event_create(event_type_t type, size_t size);

event_t *event_create_action(action_type_t action);

event_t *event_create_action_multi(const action_type_t *actions, size_t numactions);

#define event_create_type(type) event_create(type, sizeof(event_t))

void *event_create_unicode(int sym);

void event_enqueue(event_queue_t *eq, event_t *e);

event_t *event_get(event_queue_t *eq);

void event_unref(event_t *e);

void event_initqueue(event_queue_t *eq);

void event_flushqueue(event_queue_t *eq);

event_t *event_create_url(event_type_t et, const char *url);

event_t *event_create_openurl(const char *url, const char *type,
			      const char *parent);

event_t *event_create_select_track(const char *id);

typedef struct event_keydesc {
  event_t h;
  char desc[0];
} event_keydesc_t;

const char *action_code2str(action_type_t code);

action_type_t action_str2code(const char *str);

int action_update_hold_by_event(int hold, event_t *e);

#define event_is_type(e, et) ((e)->e_type_x == (et))

int event_is_action(event_t *e, action_type_t at);

void event_dispatch(event_t *e);

#endif /* EVENT_H */
