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

#include "networking/http.h"
#include "htsmsg/htsmsg_xml.h"
#include "event.h"
#include "misc/string.h"
#include "api/lastfm.h"

#include "upnp.h"


/**
 *
 */
static htsmsg_t *
rc_SetMute(struct http_connection *hc, htsmsg_t *args,
	   const char *myhost, int myport)
{
  
  return NULL;
}



/**
 *
 */
static htsmsg_t *
rc_SetVolume(struct http_connection *hc, htsmsg_t *args,
	     const char *myhost, int myport)
{
  
  return NULL;
}


/**
 *
 */
static htsmsg_t *
rc_SelectPreset(struct http_connection *hc, htsmsg_t *args,
		const char *myhost, int myport)
{
  
  return NULL;
}



/**
 *
 */
static htsmsg_t *
rc_ListPresets(struct http_connection *hc, htsmsg_t *args,
	       const char *myhost, int myport)
{
  htsmsg_t *out = htsmsg_create_map();
  htsmsg_add_str(out, "CurrentPresetNameList ", "");
  return out;
}

#if 0

/**
 *
 */
static void
lc_encode_val_str(htsbuf_queue_t *xml, const char *attrib, const char *str)
{
  str = str ?: "NOT_IMPLEMENTED";

  htsbuf_qprintf(xml, "<%s val=\"", attrib);
  htsbuf_append_and_escape_xml(xml, str);
  htsbuf_qprintf(xml, "\"/>");
}
#endif

/**
 *
 */
static htsmsg_t *
rc_generate_props(upnp_local_service_t *uls, const char *myhost, int myport)
{
  char *event;
  htsbuf_queue_t xml;
    
  htsbuf_queue_init(&xml, 0);

  htsbuf_qprintf(&xml,
		 "<Event xmlns=\"urn:schemas-upnp-org:metadata-1-0/RCS/\">"
		 "<InstanceID val=\"0\">");


  htsbuf_qprintf(&xml, "</InstanceID></Event>");

  event = htsbuf_to_string(&xml);

  htsmsg_t *r = htsmsg_create_map();
  htsmsg_add_str(r, "LastChange", event);
  free(event);
  return r;
}


/**
 *
 */
upnp_local_service_t upnp_RenderingControl_2 = {
  .uls_name = "RenderingControl",
  .uls_version = 2,
  .uls_generate_props = rc_generate_props,
  .uls_methods = {
    { "SetMute", rc_SetMute },
    { "SetVolume", rc_SetVolume },
    { "SelectPreset", rc_SelectPreset },
    { "ListPresets", rc_ListPresets },
    { NULL, NULL}
  }
};

