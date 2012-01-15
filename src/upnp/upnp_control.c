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
#include "misc/string.h"
#include "api/soap.h"

#include "upnp.h"



/**
 *
 */
static int
control_dispatch_method(upnp_local_service_t *uls, upnp_service_method_t *usm,
			http_connection_t *hc, htsmsg_t *inargs)
{
  htsmsg_field_t *f;
  htsmsg_t *in, *a;
  const char *s;
  htsbuf_queue_t xml;

  inargs = htsmsg_get_map(inargs, "tags");
  if(inargs != NULL) {
    in = htsmsg_create_map();
    // Convert args from XML style to more compact style
    HTSMSG_FOREACH(f, inargs) {
      if((a = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      if((s = htsmsg_get_str(a, "cdata")) != NULL)
	htsmsg_add_str(in, f->hmf_name, s);
    }
  } else {
    in = htsmsg_create_map();
  }

  htsmsg_t *out = usm->usm_fn(hc, in, http_get_my_host(hc),
			      http_get_my_port(hc));
  htsmsg_destroy(in);

  htsbuf_queue_init(&xml, 0);
  
  htsbuf_qprintf(&xml,
		 "<?xml version=\"1.0\"?>"
		 "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		 "<s:Body>");

  if(out == UPNP_CONTROL_INVALID_ARGS) {
    htsbuf_qprintf(&xml,
		   "<s:Fault>"
		   "<s:faultcode>500</s:faultcode></s:Fault>");
  } else {
    htsbuf_qprintf(&xml,
		   "<u:%sResponse "
		   "xmlns:u=\"urn:schemas-upnp-org:service:%s:%d\">",
		   usm->usm_name,
		   uls->uls_name,
		   uls->uls_version);
    
    if(out != NULL) {
      soap_encode_args(&xml, out);
      htsmsg_destroy(out);
    }

    htsbuf_qprintf(&xml,
		   "</u:%sResponse>",
		   usm->usm_name);
  }

  htsbuf_qprintf(&xml,
		 "</s:Body>"
		 "</s:Envelope>");

  http_set_response_hdr(hc, "EXT", "");

  return http_send_reply(hc, 0, "text/xml; charset=\"utf-8\"",
			 NULL, NULL, 0, &xml);
}


/**
 *
 */
static int
control_parse_soap(upnp_local_service_t *uls, http_connection_t *hc,
		   htsmsg_t *envelope)
{
  htsmsg_field_t *f;
  htsmsg_t *m, *methods;

  methods =
    htsmsg_get_map_multi(envelope, 
			 "tags",
			 "http://schemas.xmlsoap.org/soap/envelope/Envelope",
			 "tags",
			 "http://schemas.xmlsoap.org/soap/envelope/Body",
			 "tags",
			 NULL);

  if(methods == NULL)
    return http_error(hc, HTTP_STATUS_BAD_REQUEST,
		      "No methods found in envelope");

  HTSMSG_FOREACH(f, methods) {
    if((m = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    // This parsing is nasty

    const char *s = f->hmf_name;
    if(strncmp(s, "urn:schemas-upnp-org:service:",
		strlen("urn:schemas-upnp-org:service:")))
      continue;
    s += strlen("urn:schemas-upnp-org:service:");
    
    if(strncmp(s, uls->uls_name, strlen(uls->uls_name)))
      continue;
    s += strlen(uls->uls_name);
    
    if(*s == ':')
      s++;

    int ver = *s++ - '0';
    if(ver > uls->uls_version)
      continue;

    int i = 0;
    while(1) {
      if(uls->uls_methods[i].usm_name == NULL) {
	return http_error(hc, 500, 
			  "Method %s:%d::%s not found",
			  uls->uls_name, uls->uls_version, s);
      }
      if(!strcmp(s, uls->uls_methods[i].usm_name))
	return control_dispatch_method(uls, &uls->uls_methods[i], hc, m);
      i++;
    }
  }
  return http_error(hc, HTTP_STATUS_BAD_REQUEST,
		    "Unable to parse methods");
}


/**
 *
 */
int
upnp_control(http_connection_t *hc, const char *remain, void *opaque,
	     http_cmd_t method)
{
  upnp_local_service_t *uls = opaque;
  char *xml = http_get_post_data(hc, NULL, 1);
  htsmsg_t *inenv;
  char errbuf[100];
  int r;

  if(xml == NULL)
    return http_error(hc, HTTP_STATUS_BAD_REQUEST, "Missing POST data");

  inenv = htsmsg_xml_deserialize(xml, errbuf, sizeof(errbuf));
  if(inenv == NULL)
    return http_error(hc, HTTP_STATUS_BAD_REQUEST, errbuf);
  
  r = control_parse_soap(uls, hc, inenv);
  htsmsg_destroy(inenv);
  return r;
}
