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

#ifndef EVENT_H
#define EVENT_H

#include <inttypes.h>
#include <queue.h>
#include <arch/threads.h>
#include <arch/atomic.h>


typedef enum {
  EVENT_NONE = 0,
  EVENT_UP,
  EVENT_DOWN,
  EVENT_LEFT,
  EVENT_RIGHT,
  EVENT_ENTER,

  EVENT_FOCUS_NEXT,  /* TAB */
  EVENT_FOCUS_PREV,  /* Shift + TAB */

  EVENT_INCR,
  EVENT_DECR,

  EVENT_OK,
  EVENT_CANCEL,

  EVENT_BACKSPACE,


  EVENT_CLOSE,
  EVENT_STOP,
  EVENT_PLAYPAUSE,
  EVENT_PLAY,
  EVENT_PAUSE,
  EVENT_VOLUME_UP,
  EVENT_VOLUME_DOWN,
  EVENT_VOLUME_MUTE_TOGGLE,
  EVENT_MENU,
  EVENT_SELECT,
  EVENT_EJECT,
  EVENT_RESTART_TRACK,
  EVENT_POWER,
  EVENT_QUIT,
  EVENT_PREV,
  EVENT_NEXT,
  EVENT_SEEK_FORWARD,
  EVENT_SEEK_BACKWARD,
  EVENT_SEEK_FAST_FORWARD,
  EVENT_SEEK_FAST_BACKWARD,
  EVENT_MAINMENU,
  EVENT_SWITCH_VIEW,
  EVENT_CHANNEL_PLUS,
  EVENT_CHANNEL_MINUS,
  EVENT_FULLSCREEN_TOGGLE,

  EVENT_last_mappable,


  EVENT_UNICODE,
  EVENT_GENERIC,
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
  event_type_t e_type;
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
typedef struct event_generic {
  event_t h;
  char *target;
  char *method;
  char *argument;
} event_generic_t;


/**
 *
 */
typedef struct event_seek {
  event_t h;
  int64_t ts;
} event_seek_t;


void event_generic_dtor(event_t *e);

void *event_create(event_type_t type, size_t size);

#define event_create_simple(type) event_create(type, sizeof(event_t))

void *event_create_unicode(int sym);

void event_enqueue(event_queue_t *eq, event_t *e);

event_t *event_get(int timeout, event_queue_t *eq);

void event_unref(event_t *e);

void event_initqueue(event_queue_t *eq);

void event_flushqueue(event_queue_t *eq);

event_t *event_create_url(event_type_t et, const char *url);


/**
 * The last entry in this list will be called first
 */
typedef enum {
  EVENTPRI_NAV,
  EVENTPRI_MAIN,
  EVENTPRI_SPEEDBUTTONS,

  EVENTPRI_MEDIACONTROLS_PLAYQUEUE,
  EVENTPRI_MEDIACONTROLS_SLIDESHOW,
  EVENTPRI_MEDIACONTROLS_VIDEOPLAYBACK,

  EVENTPRI_AUDIO_MIXER,
} eventpri_t;

void event_post(event_t *ge);

void event_post_simple(event_type_t type);

void *event_handler_register(const char *name, int (*callback)(event_t *ge,
							       void *opaque),
			     eventpri_t pri, void *opaque);

void event_handler_unregister(void *ih);

typedef struct event_keydesc {
  event_t h;
  char desc[0];
} event_keydesc_t;

void event_init(void);

const char *event_code2str(event_type_t code);
event_type_t event_str2code(const char *str);


#endif /* EVENT_H */
