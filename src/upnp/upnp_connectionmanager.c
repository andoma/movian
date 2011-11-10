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
cm_GetProtocolInfo(http_connection_t *hc, htsmsg_t *args,
		   const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();
  htsmsg_add_str(out, "Source", "");
  htsmsg_add_str(out, "Sink", "http-get:*:*:*");
  return out;
}


/**
 *
 */
static htsmsg_t *
cm_GetCurrentConnectionInfo(http_connection_t *hc, htsmsg_t *args,
			    const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();
  htsmsg_add_u32(out, "RcsID", 0);
  htsmsg_add_u32(out, "AVTransportID", 0);
  htsmsg_add_str(out, "ProtocolInfo", "");
  htsmsg_add_str(out, "PeerConnectionManager", "");
  htsmsg_add_u32(out, "PeerConnectionID", -1);
  htsmsg_add_str(out, "Direction", "Input");
  htsmsg_add_str(out, "Status", "OK");
  return out;
}



/**
 *
 */
static htsmsg_t *
cm_GetCurrentConnectionIDs(http_connection_t *hc, htsmsg_t *args,
			   const char *myhost, int myport)
{
  const char *uri = htsmsg_get_str(args, "ConnectionID");
  if(uri == NULL || strcmp(uri, "0"))
    return UPNP_CONTROL_INVALID_ARGS;
  
  htsmsg_t *out = htsmsg_create_map();
  htsmsg_add_str(out, "ConnectionIDs", "0");
  return out;
}


/**
 *
 */
static htsmsg_t *
cm_generate_props(upnp_local_service_t *uls, const char *myhost, int myport)
{
  htsmsg_t *r = htsmsg_create_map();
  htsmsg_add_str(r, "SourceProtocolInfo", "");
  htsmsg_add_str(r, "SinkProtocolInfo", "http-get:*:*:*");
  htsmsg_add_str(r, "CurrentConnectionIDs", "0");

  return r;
}

/**
 *
 */
upnp_local_service_t upnp_ConnectionManager_2 = {
  .uls_name = "ConnectionManager",
  .uls_version = 2,
  .uls_generate_props = cm_generate_props,
  .uls_methods = {
    { "GetProtocolInfo", cm_GetProtocolInfo },
    { "GetCurrentConnectionInfo", cm_GetCurrentConnectionInfo },
    { "GetCurrentConnectionIDs", cm_GetCurrentConnectionIDs },
  }
};
