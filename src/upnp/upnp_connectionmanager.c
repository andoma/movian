/*
 *  Showtime UPNP
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

#include "networking/http_server.h"
#include "htsmsg/htsmsg_xml.h"
#include "event.h"
#include "misc/string.h"
#include "api/lastfm.h"

#include "upnp.h"


/**
 *
 */
static htsmsg_t *
cm_GetProtocolInfo(http_connection_t *hc, htsmsg_t *args)
{
  htsmsg_t *out = htsmsg_create_map();
  htsmsg_add_str(out, "Source", "");
  htsmsg_add_str(out, "Sink", "http-get:*:*:*");
  return out;
}


/**
 *
 */
upnp_local_service_t upnp_ConnectionManager_2 = {
  .uls_name = "ConnectionManager",
  .uls_version = 2,
  .uls_methods = {
    { "GetProtocolInfo", cm_GetProtocolInfo },
  }
};
