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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "showtime.h"
#include "event.h"
#include "misc/strtab.h"
#include "prop/prop.h"

/**
 *
 */
static void
event_default_dtor(event_t *e)
{
  free(e);
}

/**
 *
 */
void *
event_create(event_type_t type, size_t size)
{
  event_t *e = malloc(size);
  e->e_dtor = event_default_dtor;
  e->e_refcount = 1;
  e->e_mapped = 0;
  assert(type > EVENT_OFFSET);
  e->e_type_x = type;
  return e;
}

/**
 *
 */
void *
event_create_int(event_type_t type, int sym)
{
  event_int_t *e = event_create(type, sizeof(event_int_t));
  e->val = sym;
  return e;
}


/**
 *
 */
void
event_addref(event_t *e)
{
  atomic_add(&e->e_refcount, 1);
}


/**
 *
 */
void
event_release(event_t *e)
{
  if(atomic_add(&e->e_refcount, -1) == 1)
    e->e_dtor(e);
}


/**
 *
 */
static struct strtab actionnames[] = {
  { "Up",                    ACTION_UP },
  { "Down",                  ACTION_DOWN },
  { "Left",                  ACTION_LEFT },
  { "Right",                 ACTION_RIGHT },
  { "Activate",              ACTION_ACTIVATE },
  { "Enter",                 ACTION_ENTER },
  { "Ok",                    ACTION_OK },
  { "Cancel",                ACTION_CANCEL },
  { "Backspace",             ACTION_BS },

  { "Forward",               ACTION_NAV_FWD },
  { "Back",                  ACTION_NAV_BACK },

  { "FocusNext",             ACTION_FOCUS_NEXT },
  { "FocusPrev",             ACTION_FOCUS_PREV },

  { "PageUp",                ACTION_PAGE_UP },
  { "PageDown",              ACTION_PAGE_DOWN },

  { "Top",                   ACTION_TOP },
  { "Bottom",                ACTION_BOTTOM },

  { "Increase",              ACTION_INCR },
  { "Decrease",              ACTION_DECR },

  { "Stop",                  ACTION_STOP },
  { "PlayPause",             ACTION_PLAYPAUSE },
  { "Play",                  ACTION_PLAY },
  { "Pause",                 ACTION_PAUSE },
  { "Eject",                 ACTION_EJECT },
  { "Record",                ACTION_RECORD },

  { "PreviousTrack",         ACTION_PREV_TRACK },
  { "NextTrack",             ACTION_NEXT_TRACK },
  { "SeekForward",           ACTION_SEEK_FORWARD },
  { "SeekReverse",           ACTION_SEEK_BACKWARD },
  { "SeekFastForward",       ACTION_SEEK_FAST_FORWARD },
  { "SeekFastReverse",       ACTION_SEEK_FAST_BACKWARD },

  { "VolumeUp",              ACTION_VOLUME_UP },
  { "VolumeDown",            ACTION_VOLUME_DOWN },
  { "VolumeMuteToggle",      ACTION_VOLUME_MUTE_TOGGLE },

  { "Menu",                  ACTION_MENU },
  { "Sysinfo",               ACTION_SYSINFO },
  { "LogWindow",             ACTION_LOGWINDOW },
  { "Select",                ACTION_SELECT },
  { "MediaStats",            ACTION_SHOW_MEDIA_STATS },
  { "Home",                  ACTION_HOME },

  { "ChangeView",            ACTION_SWITCH_VIEW },
  { "FullscreenToggle",      ACTION_FULLSCREEN_TOGGLE },

  { "Channel+",              ACTION_NEXT_CHANNEL },
  { "Channel-",              ACTION_PREV_CHANNEL },

  { "ZoomUI+",               ACTION_ZOOM_UI_INCR },
  { "ZoomUI-",               ACTION_ZOOM_UI_DECR },
  { "ReloadUI",              ACTION_RELOAD_UI },

  { "Quit",                  ACTION_QUIT },
  { "Standby",               ACTION_STANDBY },
  { "PowerOff",              ACTION_POWER_OFF },

  { "Shuffle",               ACTION_SHUFFLE },
  { "Repeat",                ACTION_REPEAT },

  { "EnableScreenSaver",     ACTION_ENABLE_SCREENSAVER },

