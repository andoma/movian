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

#include <libglw/glw.h>


typedef enum {
  EVENT_SHOWTIME = GEV_EXT_BASE,

  EVENT_KEYDESC,

  EVENT_AUDIO_CLOCK,
  EVENT_VIDEO_CLOCK,

  EVENT_PLAYLIST,      /* Playlist specific events, see playlist.h */


  EVENT_KEY_CLOSE,
  EVENT_KEY_STOP,
  EVENT_KEY_PLAYPAUSE,
  EVENT_KEY_PLAY,
  EVENT_KEY_PAUSE,
  EVENT_KEY_VOLUME_UP,
  EVENT_KEY_VOLUME_DOWN,
  EVENT_KEY_VOLUME_MUTE_TOGGLE,
  EVENT_KEY_MENU,
  EVENT_KEY_BACK,
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
  EVENT_KEY_TASK_SWITCHER,
  EVENT_KEY_TASK_DOSWITCH,
  EVENT_KEY_SWITCH_VIEW,
} event_type_t;

void event_post(glw_event_t *ge);

void event_post_simple(event_type_t type);

void event_handler_register(int pri, int (*callback)(glw_event_t *ge));


typedef struct event_keydesc {
  glw_event_t h;

  char desc[0];

} event_keydesc_t;


#endif /* EVENT_H */
