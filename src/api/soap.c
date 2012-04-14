/*
 *  Showtime SOAP
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

#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_xml.h"

#include "soap.h"

/**
 *
 */
void
soap_encode_arg(htsbuf_queue_t *xml, htsmsg_field_t *f)
{
  switch(f->hmf_type) {
  case HMF_S64:
    htsbuf_qprintf(xml, "<%s>%"PRId64"</%s>",
		   f->hmf_name, f->hmf_s64, f->hmf_name);
    break;
      
  case HMF_STR:
    if(f->hmf_str[0] == 0) {
      htsbuf_qprintf(xml, "<%s/>", f->hmf_name);
      break;
    }

    htsbuf_qprintf(xml, "<%s>", f->hmf_name);
    htsbuf_append_and_escape_xml(xml, f->hmf_str);
    htsbuf_qprintf(xml, "</%s>", f->hmf_name);
    break;
  default:
    break;
  }
}


/**
 *
 */
void
soap_encode_args(htsbuf_queue_t *xml, htsmsg_t *args)
{
  htsmsg_field_t *f;

  HTSMSG_FOREACH(f, args)
    soap_encode_arg(xml, f);
}


/**
 *
 */
int
soap_exec(const char *uri, const char *service, int version, const char *method,
	  htsmsg_t *in, htsmsg_t **outp, char *errbuf, size_t errlen)
{
  int r;
  htsmsg_t *out;
  htsbuf_queue_t post;
  char *xmldata;
  size_t xmlsize;
  struct http_header_list hdrs = {0};
  char tmp[100];

  htsbuf_queue_init(&post, 0);

  htsbuf_qprintf(&post,
		 "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		 "<s:Envelope s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\" xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
		 "<s:Body><ns0:%s xmlns:ns0=\"urn:schemas-upnp-org:service:%s:%d\">", method, service, version);

  soap_encode_args(&post, in);
  htsbuf_qprintf(&post, "</ns0:%s></s:Body></s:Envelope>", method);

  snprintf(tmp, sizeof(tmp),"\"urn:schemas-upnp-org:service:%s:%d#%s\"",
	   service, version, method);

  http_header_add(&hdrs, "SOAPACTION", tmp, 0);

  r = http_request(uri, NULL, &xmldata, &xmlsize, errbuf, errlen,
		   &post, "text/xml; charset=\"utf-8\"",
		   0, NULL, &hdrs, NULL);

  http_headers_free(&hdrs);

  htsbuf_queue_flush(&post);

  if(r)
    return -1;

  out = htsmsg_xml_deserialize(xmldata, errbuf, errlen);
  if(out == NULL)
    return -1;

  snprintf(tmp, sizeof(tmp), "urn:schemas-upnp-org:service:%s:%d%sResponse",
	   service, version, method);

  htsmsg_t *outargs = 
    htsmsg_get_map_multi(out,
			 "tags",
			 "http://schemas.xmlsoap.org/soap/envelope/Envelope",
			 "tags",
			 "http://schemas.xmlsoap.org/soap/envelope/Body",
			 "tags",
			 tmp,
			 "tags",
			 NULL);

  if(outargs != NULL) {
    htsmsg_field_t *f;
    htsmsg_t *out = htsmsg_create_map();
    // Convert args from XML style to more compact style
    HTSMSG_FOREACH(f, outargs) {
      htsmsg_t *a;
      const char *s;

      if((a = htsmsg_get_map_by_field(f)) == NULL)
	continue;
      if((s = htsmsg_get_str(a, "cdata")) != NULL)
	htsmsg_add_str(out, f->hmf_name, s);
    }
    *outp = out;
  } else {
    *outp = NULL;
  }
  return 0;
}
