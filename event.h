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

#include <libhts/htsq.h>
#include <libhts/htsthreads.h>
#include <libhts/htsatomic.h>


typedef enum {
  EVENT_NONE = 0,
  EVENT_UP,
  EVENT_DOWN,
  EVENT_LEFT,
  EVENT_RIGHT,
  EVENT_ENTER,

  EVENT_INCR,
  EVENT_DECR,

  EVENT_OK,
  EVENT_CANCEL,

  EVENT_BACKSPACE,
  EVENT_UNICODE,

  EVENT_GENERIC,



  EVENT_KEYDESC,

  EVENT_AUDIO_CLOCK,
  EVENT_VIDEO_CLOCK,

  EVENT_RECONFIGURE,   /* Special event to force apps to reconfiguring
			  themselfs */

  EVENT_PLAYLIST,      /* Playlist specific events, see playlist.h */
  EVENT_TV,            /* TV specific events, see tv.h */


  EVENT_KEY_CLOSE,
  EVENT_KEY_STOP,
  EVENT_KEY_PLAYPAUSE,
  EVENT_KEY_PLAY,
  EVENT_KEY_PAUSE,
  EVENT_KEY_VOLUME_UP,
  EVENT_KEY_VOLUME_DOWN,
  EVENT_KEY_VOLUME_MUTE_TOGGLE,
  EVENT_KEY_MENU,
  EVENT_KEY_ENTER,
  EVENT_KEY_SELECT,
  EVENT_KEY_EJECT,
  EVENT_KEY_RESTART_TRACK,
  EVENT_KEY_POWER,
  EVENT_KEY_QUIT,
  EVENT_KEY_PREV,
  EVENT_KEY_NEXT,
  EVENT_KEY_SEEK_FORWARD,
  EVENT_KEY_SEEK_BACKWARD,
  EVENT_KEY_SEEK_FAST_FORWARD,
  EVENT_KEY_SEEK_FAST_BACKWARD,
  EVENT_KEY_MAINMENU,
  EVENT_KEY_SWITCH_VIEW,
  EVENT_KEY_CHANNEL_PLUS,
  EVENT_KEY_CHANNEL_MINUS,
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

void event_generic_dtor(event_t *e);



void *event_create(event_type_t type, size_t size);

#define event_create_simple(type) event_create(type, sizeof(event_t))

void *event_create_unicode(int sym);

void event_enqueue(event_queue_t *eq, event_t *e);

event_t *event_get(int timeout, event_queue_t *eq);

void event_unref(event_t *e);

void event_initqueue(event_queue_t *eq);

void event_flushqueue(event_queue_t *eq);












/**
 * The last entry in this list will be called first
 */
typedef enum {
  EVENTPRI_NAV,
  EVENTPRI_MAIN,
  EVENTPRI_SPEEDBUTTONS,

  EVENTPRI_MEDIACONTROLS_PLAYLIST,
  EVENTPRI_MEDIACONTROLS_SLIDESHOW,
  EVENTPRI_MEDIACONTROLS_VIDEOPLAYBACK,

  EVENTPRI_AUDIO_MIXER,
  EVENTPRI_KEYMAPPER,
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


#endif /* EVENT_H */
