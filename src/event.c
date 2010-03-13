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
#include "prop.h"

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
event_create_unicode(int sym)
{
  event_unicode_t *e = malloc(sizeof(event_unicode_t));
  e->h.e_dtor = event_default_dtor;
  e->h.e_refcount = 1;
  e->h.e_mapped = 0;
  e->h.e_type_x = EVENT_UNICODE;
  e->sym = sym;
  return e;
}


/**
 *
 */
void
event_enqueue(event_queue_t *eq, event_t *e)
{
  atomic_add(&e->e_refcount, 1);
  hts_mutex_lock(&eq->eq_mutex);
  TAILQ_INSERT_TAIL(&eq->eq_q, e, e_link);
  hts_cond_signal(&eq->eq_cond);
  hts_mutex_unlock(&eq->eq_mutex);
}


/**
 *
 * @param timeout Timeout in milliseconds
 */
event_t *
event_get(event_queue_t *eq)
{
  event_t *e;

  hts_mutex_lock(&eq->eq_mutex);

  while((e = TAILQ_FIRST(&eq->eq_q)) == NULL)
    hts_cond_wait(&eq->eq_cond, &eq->eq_mutex);

  TAILQ_REMOVE(&eq->eq_q, e, e_link);
  hts_mutex_unlock(&eq->eq_mutex);
  return e;
}


/**
 *
 */
void
event_unref(event_t *e)
{
  if(atomic_add(&e->e_refcount, -1) == 1)
    e->e_dtor(e);
}


/**
 *
 */
void
event_initqueue(event_queue_t *eq)
{
  TAILQ_INIT(&eq->eq_q);
  hts_cond_init(&eq->eq_cond);
  hts_mutex_init(&eq->eq_mutex);
}


/**
 *
 */
void
event_flushqueue(event_queue_t *eq)
{
  event_t *e;

  hts_mutex_lock(&eq->eq_mutex);

  while((e = TAILQ_FIRST(&eq->eq_q)) != NULL) {
    TAILQ_REMOVE(&eq->eq_q, e, e_link);
    event_unref(e);
  }
  hts_mutex_unlock(&eq->eq_mutex);
}


/**
 *
 */
static struct strtab actionnames[] = {
  { "Up",                    ACTION_UP },
  { "Down",                  ACTION_DOWN },
  { "Left",                  ACTION_LEFT },
  { "Right",                 ACTION_RIGHT },
  { "Enter",                 ACTION_ENTER },
  { "PageUp",                ACTION_PAGE_UP },
  { "PageDown",              ACTION_PAGE_DOWN },
  { "Top",                   ACTION_TOP },
  { "Bottom",                ACTION_BOTTOM },
  { "Stop",                  ACTION_STOP },
  { "PlayPause",             ACTION_PLAYPAUSE },
  { "Play",                  ACTION_PLAY },
  { "Pause",                 ACTION_PAUSE },
  { "VolumeUp",              ACTION_VOLUME_UP },
  { "VolumeDown",            ACTION_VOLUME_DOWN },
  { "VolumeMuteToggle",      ACTION_VOLUME_MUTE_TOGGLE },
  { "Menu",                  ACTION_MENU },
  { "Back",                  ACTION_NAV_BACK },
  { "Forward",               ACTION_NAV_FWD },
  { "Select",                ACTION_SELECT },
  { "Eject",                 ACTION_EJECT },
    { "PreviousTrack",         ACTION_PREV_TRACK },
  { "NextTrack",             ACTION_NEXT_TRACK },
  { "SeekForward",           ACTION_SEEK_FORWARD },
  { "SeekReverse",           ACTION_SEEK_BACKWARD },
  { "Quit",                  ACTION_QUIT },
  { "Standby",               ACTION_STANDBY },
  { "PowerOff",              ACTION_POWER_OFF },
  { "Home",                  ACTION_HOME },
  { "ChangeView",            ACTION_SWITCH_VIEW },
  { "Channel+",              ACTION_CHANNEL_NEXT },
  { "Channel-",              ACTION_CHANNEL_PREV },
  { "FullscreenToggle",      ACTION_FULLSCREEN_TOGGLE },
  { "Increase",              ACTION_INCR },
  { "Decrease",              ACTION_DECR },
  { "MediaStats",            ACTION_SHOW_MEDIA_STATS },
  { "Shuffle",               ACTION_SHUFFLE },
  { "Repeat",                ACTION_REPEAT },
  { "Record",                ACTION_RECORD },

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
event_create_url(event_type_t et, const char *url)
{
  int l = strlen(url) + 1;
  event_t *e = event_create(et, sizeof(event_t) + l);
  memcpy(e->e_payload, url, l);
  return e;
}


/**
 *
 */
static void
event_openurl2_dtor(event_t *e)
{
  event_openurl_t *ou = (void *)e;
  free(ou->url);
  free(ou->type);
  free(ou->parent);
  free(ou);
}


/**
 *
 */
event_t *
event_create_openurl(const char *url, const char *type, const char *parent)
{
  event_openurl_t *e = event_create(EVENT_OPENURL, sizeof(event_openurl_t));

  e->url      = url    ? strdup(url)    : NULL;
  e->type     = type   ? strdup(type)   : NULL;
  e->parent   = parent ? strdup(parent) : NULL;
  e->h.e_dtor = event_openurl2_dtor;
  return &e->h;
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
event_create_select_track(const char *id)
{
  event_select_track_t *e = event_create(EVENT_SELECT_TRACK,
					 sizeof(event_select_track_t));
  e->id = strdup(id);
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

  if(event_is_action(e, ACTION_QUIT)) {
    showtime_shutdown(0);

  } else if(event_is_action(e, ACTION_STANDBY)) {
    showtime_shutdown(10);

  } else if(event_is_action(e, ACTION_POWER_OFF)) {
    showtime_shutdown(11);

  } else if(event_is_action(e, ACTION_NAV_BACK) ||
	    event_is_action(e, ACTION_NAV_FWD) ||
	    event_is_action(e, ACTION_HOME) ||
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
	    event_is_action(e, ACTION_RESTART_TRACK) ||
	    event_is_action(e, ACTION_SHOW_MEDIA_STATS) ||
	    event_is_action(e, ACTION_SHUFFLE) ||
	    event_is_action(e, ACTION_REPEAT) ||
	    event_is_type(e, EVENT_SELECT_TRACK)
	    ) {

    event_to_prop(prop_get_by_name(PNVEC("global", "media", "eventsink"),
				   1, NULL), e);
  }


  event_unref(e);
}

