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
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include "misc/callout.h"
#include "htsmsg/htsmsg.h"

#include "main.h"
#include "settings.h"
#include "runcontrol.h"

static int standby_delay;
static int64_t last_activity;
static int active_media;
static callout_t autostandby_timer;

static prop_sub_t *sleeptime_sub;
static prop_t *sleeptime_prop;
static int sleeptime;
static int sleeptimer_enabled;
static callout_t sleep_timer;


/**
 * Called from various places to indicate that user is active
 *
 * Defined in main.h
 */
void
runcontrol_activity(void)
{
  if(standby_delay)
    last_activity = arch_get_ts();
}


/**
 * Periodically check if we should auto standby
 */ 
static void
check_autostandby(callout_t *c, void *aux)
{
  int64_t idle = arch_get_ts() - last_activity;

  idle /= (1000000 * 60); // Convert to minutes

  if(standby_delay && idle >= standby_delay && !active_media) {
    TRACE(TRACE_INFO, "runcontrol", "Automatic standby after %d minutes idle",
	  standby_delay);
    app_shutdown(APP_EXIT_STANDBY);
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
  last_activity = arch_get_ts();

  active_media = !!str; // If str is something then we're playing, paused, etc
}


/**
 *
 */
static void
init_autostandby(void)
{
  prop_t *dir = setting_get_dir("general:runcontrol");

  setting_create(SETTING_INT, dir, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Automatic standby")),
                 SETTING_STORE("runcontrol", "autostandby"),
                 SETTING_WRITE_INT(&standby_delay),
                 SETTING_RANGE(0, 60),
                 SETTING_STEP(5),
                 SETTING_UNIT_CSTR("min"),
                 SETTING_ZERO_TEXT(_p("Off")),
                 NULL);

  last_activity = arch_get_ts();

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
    app_shutdown(APP_EXIT_STANDBY);
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
  app_shutdown(APP_EXIT_POWEROFF);
}

static void
do_logout(void *opaque, prop_event_t event, ...)
{
  app_shutdown(APP_EXIT_LOGOUT);
}

static void
do_open_shell(void *opaque, prop_event_t event, ...)
{
  app_shutdown(APP_EXIT_SHELL);
}

static void
do_standby(void *opaque, prop_event_t event, ...)
{
  app_shutdown(APP_EXIT_STANDBY);
}


static void
do_exit(void *opaque, prop_event_t event, ...)
{
  app_shutdown(0);
}


/**
 *
 */
static void
set_ssh_server(void *opaque, int on)
{
  char cmd = on ? 1 : 2;
  if(write(gconf.shell_fd, &cmd, 1) != 1) {
    TRACE(TRACE_ERROR, "SSHD", "Unable to send cmd -- %s", strerror(errno));
  }
}


/**
 *
 */
static void
runcontrol_global_eventsink(void *opaque, event_t *e)
{
  if(event_is_action(e, ACTION_QUIT)) {
    app_shutdown(0);

  } else if(event_is_action(e, ACTION_STANDBY)) {
    app_shutdown(APP_EXIT_STANDBY);

  } else if(event_is_action(e, ACTION_POWER_OFF)) {
    app_shutdown(APP_EXIT_POWEROFF);

  } else if(event_is_action(e, ACTION_RESTART)) {
    app_shutdown(APP_EXIT_RESTART);

  } else if(event_is_action(e, ACTION_REBOOT)) {
    app_shutdown(APP_EXIT_REBOOT);
  }
}

/**
 *
 */
void
runcontrol_init(void)
{
  prop_t *rc;

  rc = prop_create(prop_get_global(), "runcontrol");

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "eventSink"),
		 PROP_TAG_CALLBACK_EVENT, runcontrol_global_eventsink, NULL,
		 NULL);

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

  prop_t *dir = setting_get_dir("general:runcontrol");

  if(gconf.can_standby) {
    init_autostandby();
    init_sleeptimer(rc);
    settings_create_action(dir, _p("Standby"), NULL,
			   do_standby, NULL, 0, NULL);
  }

  if(gconf.can_poweroff)
    settings_create_action(dir, _p("Power off system"), NULL,
			   do_power_off, NULL, 0, NULL);

  if(gconf.can_logout)
    settings_create_action(dir, _p("Logout"), NULL,
			   do_logout, NULL, 0, NULL);

  if(gconf.can_open_shell)
    settings_create_action(dir, _p("Open shell"), NULL,
			   do_open_shell, NULL, 0, NULL);

  if(!gconf.can_not_exit)
    settings_create_action(dir, _p("Quit"), NULL,
			   do_exit, NULL, 0, NULL);

  if(gconf.shell_fd > 0) {

    settings_create_separator(gconf.settings_network, _p("SSH server"));

    setting_create(SETTING_BOOL, gconf.settings_network,SETTINGS_INITIAL_UPDATE,
		   SETTING_TITLE(_p("Enable SSH server")),
		   SETTING_VALUE(0),
		   SETTING_CALLBACK(set_ssh_server, NULL),
		   SETTING_STORE("runcontrol", "sshserver"),
		   NULL);
  }
}
