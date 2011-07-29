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
#include <stddef.h>
#include "misc/queue.h"
#include "arch/threads.h"
#include "arch/atomic.h"

struct prop;

typedef enum {

  ACTION_NONE = 0,

  ACTION_mappable_begin,

  ACTION_UP,
  ACTION_DOWN,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_ACTIVATE,
  ACTION_ENTER,
  ACTION_OK,
  ACTION_CANCEL,
  ACTION_BS,

  ACTION_FOCUS_NEXT,  /* TAB */
  ACTION_FOCUS_PREV,  /* Shift + TAB */


  ACTION_NAV_FWD,
  ACTION_NAV_BACK,

  ACTION_PAGE_UP,
  ACTION_PAGE_DOWN,

  ACTION_TOP,
  ACTION_BOTTOM,

  ACTION_INCR,
  ACTION_DECR,

  ACTION_STOP,
  ACTION_PLAYPAUSE,
  ACTION_PLAY,
  ACTION_PAUSE,
  ACTION_EJECT,
  ACTION_RECORD,

  ACTION_PREV_TRACK,
  ACTION_NEXT_TRACK,
  ACTION_SEEK_FORWARD,
  ACTION_SEEK_BACKWARD,
  ACTION_SEEK_FAST_FORWARD,
  ACTION_SEEK_FAST_BACKWARD,

  ACTION_VOLUME_UP,
  ACTION_VOLUME_DOWN,
  ACTION_VOLUME_MUTE_TOGGLE,

  ACTION_MENU,
  ACTION_SYSINFO,
  ACTION_LOGWINDOW,
  ACTION_SELECT,
  ACTION_SHOW_MEDIA_STATS,
  ACTION_HOME,

  ACTION_SWITCH_VIEW,
  ACTION_FULLSCREEN_TOGGLE,

  ACTION_NEXT_CHANNEL,
  ACTION_PREV_CHANNEL,

  ACTION_ZOOM_UI_INCR,
  ACTION_ZOOM_UI_DECR,
  ACTION_RELOAD_UI,

  ACTION_QUIT,
  ACTION_STANDBY,
  ACTION_POWER_OFF,


  ACTION_SHUFFLE,
  ACTION_REPEAT,

  ACTION_ENABLE_SCREENSAVER,

  ACTION_CYCLE_AUDIO,
  ACTION_CYCLE_SUBTITLE,

  ACTION_RELOAD_DATA,

  ACTION_mappable_end,
} action_type_t;



typedef enum {
  EVENT_OFFSET = 5000,
  EVENT_ACTION_VECTOR,
  EVENT_UNICODE,
  EVENT_KEYDESC,
  EVENT_AUDIO_CLOCK,
  EVENT_VIDEO_CLOCK,
  EVENT_PLAYQUEUE_JUMP,
  EVENT_PLAYQUEUE_JUMP_AND_PAUSE,
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
  EVENT_PLAYTRACK,            /* For playqueue */

  EVENT_MP_NO_LONGER_PRIMARY, // Carries a string as a reason
  EVENT_MP_IS_PRIMARY,
  EVENT_INTERNAL_PAUSE,       // Carries a string as a reason

  EVENT_CURRENT_PTS,

  EVENT_SELECT_AUDIO_TRACK,
  EVENT_SELECT_SUBTITLE_TRACK,

  EVENT_PLAYBACK_PRIORITY,   // 0 = best, higher value == less important 

  EVENT_DYNAMIC_ACTION,

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
typedef struct event_int {
  event_t h;
  int val;
} event_int_t;



/**
 *
 */
typedef struct event_openurl {
  event_t h;
  char *url;
  char *view;
  struct prop *origin;
} event_openurl_t;



/**
 *
 */
typedef struct event_playurl {
  event_t h;
  char *url;
  int primary;
  int priority;
  int no_audio;
} event_playurl_t;


/**
 *
 */
typedef struct event_select_track {
  event_t h;
  int manual;   /* Set iff it was initiated by user, 
		   otherwise it was suggested by showtime itself */
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


/**
 *
 */
typedef struct event_playtrack {
  event_t h;
  struct prop *track;
  struct prop *source;
  int mode;
} event_playtrack_t;


void event_generic_dtor(event_t *e);

void *event_create(event_type_t type, size_t size);

event_t *event_create_action(action_type_t action);

event_t *event_create_action_multi(const action_type_t *actions, size_t numactions);

event_t *event_create_action_str(const char *str);

#define event_create_type(type) event_create(type, sizeof(event_t))

void *event_create_int(event_type_t type, int val);

void event_release(event_t *e);

void event_addref(event_t *e);

event_t *event_create_str(event_type_t et, const char *url);

event_t *event_create_playurl(const char *url, int primary, int priority, int no_audio);

event_t *event_create_openurl(const char *url, const char *view,
			      struct prop *origin);

event_t *event_create_playtrack(struct prop *track,
				struct prop *psource,
				int mode);

event_t *event_create_select_track(const char *id, event_type_t type, 
				   int manual);

const char *action_code2str(action_type_t code);

action_type_t action_str2code(const char *str);

int action_update_hold_by_event(int hold, event_t *e);

#define event_is_type(e, et) ((e)->e_type_x == (et))

int event_is_action(event_t *e, action_type_t at);

void event_dispatch(event_t *e);

#endif /* EVENT_H */
