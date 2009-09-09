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
#include "event.h"

#include "mpris.h"
#include "dbus.h"
#include "prop.h"

static dbus_int32_t mpris_playstatus = 2; // stop

/**
 *
 */
struct method {
  const char *name;
  DBusHandlerResult (*handler)(DBusConnection *c, DBusMessage *in, 
			       DBusMessageIter *args, void *aux, int v);
  void *aux;
  int value;
};


/**
 *
 */
struct obj {
  const char *introspection;
  struct method *methods;
  int nmethods;
};




/**
 *
 */
static DBusHandlerResult
GetLength(DBusConnection *c, DBusMessage *in,
	  DBusMessageIter *args, void *aux, int v)
{
  dbus_int32_t len = 0;

  if(!dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &len))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  return DBUS_HANDLER_RESULT_HANDLED;
}



/**
 *
 */
static DBusHandlerResult
Identity(DBusConnection *c, DBusMessage *in,
	  DBusMessageIter *args, void *aux, int v)
{
  char buf[100];
  char *str = buf;
  extern const char *htsversion;

  snprintf(buf, sizeof(buf), "HTS Showtime %s", htsversion);

  if(!dbus_message_iter_append_basic(args, DBUS_TYPE_STRING, &str))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
MprisVersion(DBusConnection *c, DBusMessage *in,
	     DBusMessageIter *args, void *aux, int v)
{
  dbus_uint16_t major = 1;
  dbus_uint16_t minor = 0;

  DBusMessageIter version;
  
  if(!dbus_message_iter_open_container(args, DBUS_TYPE_STRUCT, NULL,
				       &version))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  if(!dbus_message_iter_append_basic(&version, DBUS_TYPE_UINT16, &major))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  if(!dbus_message_iter_append_basic(&version, DBUS_TYPE_UINT16, &minor))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  if(!dbus_message_iter_close_container(args, &version))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
Quit(DBusConnection *c, DBusMessage *in,
     DBusMessageIter *args, void *aux, int v)
{
  showtime_shutdown(0);
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
doAction(DBusConnection *c, DBusMessage *in,
	 DBusMessageIter *args, void *aux, int v)
{
   event_dispatch(event_create_action(v));
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
GetStatus(DBusConnection *c, DBusMessage *in,
	  DBusMessageIter *args, void *aux, int v)
{
  int zero = 0;

  dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &mpris_playstatus);
  dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &zero);
  dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &zero);
  dbus_message_iter_append_basic(args, DBUS_TYPE_INT32, &zero);
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
VolumeSet(DBusConnection *c, DBusMessage *in,
	  DBusMessageIter *args, void *aux, int v)
{
  dbus_int32_t vol = 0;
  prop_t *p;

  if(!dbus_message_get_args(in, NULL, 
			    DBUS_TYPE_INT32, &vol,
			    DBUS_TYPE_INVALID))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  p = prop_get_by_name(PNVEC("global", "audio", "mastervolume"), 1, NULL);
  prop_set_float(p, 0 - (float)vol);
  prop_ref_dec(p);

  return DBUS_HANDLER_RESULT_HANDLED;
}




/**
 * Root Methods
 */
struct method Root_methods[] = {
  { "Identity",           Identity,          NULL },
  { "MprisVersion",       MprisVersion,      NULL },
  { "Quit",               Quit,              NULL },
};


/**
 * Root Object
 */
static struct obj Root = {
  .introspection = ROOT_INTROSPECTION,
  .methods = Root_methods,
  .nmethods = sizeof(Root_methods) / sizeof(struct method),
};


/**
 * Player Methods
 */
struct method Player_methods[] = {
  { "Prev",               doAction,        NULL,   ACTION_PREV_TRACK },
  { "Next",               doAction,        NULL,   ACTION_NEXT_TRACK },
  { "Stop",               doAction,        NULL,   ACTION_STOP },
  { "Pause",              doAction,        NULL,   ACTION_PLAYPAUSE },
  { "Play",               doAction,        NULL,   ACTION_PLAY },
  { "VolumeSet",          VolumeSet,       NULL },
  { "GetStatus",          GetStatus,       NULL },
};


/**
 * Player Object
 */
static struct obj Player = {
  .introspection = PLAYER_INTROSPECTION,
  .methods = Player_methods,
  .nmethods = sizeof(Player_methods) / sizeof(struct method),
};


/**
 * TrackList Methods
 */
struct method TrackList_methods[] = {
  { "GetLength",           GetLength,          NULL },
};

/**
 * TrackList Object
 */
static struct obj TrackList = {
  .introspection = TRACKLIST_INTROSPECTION,
  .methods = TrackList_methods,
  .nmethods = sizeof(TrackList_methods) / sizeof(struct method),
};


/**
 *
 */
static DBusHandlerResult
handle_introspect(DBusConnection *c, DBusMessage *in, const char *reply)
{
  DBusMessage *out;
  DBusMessageIter args;

  if((out = dbus_message_new_method_return(in)) == NULL)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  dbus_message_iter_init_append(out, &args);

  if(!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &reply))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  
  if(!dbus_connection_send(c, out, NULL))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  dbus_connection_flush(c);
  dbus_message_unref(out);
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult
handle_method(DBusConnection *c, DBusMessage *in, struct method *m)
{
  DBusMessage *out;
  DBusMessageIter args;
  DBusHandlerResult r;

  if((out = dbus_message_new_method_return(in)) == NULL)
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  dbus_message_iter_init_append(out, &args);

  r = m->handler(c, in, &args, m->aux, m->value);

  if(r != DBUS_HANDLER_RESULT_HANDLED)
    return r;
  
  if(!dbus_connection_send(c, out, NULL))
    return DBUS_HANDLER_RESULT_NEED_MEMORY;

  dbus_connection_flush(c);
  dbus_message_unref(out);
  return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 *
 */
static DBusHandlerResult 
obj_handler(DBusConnection *c, DBusMessage *in, void *aux)
{
  struct obj *o = aux;
  int i;

  if(dbus_message_is_method_call(in, DBUS_INTERFACE_INTROSPECTABLE, 
				 "Introspect"))
    return handle_introspect(c, in, o->introspection);

  for(i = 0; i < o->nmethods; i++) {
    if(dbus_message_is_method_call(in, "org.freedesktop.MediaPlayer",
				   o->methods[i].name))
      return handle_method(c, in, &o->methods[i]);
  }

  TRACE(TRACE_ERROR, "mpris", "Unknown method %s.%s called",
	dbus_message_get_interface(in), dbus_message_get_member(in));
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/**
 *
 */
static const DBusObjectPathVTable vt = {
  .message_function = obj_handler
};


/**
 *
 */
static void
playstatus_changed(void *opaque, const char *str)
{
  if(str == NULL || !strcmp(str, "stop"))
    mpris_playstatus = 2;
  else if(!strcmp(str, "pause")) 
    mpris_playstatus = 1;
  else if(!strcmp(str, "play")) 
    mpris_playstatus = 0;
  else
    mpris_playstatus = 2;

  

}

/**
 *
 */
void
dbus_mpris_init(DBusConnection *c)
{
  DBusError err;

  dbus_error_init(&err);

  dbus_bus_request_name(c,  "org.mpris.hts-showtime", 0, &err);
  if(dbus_error_is_set(&err)) {
    TRACE(TRACE_ERROR, "D-Bus", "Unable to request service: %s",  err.message);
    return;
  }

  dbus_connection_register_object_path(c, "/", &vt, &Root);
  dbus_connection_register_object_path(c, "/Player", &vt, &Player);
  dbus_connection_register_object_path(c, "/TrackList", &vt, &TrackList);

  TRACE(TRACE_DEBUG, "D-Bus", "MPRIS Initialized");

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "media", "current", "playstatus"),
		 PROP_TAG_CALLBACK_STRING, playstatus_changed, c,
		 NULL);


}