  { "AudioTrack",            ACTION_CYCLE_AUDIO },
  { "SubtitleTrack",         ACTION_CYCLE_SUBTITLE },

  { "ReloadData",            ACTION_RELOAD_DATA },

};



const char *
action_code2str(action_type_t code)
{
  return val2str(code, actionnames);
}

action_type_t
action_str2code(const char *str)
{
  return str2val(str, actionnames);
}

/**
 *
 */
event_t *
event_create_str(event_type_t et, const char *str)
{
  int l = strlen(str) + 1;
  event_t *e = event_create(et, sizeof(event_t) + l);
  memcpy(e->e_payload, str, l);
  return e;
}


/**
 *
 */
static void
event_playurl_dtor(event_t *e)
{
  event_playurl_t *ep = (void *)e;
  free(ep->url);
  free(ep);
}

/**
 *
 */
event_t *
event_create_playurl(const char *url, int primary, int priority, int no_audio)
{
  event_playurl_t *ep =  event_create(EVENT_PLAY_URL, sizeof(event_playurl_t));
  ep->url = strdup(url);
  ep->primary = primary;
  ep->priority = priority;
  ep->no_audio = no_audio;
  ep->h.e_dtor = event_playurl_dtor;
  return &ep->h;
}


/**
 *
 */
static void
event_openurl_dtor(event_t *e)
{
  event_openurl_t *ou = (void *)e;
  if(ou->origin != NULL)
    prop_ref_dec(ou->origin);
  free(ou->url);
  free(ou->view);
  free(ou);
}


/**
 *
 */
event_t *
event_create_openurl(const char *url, const char *view, prop_t *origin)
{
  event_openurl_t *e = event_create(EVENT_OPENURL, sizeof(event_openurl_t));

  e->url      = url    ? strdup(url)          : NULL;
  e->view     = view   ? strdup(view)         : NULL;
  e->origin   = prop_ref_inc(origin);

  e->h.e_dtor = event_openurl_dtor;
  return &e->h;
}


/**
 *
 */
static void
playtrack_dtor(event_t *e)
{
  event_playtrack_t *ep = (event_playtrack_t *)e;
  prop_destroy(ep->track);
  if(ep->source != NULL)
    prop_destroy(ep->source);
  free(ep);
}


/**
 *
 */
event_t *
event_create_playtrack(struct prop *track, struct prop *psource, int mode)
{
  event_playtrack_t *ep = event_create(EVENT_PLAYTRACK, 
				       sizeof(event_playtrack_t));

  ep->track    = prop_xref_addref(track);
  ep->source   = psource ? prop_xref_addref(psource) : NULL;
  ep->mode     = mode;
  ep->h.e_dtor = playtrack_dtor;
  return &ep->h;
}


/**
 *
 */
static void
event_select_track_dtor(event_t *e)
{
  event_select_track_t *est = (void *)e;
  free(est->id);
  free(e);
}


/**
 *
 */
event_t *
event_create_select_track(const char *id, event_type_t type, int manual)
{
  event_select_track_t *e = event_create(type, sizeof(event_select_track_t));
  e->id = strdup(id);
  e->manual = manual;
  e->h.e_dtor = event_select_track_dtor;
  return &e->h;
}


/**
 *
 */
int
action_update_hold_by_event(int hold, event_t *e)
{
  if(event_is_action(e, ACTION_PLAYPAUSE))
    return !hold;
  
  if(event_is_action(e, ACTION_PAUSE))
    return 1;

  if(event_is_action(e, ACTION_PLAY))
    return 0;

  return 0;
}


/**
 *
 */
event_t *
event_create_action_multi(const action_type_t *actions, size_t numactions)
{
  event_action_vector_t *eav;
  int s = sizeof(action_type_t) * numactions;

  eav = event_create(EVENT_ACTION_VECTOR, sizeof(event_action_vector_t) + s);
  memcpy(eav->actions, actions, s);
  eav->num = numactions;
  return &eav->h;
}


/**
 *
 */
event_t *
event_create_action(action_type_t action)
{
  return event_create_action_multi(&action, 1);
}



/**
 *
 */
