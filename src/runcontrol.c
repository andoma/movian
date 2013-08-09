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

extern int showtime_can_standby;
extern int showtime_can_poweroff;
extern int showtime_can_open_shell;
extern int showtime_can_logout;

static prop_sub_t *sleeptime_sub;
static prop_t *sleeptime_prop;
static int sleeptime;
static int sleeptimer_enabled;
static callout_t sleep_timer;

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

  setting_create(SETTING_INT, gconf.settings_general, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Automatic standby")),
                 SETTING_HTSMSG("autostandby", store, "runcontrol"),
                 SETTING_WRITE_INT(&standby_delay),
                 SETTING_RANGE(0, 60),
                 SETTING_STEP(5),
                 SETTING_UNIT_CSTR("min"),
                 SETTING_ZERO_TEXT(_p("Off")),
                 NULL);

  last_activity = showtime_get_ts();

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "playstatus"),
		 PROP_TAG_CALLBACK_STRING, current_media_playstatus, NULL,
		 NULL);

  callout_arm(&autostandby_timer, check_autostandby, NULL, 1);
}


/**
 *
 */
static void
update_sleeptime(void *opaque, int v)
{
  TRACE(TRACE_DEBUG, "runcontrol", "Sleep timer set to %d", v);
  sleeptime = v;
}

static void
decrease_sleeptimer(callout_t *c, void *aux)
{
  if(!sleeptimer_enabled)
    return;

  sleeptime--;

  if(sleeptime < 0) {
    TRACE(TRACE_INFO, "runcontrol", "Automatic standby by sleep timer");
    showtime_shutdown(SHOWTIME_EXIT_STANDBY);
    return;
  }
  prop_set_int_ex(sleeptime_prop, sleeptime_sub, sleeptime);
  callout_arm(&sleep_timer, decrease_sleeptimer, NULL, 60);

}


/**
 *
 */
static void
update_sleeptimer(void *opaque, int v)
{
  TRACE(TRACE_DEBUG, "runcontrol", "Sleep timer %s",
        v ? "enabled" : "disabled");
  sleeptimer_enabled = v;
  if(v) {
    prop_set_int(sleeptime_prop, 60);
    callout_arm(&sleep_timer, decrease_sleeptimer, NULL, 60);
  } else {
    callout_disarm(&sleep_timer);
  }
}


/**
 *
 */
static void
init_sleeptimer(prop_t *rc)
{
  const int maxtime = 180;
  sleeptime_prop = prop_create(rc, "sleepTime");
  prop_set_int(sleeptime_prop, 60);
  prop_set_int_clipping_range(sleeptime_prop, 0, maxtime);

  prop_set(rc, "sleepTimeMax",  PROP_SET_INT, maxtime);
  prop_set(rc, "sleepTimeStep", PROP_SET_INT, 5);

  sleeptime_sub =
    prop_subscribe(0,
                   PROP_TAG_CALLBACK_INT, update_sleeptime, NULL,
                   PROP_TAG_ROOT, sleeptime_prop,
                   NULL);

  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
                 PROP_TAG_NAME("global", "runcontrol", "sleepTimer"),
		 PROP_TAG_CALLBACK_INT, update_sleeptimer, NULL,
		 NULL);
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


static void
do_exit(void *opaque, prop_event_t event, ...)
{
  showtime_shutdown(0);
}

/**
 *
 */
void
runcontrol_init(void)
{
  prop_t *rc;

  rc = prop_create(prop_get_global(), "runcontrol");

  prop_set(rc, "canStandby",   PROP_SET_INT,  !!gconf.can_standby);
  prop_set(rc, "canPowerOff",  PROP_SET_INT,  !!gconf.can_poweroff);
  prop_set(rc, "canLogout",    PROP_SET_INT,  !!gconf.can_logout);
  prop_set(rc, "canOpenShell", PROP_SET_INT,  !!gconf.can_open_shell);
  prop_set(rc, "canRestart",   PROP_SET_INT,  !!gconf.can_restart);
  prop_set(rc, "canExit",      PROP_SET_INT,   !gconf.can_not_exit);

  if(!(gconf.can_standby ||
       gconf.can_poweroff ||
       gconf.can_logout ||
       gconf.can_open_shell ||
       gconf.can_restart ||
       !gconf.can_not_exit))
    return;

  settings_create_separator(gconf.settings_general, 
			  _p("Starting and stopping Showtime"));

  if(gconf.can_standby) {
    init_autostandby();
    init_sleeptimer(rc);
  }

  if(gconf.can_poweroff)
    settings_create_action(gconf.settings_general, _p("Power off system"),
			   do_power_off, NULL, 0, NULL);

  if(gconf.can_logout)
    settings_create_action(gconf.settings_general, _p("Logout"),
			   do_logout, NULL, 0, NULL);

  if(gconf.can_open_shell)
    settings_create_action(gconf.settings_general, _p("Open shell"),
			   do_open_shell, NULL, 0, NULL);

  if(!gconf.can_not_exit)
    settings_create_action(gconf.settings_general, _p("Exit Showtime"),
			   do_exit, NULL, 0, NULL);
}
