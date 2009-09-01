/*
 *  DBUS MPRIS interface
 *  Copyright (C) 2009 Andreas Öman
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
#include <unistd.h>
#include <string.h>

#include "showtime.h"
#include "dbus.h"
#include "event.h"
#include "misc/strtab.h"
#include "ipc/ipc.h"

#define MK_PATH     "/org/gnome/SettingsDaemon/MediaKeys"
#define MK_IFACE    "org.gnome.SettingsDaemon.MediaKeys"
#define MK_GRABKEYS "GrabMediaPlayerKeys"
#define MK_KEYPRESS "MediaPlayerKeyPressed"

#define MK_THISAPP  

static char mkappname[64];
static char grabdest[64];

static int
get_name_owner(DBusConnection *c, const char *name,
	       char *owner, size_t ownerlen)
{
  DBusError err;
  DBusMessage *m, *r;
  char *base_name;
  int e;

  base_name = NULL;
  m = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
				   DBUS_PATH_DBUS,
				   DBUS_INTERFACE_DBUS,
				   "GetNameOwner");
  if(m == NULL)
    return -1;

  if(!dbus_message_append_args(m,
			       DBUS_TYPE_STRING, &name,
			       DBUS_TYPE_INVALID))
    return -1;

  dbus_error_init(&err);
  r = dbus_connection_send_with_reply_and_block(c, m, 2000, &err);

  dbus_message_unref(m);
  if(r != NULL) {

    e = dbus_set_error_from_message(&err, r) ||
      !dbus_message_get_args(r, &err,
			     DBUS_TYPE_STRING, &base_name,
			     DBUS_TYPE_INVALID);

    dbus_message_unref(r);
    if(!e) {
      snprintf(owner, ownerlen, "%s", base_name);
      return 0;
    }
   
  }
  return -1;
}



/**
 *
 */
static void
mpkeys_grab0(DBusConnection *c)
{
  DBusMessage *m;
  DBusMessageIter iter;
  uint32_t t = 0;
  char *appname = mkappname;

  m = dbus_message_new_method_call(grabdest, MK_PATH, MK_IFACE, MK_GRABKEYS);
  if(m == NULL)
    return;

  dbus_message_iter_init_append(m, &iter);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &appname);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &t);

  dbus_connection_send(c, m, NULL);
  dbus_message_unref(m);
}


/**
 * Maps MediaPlayerKeyPressed events to showtime action events
 */
static struct strtab mkactions[] = {
  { "Play",          ACTION_PLAYPAUSE },
  { "Pause",         ACTION_PAUSE },
  { "Stop",          ACTION_STOP },
  { "Previous",      ACTION_PREV_TRACK },
  { "Next",          ACTION_NEXT_TRACK },
  { "FastForward",   ACTION_SEEK_FORWARD },
  { "Rewind",        ACTION_SEEK_BACKWARD },
};


/**
 *
 */
static DBusHandlerResult 
message_handler(DBusConnection *c, DBusMessage *in, void *aux)
{
  DBusError err;
  const char *target, *key;
  int a;

  if(!dbus_message_is_signal(in, MK_IFACE, MK_KEYPRESS))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  dbus_error_init(&err);
  if(!dbus_message_get_args(in, &err, 
			    DBUS_TYPE_STRING, &target,
			    DBUS_TYPE_STRING, &key,
			    DBUS_TYPE_INVALID))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if(strcmp(target, mkappname))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if((a = str2val(key, mkactions)) != -1) 
    event_dispatch(event_create_action(a));

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 *
 */
static const DBusObjectPathVTable vt = {
  .message_function = message_handler
};


DBusConnection *Con;

/**
 *
 */
void
dbus_mpkeys_init(DBusConnection *c)
{
  DBusError err;

  if(get_name_owner(c, "org.gnome.SettingsDaemon", grabdest, sizeof(grabdest)))
    return;
 
  Con = c;
  snprintf(mkappname, sizeof(mkappname), "Showtime-%d", getpid());

  dbus_error_init(&err);

  dbus_bus_add_match(c, "type='signal'", &err);

  dbus_connection_register_object_path(c, MK_PATH, &vt, c);

  mpkeys_grab0(c);
}


void
mpkeys_grab(void)
{
  if(Con)
    mpkeys_grab0(Con);
}
