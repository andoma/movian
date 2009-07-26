/*
 *  DBUS Interface
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
#include <dbus/dbus.h>

#include "showtime.h"
#include "ui/ui.h"
#include "dbus.h"


/**
 *
 */
static int
dbus_start(ui_t *ui, int argc, char *argv[], int primary)
{
  DBusConnection *c;
  DBusError err;

  dbus_error_init(&err);

  if((c = dbus_bus_get(DBUS_BUS_SESSION, &err)) == NULL) {
    TRACE(TRACE_ERROR, "D-Bus", "Unable to connect to D-Bus: %s", err.message);
    return 1;
  }

  dbus_mpris_init(c);

  dbus_connection_flush(c);

  while(1) 
    dbus_connection_read_write_dispatch(c, -1);

  return 0;
}


/**
 *
 */
ui_t dbus_ui = {
  .ui_title = "dbus",
  .ui_start = dbus_start,
};
