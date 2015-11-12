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
#ifndef EVENT_H__
#define EVENT_H__

#include <inttypes.h>
#include <stddef.h>
#include "misc/queue.h"
#include "arch/threads.h"
#include "arch/atomic.h"

struct prop;
struct rstr;

typedef enum {

  ACTION_NONE = 0,

  ACTION_mappable_begin,

  ACTION_UP,
  ACTION_DOWN,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_ACTIVATE,
  ACTION_ENTER,
  ACTION_SUBMIT,
  ACTION_OK,
  ACTION_CANCEL,
  ACTION_BS,
  ACTION_DELETE,

  ACTION_FOCUS_NEXT,  /* TAB */
  ACTION_FOCUS_PREV,  /* Shift + TAB */

  ACTION_MOVE_UP,       // Move item
  ACTION_MOVE_DOWN,
  ACTION_MOVE_LEFT,
  ACTION_MOVE_RIGHT,

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

  ACTION_SKIP_FORWARD,
  ACTION_SKIP_BACKWARD,

  ACTION_SEEK_FORWARD,
  ACTION_SEEK_BACKWARD,

  ACTION_VOLUME_UP,
  ACTION_VOLUME_DOWN,
  ACTION_VOLUME_MUTE_TOGGLE,

  ACTION_MENU,
  ACTION_ITEMMENU,
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
  ACTION_ZOOM_UI_RESET,
  ACTION_RELOAD_UI,

  ACTION_QUIT,
  ACTION_STANDBY,
  ACTION_POWER_OFF,
  ACTION_REBOOT,


  ACTION_SHUFFLE,
  ACTION_REPEAT,

  ACTION_ENABLE_SCREENSAVER,

  ACTION_CYCLE_AUDIO,
  ACTION_CYCLE_SUBTITLE,

  ACTION_RELOAD_DATA,

  ACTION_PLAYQUEUE,

  ACTION_SYSINFO,

  ACTION_SWITCH_UI,

  ACTION_RECORD_UI,

  ACTION_mappable_end,

  ACTION_invalid = -1,
} action_type_t;



/**
 * These numbers are sent over the wire (STPP) so don't change them
 */
typedef enum {
  EVENT_ACTION_VECTOR                 = 1,
  EVENT_UNICODE                       = 2,
  EVENT_KEYDESC                       = 3,
  EVENT_PLAYQUEUE_JUMP                = 4,
  EVENT_PLAYQUEUE_JUMP_AND_PAUSE      = 5,
  EVENT_SEEK                          = 6,
  EVENT_DELTA_SEEK_REL                = 7,
  EVENT_EOF                           = 8,
  EVENT_PLAY_URL                      = 9,
  EVENT_EXIT                          = 10,
  EVENT_DVD_PCI                       = 11,
  EVENT_DVD_SELECT_BUTTON             = 12,
  EVENT_DVD_ACTIVATE_BUTTON           = 13, // "Press" button
  EVENT_OPENURL                       = 14,
  EVENT_PLAYTRACK                     = 15, // For playqueue
  EVENT_INTERNAL_PAUSE                = 16, // Carries a string as a reason
  EVENT_CURRENT_TIME                  = 17,
  EVENT_SELECT_AUDIO_TRACK            = 18,
  EVENT_SELECT_SUBTITLE_TRACK         = 19,
  EVENT_PLAYBACK_PRIORITY             = 20, // 0 = best
  EVENT_STOP_UI                       = 21,
  EVENT_HOLD                          = 22,
  EVENT_REPAINT_UI                    = 23,
  EVENT_REOPEN                        = 24,
  EVENT_REDIRECT                      = 25,
  EVENT_PROPREF                       = 26,
  EVENT_DYNAMIC_ACTION                = 27,
  EVENT_MAKE_SCREENSHOT               = 28,
  EVENT_PROP_ACTION                   = 29,
} event_type_t;



TAILQ_HEAD(event_q, event);


/**
 *
 */
typedef struct event {
  atomic_t e_refcount;
  int     e_flags;
#define EVENT_MAPPED 0x1
#define EVENT_KEYPRESS 0x2 // Came from user keypress
  struct prop *e_nav;
  event_type_t e_type;
  void (*e_dtor)(struct event *e);
  TAILQ_ENTRY(event) e_link;
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
typedef struct event_payload {
  event_t h;
  char payload[0];
} event_payload_t;


/**
 *
 */
typedef struct event_int3 {
  event_t h;
  int val1;
  int val2;
  int val3;
} event_int3_t;


/**
 *
 */
typedef struct event_openurl {
  event_t h;
  char *url;
  char *view;
  struct prop *item_model;
  struct prop *parent_model;
  char *how;
  char *parent_url;
} event_openurl_t;


/**
 *
 */
typedef struct event_openurl_args {
  const char *url;
  const char *view;
  struct prop *item_model;
  struct prop *parent_model;
  const char *how;
  const char *parent_url;
} event_openurl_args_t;



/**
 *
 */
typedef struct event_playurl {
  event_t h;
  char *url;
  int primary;
  int priority;
  int no_audio;
  struct prop *item_model;
  struct prop *parent_model;
  char *how;
  char *parent_url;
} event_playurl_t;


/**
 *
 */
typedef struct event_playurl_args {
  const char *url;
  int primary;
  int priority;
  int no_audio;
  struct prop *item_model;
  struct prop *parent_model;
  const char *how;
  const char *parent_url;
} event_playurl_args_t;


/**
 *
 */
typedef struct event_select_track {
  event_t h;
  int manual;   /* Set iff it was initiated by user,
		   otherwise it was suggested automatically */
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


/**
 *
 */
typedef struct event_prop {
  event_t h;
  struct prop *p;
} event_prop_t;


/**
 *
 */
typedef struct event_prop_action {
  event_t h;
  struct prop *p;
  struct rstr *action;
} event_prop_action_t;

void event_generic_dtor(event_t *e);

void *event_create(event_type_t type, size_t size);

event_t *event_create_action(action_type_t action);

event_t *event_create_action_multi(const action_type_t *actions,
                                   size_t numactions);

event_t *event_create_action_str(const char *str);

#define event_create_type(type) event_create(type, sizeof(event_t))

void *event_create_int(event_type_t type, int val);

void *event_create_int3(event_type_t type, int val1, int val2, int val3);

void event_release(event_t *e);

void event_addref(event_t *e);

event_t *event_create_str(event_type_t et, const char *url);

event_t *event_create_playurl_args(const event_playurl_args_t *args);

#define event_create_playurl(x, ...) \
  event_create_playurl_args(&(const event_playurl_args_t) { x, ##__VA_ARGS__})

event_t *event_create_openurl_args(const event_openurl_args_t *args);

#define event_create_openurl(x, ...) \
  event_create_openurl_args(&(const event_openurl_args_t) { x, ##__VA_ARGS__})

event_t *event_create_playtrack(struct prop *track,
				struct prop *psource,
				int mode);

event_t *event_create_select_track(const char *id, event_type_t type, 
				   int manual);

event_t *event_create_prop(event_type_t type, struct prop *p);

event_t *event_create_prop_action(struct prop *p, struct rstr *action);

const char *action_code2str(action_type_t code);

action_type_t action_str2code(const char *str);

int action_update_hold_by_event(int hold, event_t *e);

#define event_is_type(e, et) ((e)->e_type == (et))

int event_is_action(event_t *e, action_type_t at);

void event_dispatch(event_t *e);

event_t *event_from_Fkey(unsigned int keynum, unsigned int mod);

void event_to_ui(event_t *e);

#if 1
const char *event_sprint(const event_t *e);
#endif

#endif /* EVENT_H */
