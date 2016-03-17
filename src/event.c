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
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "main.h"
#include "event.h"
#include "misc/strtab.h"
#include "prop/prop.h"
#include "arch/arch.h"

/**
 *
 */
void *
event_create(event_type_t type, size_t size)
{
  event_t *e = malloc(size);
  e->e_timestamp = arch_get_ts();
  e->e_nav = NULL;
  e->e_dtor = NULL;
  atomic_set(&e->e_refcount, 1);
  e->e_flags = 0;
  e->e_type = type;
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
void *
event_create_int3(event_type_t type, int v1, int v2, int v3)
{
  event_int3_t *e = event_create(type, sizeof(event_int3_t));
  e->val1 = v1;
  e->val2 = v2;
  e->val3 = v3;
  return e;
}


/**
 *
 */
void
event_addref(event_t *e)
{
  atomic_inc(&e->e_refcount);
}


/**
 *
 */
void
event_release(event_t *e)
{
  if(atomic_dec(&e->e_refcount))
    return;
  if(e->e_dtor != NULL)
    e->e_dtor(e);
  prop_ref_dec(e->e_nav);
  free(e);
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
  { "Click",                 ACTION_CLICK },
  { "Enter",                 ACTION_ENTER },
  { "Submit",                ACTION_SUBMIT },
  { "Ok",                    ACTION_OK },
  { "Cancel",                ACTION_CANCEL },
  { "Backspace",             ACTION_BS },
  { "Delete",                ACTION_DELETE },

  { "MoveUp",                ACTION_MOVE_UP },
  { "MoveDown",              ACTION_MOVE_DOWN },
  { "MoveLeft",              ACTION_MOVE_LEFT },
  { "MoveRight",             ACTION_MOVE_RIGHT },

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

  { "PreviousTrack",         ACTION_SKIP_BACKWARD },
  { "NextTrack",             ACTION_SKIP_FORWARD },
  { "SeekForward",           ACTION_SEEK_FORWARD },
  { "SeekReverse",           ACTION_SEEK_BACKWARD },

  { "VolumeUp",              ACTION_VOLUME_UP },
  { "VolumeDown",            ACTION_VOLUME_DOWN },
  { "VolumeMuteToggle",      ACTION_VOLUME_MUTE_TOGGLE },

  { "Menu",                  ACTION_MENU },
  { "ItemMenu",              ACTION_ITEMMENU },
  { "LogWindow",             ACTION_LOGWINDOW },
  { "Select",                ACTION_SELECT },
  { "MediaStats",            ACTION_SHOW_MEDIA_STATS },
  { "Home",                  ACTION_HOME },
  { "Reset",                 ACTION_RESET },

  { "ChangeView",            ACTION_SWITCH_VIEW },
  { "FullscreenToggle",      ACTION_FULLSCREEN_TOGGLE },

  { "Channel+",              ACTION_NEXT_CHANNEL },
  { "Channel-",              ACTION_PREV_CHANNEL },

  { "ZoomUI+",               ACTION_ZOOM_UI_INCR },
  { "ZoomUI-",               ACTION_ZOOM_UI_DECR },
  { "ZoomUIReset",           ACTION_ZOOM_UI_RESET },
  { "ReloadUI",              ACTION_RELOAD_UI },

  { "Restart",               ACTION_RESTART },
  { "Quit",                  ACTION_QUIT },
  { "Standby",               ACTION_STANDBY },
  { "PowerOff",              ACTION_POWER_OFF },
  { "Reboot",                ACTION_REBOOT },

  { "Shuffle",               ACTION_SHUFFLE },
  { "Repeat",                ACTION_REPEAT },

  { "EnableScreenSaver",     ACTION_ENABLE_SCREENSAVER },

  { "AudioTrack",            ACTION_CYCLE_AUDIO },
  { "SubtitleTrack",         ACTION_CYCLE_SUBTITLE },

  { "ReloadData",            ACTION_RELOAD_DATA },
  { "Playqueue",             ACTION_PLAYQUEUE },
  { "Sysinfo",               ACTION_SYSINFO },

  { "SwitchUI",              ACTION_SWITCH_UI },

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
  event_payload_t *ep = event_create(et, sizeof(event_t) + l);
  memcpy(ep->payload, str, l);
  return &ep->h;
}


/**
 *
 */
static void
event_playurl_dtor(event_t *e)
{
  event_playurl_t *ep = (event_playurl_t *)e;
  prop_destroy(ep->item_model);
  free(ep->url);
  free(ep->how);
  free(ep->parent_url);
  prop_ref_dec(ep->parent_model);
}

/**
 *
 */
event_t *
event_create_playurl_args(const event_playurl_args_t *args)
{
  event_playurl_t *ep = event_create(EVENT_PLAY_URL, sizeof(event_playurl_t));
  ep->h.e_dtor = event_playurl_dtor;

  ep->url          = strdup(args->url);
  ep->how          = args->how ? strdup(args->how) : NULL;
  ep->item_model   = prop_xref_addref(args->item_model);
  ep->parent_model = prop_ref_inc(args->parent_model);
  ep->primary      = args->primary;
  ep->priority     = args->priority;
  ep->no_audio     = args->no_audio;
  ep->parent_url   = args->parent_url ? strdup(args->parent_url) : NULL;
  return &ep->h;
}


/**
 *
 */
static void
event_openurl_dtor(event_t *e)
{
  event_openurl_t *ou = (void *)e;
  prop_ref_dec(ou->item_model);
  prop_ref_dec(ou->parent_model);
  free(ou->url);
  free(ou->view);
  free(ou->how);
  free(ou->parent_url);
}


/**
 *
 */
event_t *
event_create_openurl_args(const event_openurl_args_t *args)
{
  event_openurl_t *e = event_create(EVENT_OPENURL, sizeof(event_openurl_t));
  e->h.e_dtor = event_openurl_dtor;

  e->url          = args->url    ? strdup(args->url)            : NULL;
  e->view         = args->view   ? strdup(args->view)           : NULL;
  e->item_model   = prop_ref_inc(args->item_model);
  e->parent_model = prop_ref_inc(args->parent_model);
  e->how          = args->how    ? strdup(args->how)            : NULL;
  e->parent_url   = args->parent_url ? strdup(args->parent_url) : NULL;
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

  if(a == ACTION_invalid)
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

  if(e->e_type != EVENT_ACTION_VECTOR)
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
event_prop_dtor(event_t *e)
{
  event_prop_t *ep = (event_prop_t *)e;
  prop_ref_dec(ep->p);
}


/**
 *
 */
event_t *
event_create_prop(event_type_t type, prop_t *p)
{
  event_prop_t *e = event_create(type, sizeof(event_prop_t));
  e->p = prop_ref_inc(p);
  e->h.e_dtor = event_prop_dtor;
  return &e->h;
}


/**
 *
 */
static void
event_prop_action_dtor(event_t *e)
{
  event_prop_action_t *epa = (event_prop_action_t *)e;
  prop_ref_dec(epa->p);
  rstr_release(epa->action);
}


/**
 *
 */
event_t *
event_create_prop_action(prop_t *p, rstr_t *action)
{
  event_prop_action_t *e = event_create(EVENT_PROP_ACTION,
                                        sizeof(event_prop_action_t));
  e->p = prop_ref_inc(p);
  e->action = rstr_dup(action);
  e->h.e_dtor = event_prop_action_dtor;
  return &e->h;
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
event_to_ui(event_t *e)
{
  event_to_prop(prop_get_by_name(PNVEC("global", "userinterfaces", "ui", "eventSink"),
				 1, NULL), e);
  event_release(e);
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
  
  event_to_prop(prop_get_by_name(PNVEC("global", "eventSink"),
				 1, NULL), e);
  
  if(event_is_action(e, ACTION_NAV_BACK) ||
	    event_is_action(e, ACTION_NAV_FWD) ||
	    event_is_action(e, ACTION_HOME) ||
	    event_is_action(e, ACTION_PLAYQUEUE) ||
	    event_is_action(e, ACTION_RELOAD_DATA) ||
	    event_is_type(e, EVENT_OPENURL)) {
    event_to_prop(prop_get_by_name(PNVEC("global", "navigators", "current", "eventSink"),
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

  } else if(event_is_action(e, ACTION_SEEK_BACKWARD) ||
	    event_is_action(e, ACTION_SEEK_FORWARD) ||
	    event_is_action(e, ACTION_PLAYPAUSE) ||
	    event_is_action(e, ACTION_PLAY) ||
	    event_is_action(e, ACTION_PAUSE) ||
	    event_is_action(e, ACTION_STOP) ||
	    event_is_action(e, ACTION_EJECT) ||
	    event_is_action(e, ACTION_SKIP_BACKWARD) ||
	    event_is_action(e, ACTION_SKIP_FORWARD) ||
	    event_is_action(e, ACTION_SHOW_MEDIA_STATS) ||
	    event_is_action(e, ACTION_SHUFFLE) ||
	    event_is_action(e, ACTION_REPEAT) ||
	    event_is_action(e, ACTION_NEXT_CHANNEL) ||
	    event_is_action(e, ACTION_PREV_CHANNEL) ||
	    event_is_action(e, ACTION_CYCLE_AUDIO) ||
	    event_is_action(e, ACTION_CYCLE_SUBTITLE) ||
	    event_is_type(e, EVENT_DELTA_SEEK_REL) || 
	    event_is_type(e, EVENT_SELECT_AUDIO_TRACK) || 
	    event_is_type(e, EVENT_SELECT_SUBTITLE_TRACK)
	    ) {
    event_to_prop(prop_get_by_name(PNVEC("global", "media", "eventSink"),
				   1, NULL), e);
  } else if(event_is_type(e, EVENT_PLAYTRACK)) {
    event_to_prop(prop_get_by_name(PNVEC("global", "playqueue", "eventSink"),
				   1, NULL), e);

  }

  event_release(e);
}


/**
 *
 */
const static int action_from_fkey[13][2] = {
  { 0, 0 },
  { ACTION_MENU,             ACTION_PLAYQUEUE },
  { ACTION_SHOW_MEDIA_STATS, ACTION_SKIP_BACKWARD },
  { ACTION_ITEMMENU,         ACTION_SKIP_FORWARD },
  { ACTION_LOGWINDOW,        ACTION_ENABLE_SCREENSAVER },

  { ACTION_RELOAD_UI,        ACTION_RELOAD_DATA },
  { ACTION_SYSINFO,          ACTION_CYCLE_SUBTITLE },
  { 0,                        ACTION_SEEK_BACKWARD },
  { ACTION_STOP,              ACTION_PLAYPAUSE },

  { ACTION_SWITCH_VIEW,       ACTION_SEEK_FORWARD },
  { 0,                        ACTION_VOLUME_MUTE_TOGGLE },
  { ACTION_FULLSCREEN_TOGGLE, ACTION_VOLUME_DOWN },
  { ACTION_SWITCH_UI,         ACTION_VOLUME_UP },
};


event_t *
event_from_Fkey(unsigned int keynum, unsigned int mod)
{
  if(keynum < 1 || keynum > 12 || mod > 1)
    return NULL;
  int a = action_from_fkey[keynum][mod];
  if(a == 0)
    return NULL;
  return event_create_action(a);
}

/**
 *
 */
const char *
event_sprint(const event_t *e)
{
  static char buf[64];

  const event_payload_t *ep = (const event_payload_t *)e;
  const event_action_vector_t *eav = (const event_action_vector_t *)e;

  if(e == NULL)
    return "(null)";

  switch(e->e_type) {
  case EVENT_OPENURL:
    {
      const event_openurl_t *eo = (const event_openurl_t *)e;
      snprintf(buf, sizeof(buf), "openurl(%s)", eo->url);
      break;
    }
  case EVENT_DYNAMIC_ACTION:
    return ep->payload;
  case EVENT_ACTION_VECTOR:
    buf[0] = 0;
    for(int i = 0; i < eav->num; i++)
      snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "%s%s",
               i == 0 ? "" : ", ", action_code2str(eav->actions[i]));
    break;
  default:
    snprintf(buf, sizeof(buf), "event<%d>", e->e_type);
    break;
  }
  return buf;
}