event_t *
event_create_action_str(const char *str)
{
  action_type_t a = action_str2code(str);

  if(a == -1)
    return event_create_str(EVENT_DYNAMIC_ACTION, str);
  return event_create_action(a);
}


/**
 *
 */
int
event_is_action(event_t *e, action_type_t at)
{
  int i;
  event_action_vector_t *eav;

  if(e->e_type_x != EVENT_ACTION_VECTOR)
    return 0;

  eav = (event_action_vector_t *)e;

  for(i = 0; i < eav->num; i++)
    if(eav->actions[i] == at)
      return 1;
  return 0;
}


/**
 *
 */
static void
event_to_prop(prop_t *p, event_t *e)
{
  prop_send_ext_event(p, e);
  prop_ref_dec(p);
}


/**
 *
 */
void
event_dispatch(event_t *e)
{
  prop_t *p;
  event_int_t *eu = (event_int_t *)e;

  if(event_is_type(e, EVENT_UNICODE) && eu->val == 32) {
    // Convert [space] into playpause
    event_release(e);
    e = event_create_action(ACTION_PLAYPAUSE);
  }
  
  if(event_is_action(e, ACTION_QUIT)) {
    showtime_shutdown(0);

  } else if(event_is_action(e, ACTION_STANDBY)) {
    showtime_shutdown(10);

  } else if(event_is_action(e, ACTION_POWER_OFF)) {
    showtime_shutdown(11);

  } else if(event_is_action(e, ACTION_NAV_BACK) ||
	    event_is_action(e, ACTION_NAV_FWD) ||
	    event_is_action(e, ACTION_HOME) ||
	    event_is_action(e, ACTION_RELOAD_DATA) ||
	    event_is_type(e, EVENT_OPENURL)) {

    event_to_prop(prop_get_by_name(PNVEC("global", "nav", "eventsink"),
				   1, NULL), e);

  } else if(event_is_action(e, ACTION_VOLUME_UP) ||
	    event_is_action(e, ACTION_VOLUME_DOWN)) {

    p = prop_get_by_name(PNVEC("global", "audio", "mastervolume"), 1, NULL);
    prop_add_float(p, event_is_action(e, ACTION_VOLUME_DOWN) ? -1 : 1);
    prop_ref_dec(p);
    
  } else if(event_is_action(e, ACTION_VOLUME_MUTE_TOGGLE)) {

    p = prop_get_by_name(PNVEC("global", "audio", "mastermute"), 1, NULL);
    prop_toggle_int(p);
    prop_ref_dec(p);

  } else if(event_is_action(e, ACTION_SEEK_FAST_BACKWARD) ||
	    event_is_action(e, ACTION_SEEK_BACKWARD) ||
	    event_is_action(e, ACTION_SEEK_FAST_FORWARD) ||
	    event_is_action(e, ACTION_SEEK_FORWARD) ||
	    event_is_action(e, ACTION_PLAYPAUSE) ||
	    event_is_action(e, ACTION_PLAY) ||
	    event_is_action(e, ACTION_PAUSE) ||
	    event_is_action(e, ACTION_STOP) ||
	    event_is_action(e, ACTION_EJECT) ||
	    event_is_action(e, ACTION_PREV_TRACK) ||
	    event_is_action(e, ACTION_NEXT_TRACK) ||
	    event_is_action(e, ACTION_SHOW_MEDIA_STATS) ||
	    event_is_action(e, ACTION_SHUFFLE) ||
	    event_is_action(e, ACTION_REPEAT) ||
	    event_is_action(e, ACTION_NEXT_CHANNEL) ||
	    event_is_action(e, ACTION_PREV_CHANNEL) ||
	    event_is_action(e, ACTION_CYCLE_AUDIO) ||
	    event_is_action(e, ACTION_CYCLE_SUBTITLE) ||
	    event_is_type(e, EVENT_SELECT_AUDIO_TRACK) || 
	    event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)
	    ) {

    event_to_prop(prop_get_by_name(PNVEC("global", "media", "eventsink"),
				   1, NULL), e);
  } else if(event_is_type(e, EVENT_PLAYTRACK)) {
    event_to_prop(prop_get_by_name(PNVEC("global", "playqueue", "eventsink"),
				   1, NULL), e);

  }

  event_release(e);
}

