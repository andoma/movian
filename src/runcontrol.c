/*
 *  Run control - Support for standby, auto standby on idle, etc
 *  Copyright (C) 2010 Andreas Öman
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

#include <stdio.h>

#include "misc/callout.h"
#include "htsmsg/htsmsg.h"
#include "htsmsg/htsmsg_store.h"

#include "showtime.h"
#include "settings.h"
#include "runcontrol.h"

static int standby_delay;
static int64_t last_activity;
static int active_media;
static callout_t autostandby_timer;

/**
 * Called from various places to indicate that user is active
 *
 * Defined in showtime.h
 */
void
runcontrol_activity(void)
{
  if(standby_delay)
    last_activity = showtime_get_ts();
}


/**
 *
 */
static void 
set_autostandby(void *opaque, int value)
{
  standby_delay = value;
}


/**
 *
 */
static void
runcontrol_save_settings(void *opaque, htsmsg_t *msg)
{
  htsmsg_store_save(msg, "runcontrol");
}


/**
 * Periodically check if we should auto standby
 */ 
static void
check_autostandby(callout_t *c, void *aux)
{
  int64_t idle = showtime_get_ts() - last_activity;

  idle /= (1000000 * 60); // Convert to minutes

  if(standby_delay && idle >= standby_delay && !active_media) {
    TRACE(TRACE_INFO, "runcontrol", "Automatic standby after %d minutes idle",
	  standby_delay);
    showtime_shutdown(SHOWTIME_EXIT_STANDBY);
    return;
  }
  callout_arm(&autostandby_timer, check_autostandby, NULL, 1);
}


/**
 *
 */
static void
current_media_playstatus(void *opaque, const char *str)
{
  // Reset time to avoid risk of turning off as soon as track playback
  // has ended if UI has been idle
  last_activity = showtime_get_ts();

  active_media = !!str; // If str is something then we're playing, paused, etc
}


/**
 *
 */
static void
init_autostandby(void)
{
  htsmsg_t *store = htsmsg_store_load("runcontrol");
  if(store == NULL)
    store = htsmsg_create_map();

  settings_create_int(settings_general, "autostandby", _p("Automatic standby"), 
		      0, store, 0, 60, 5, set_autostandby, NULL,
		      SETTINGS_INITIAL_UPDATE, " min", NULL,
		      runcontrol_save_settings, NULL);

  last_activity = showtime_get_ts();

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "playstatus"),
		 PROP_TAG_CALLBACK_STRING, current_media_playstatus, NULL,
		 NULL);

  callout_arm(&autostandby_timer, check_autostandby, NULL, 1);
}


static void
do_power_off(void *opaque, prop_event_t event, ...)
{
  showtime_shutdown(SHOWTIME_EXIT_POWEROFF);
}

static void
do_logout(void *opaque, prop_event_t event, ...)
{
  showtime_shutdown(SHOWTIME_EXIT_LOGOUT);
}

static void
do_open_shell(void *opaque, prop_event_t event, ...)
{
  showtime_shutdown(SHOWTIME_EXIT_SHELL);
}

/**
 *
 */
void
runcontrol_init(int can_standby, int can_poweroff,
		int can_logout, int can_open_shell)
{
  prop_t *rc;
  
  if(!(can_standby || can_poweroff || can_logout || can_open_shell))
    return;

  settings_create_divider(settings_general, 
			  _p("Starting and stopping Showtime"));

  rc = prop_create(prop_get_global(), "runcontrol");

  prop_set_int(prop_create(rc, "canStandby"), !!can_standby);
  prop_set_int(prop_create(rc, "canPowerOff"), !!can_poweroff);
  prop_set_int(prop_create(rc, "canLogout"), !!can_logout);
  prop_set_int(prop_create(rc, "canOpenShell"), !!can_open_shell);

  if(can_standby)
    init_autostandby();

  if(can_poweroff)
    settings_create_action(settings_general, _p("Power off system"),
			   do_power_off, NULL, NULL);

  if(can_logout)
    settings_create_action(settings_general, _p("Logout"),
			   do_logout, NULL, NULL);

  if(can_open_shell)
    settings_create_action(settings_general, _p("Open shell"),
			   do_open_shell, NULL, NULL);
}
