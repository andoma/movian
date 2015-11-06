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
#include <string.h>
#include <stdio.h>

#include "htsmsg/htsmsg_xml.h"
#include "misc/dbl.h"
#include "fileaccess/http_client.h"

#include "xmlrpc.h"


/**
 *
 */
static int
xmlrpc_parse_array(htsmsg_t *dst, htsmsg_t *params, 
		   char *errbuf, size_t errlen);

static int
xmlrpc_parse_struct(htsmsg_t *dst, htsmsg_t *params, 
		    char *errbuf, size_t errlen);

/**
 *
 */
static int
xmlrpc_parse_value(htsmsg_t *dst, htsmsg_field_t *g, const char *name,
		   char *errbuf, size_t errlen)
{
  const char *cdata;
  htsmsg_t *c;
  htsmsg_t *sub;

  if(!strcmp(g->hmf_name, "struct") &&
     (c = htsmsg_get_map_by_field(g)) != NULL) {

    sub = htsmsg_create_map();
    if(xmlrpc_parse_struct(sub, c, errbuf, errlen)) {
      htsmsg_release(sub);
      return -1;
    }
    htsmsg_add_msg(dst, name, sub);
    return 0;

  } else if(!strcmp(g->hmf_name, "array") &&
	    (c = htsmsg_get_map_by_field(g)) != NULL) {

    sub = htsmsg_create_list();
    if(xmlrpc_parse_array(sub, c, errbuf, errlen)) {
      htsmsg_release(sub);
      return -1;
    }
    htsmsg_add_msg(dst, name, sub);
    return 0;
  }

  cdata = g->hmf_type == HMF_STR ? g->hmf_str : NULL;

  if(!strcmp(g->hmf_name, "string")) {
    if(cdata != NULL)
      htsmsg_add_str(dst, name, cdata);

  } else if(!strcmp(g->hmf_name, "boolean")) {
    if(cdata != NULL)
      htsmsg_add_u32(dst, name, atoi(cdata));

  } else if(!strcmp(g->hmf_name, "double")) {
    if(cdata != NULL)
      htsmsg_add_dbl(dst, name, my_str2double(cdata, NULL));

  } else if(!strcmp(g->hmf_name, "int")) {
    if(cdata != NULL)
      htsmsg_add_s64(dst, name, atoll(cdata));

  } else {
    snprintf(errbuf, errlen, "Unknown field type \"%s\" %s = %s",
             g->hmf_name, name, cdata);
    return -1;
  }
  return 0;
}


/**
 *
 */
static int
xmlrpc_parse_struct(htsmsg_t *dst, htsmsg_t *params,
		    char *errbuf, size_t errlen)
{
  htsmsg_field_t *f, *g;
  htsmsg_t *c, *c2;
  const char *name;

  HTSMSG_FOREACH(f, params) {
    if(strcmp(f->hmf_name, "member") ||
       (c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    if((name = htsmsg_get_str(c, "name")) == NULL)
      continue;

    if((c2 = htsmsg_get_map(c, "value")) == NULL)
      continue;

    g = TAILQ_FIRST(&c2->hm_fields);
    if(g == NULL)
      continue;

    if(xmlrpc_parse_value(dst, g, name, errbuf, errlen))
      return -1;

  }
  return 0;
}


/**
 *
 */
static int
xmlrpc_parse_array(htsmsg_t *dst, htsmsg_t *m, 
		   char *errbuf, size_t errlen)
{
  htsmsg_t *c;
  htsmsg_field_t *f, *g;

  if((m = htsmsg_get_map(m, "data")) == NULL) {
    snprintf(errbuf, errlen, "Missing data tags in array\n");
    return -1;
  }

  HTSMSG_FOREACH(f, m) {
    if(strcmp(f->hmf_name, "value") || (c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    g = TAILQ_FIRST(&c->hm_fields);
    if(g == NULL || g->hmf_type != HMF_MAP)
      continue;
    
    if(xmlrpc_parse_value(dst, g, NULL, errbuf, errlen)) {
      return -1;
    }
  }
  return 0;
}


/**
 *
 */
static htsmsg_t *
xmlrpc_convert_response(htsmsg_t *xml, char *errbuf, size_t errlen)
{
  htsmsg_t *dst, *params;
  htsmsg_field_t *f, *g;
  htsmsg_t *c;


  if((params = htsmsg_get_map_multi(xml, "methodResponse",  "params",
				    NULL)) == NULL) {
    snprintf(errbuf, errlen, "No params in reply found");
    return NULL;
  }


  dst = htsmsg_create_list();

  HTSMSG_FOREACH(f, params) {
    if(strcmp(f->hmf_name, "param") || (c = htsmsg_get_map_by_field(f)) == NULL)
      continue;

    if((c = htsmsg_get_map(c, "value")) == NULL)
      continue;

    g = TAILQ_FIRST(&c->hm_fields);
    if(g == NULL || g->hmf_type != HMF_MAP)
      continue;

    if(xmlrpc_parse_value(dst, g, NULL, errbuf, errlen)) {
      htsmsg_release(dst);
      return NULL;
    }
  }
  return dst;
}


/**
 *
 */
static void xmlrpc_write_map(htsbuf_queue_t *q, htsmsg_t *m);

static void xmlrpc_write_list(htsbuf_queue_t *q, htsmsg_t *m, 
			      const char *pre, const char *post);

/**
 *
 */
static void
xmlrpc_write_field(htsbuf_queue_t *q, htsmsg_field_t *f,
		   const char *pre, const char *post)
{
  switch(f->hmf_type) {
  case HMF_S64:
    htsbuf_qprintf(q, "%s<value><int>%"PRId64"</int></value>%s\n",
		   pre, f->hmf_s64, post);
    break;

  case HMF_STR:
    htsbuf_qprintf(q, "%s<value><string>%s</string></value>%s\n",
		   pre, f->hmf_str, post);
    break;

  case HMF_LIST:
    htsbuf_qprintf(q, "%s<value><array><data>", pre);
    xmlrpc_write_list(q, f->hmf_childs, "", "");
    htsbuf_qprintf(q, "</data></array></value>%s\n", post);
    break;

  case HMF_MAP:
    htsbuf_qprintf(q, "%s<value><struct>", pre);
    xmlrpc_write_map(q, f->hmf_childs);
    htsbuf_qprintf(q, "</struct></value>%s\n", post);
    break;
  }
}

/**
 *
 */
static void
xmlrpc_write_map(htsbuf_queue_t *q, htsmsg_t *m)
{
  htsmsg_field_t *f;

  HTSMSG_FOREACH(f, m) {
    htsbuf_qprintf(q, "<member><name>%s</name>", f->hmf_name);
    xmlrpc_write_field(q, f, "", "");
    htsbuf_qprintf(q, "</member>\n");
  }
}



/**
 *
 */
static void
xmlrpc_write_list(htsbuf_queue_t *q, htsmsg_t *m, 
		  const char *pre, const char *post)
{
  htsmsg_field_t *f;

  HTSMSG_FOREACH(f, m)
    xmlrpc_write_field(q, f, pre, post);
}


/**
 *
 */
htsmsg_t *
xmlrpc_request(const char *url, const char *method, htsmsg_t *params,
	       char *errbuf, size_t errlen)
{
  htsbuf_queue_t q;
  buf_t *result;
  htsmsg_t *xml, *r;
  htsbuf_queue_init(&q, -1);
  htsbuf_qprintf(&q, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		 "<methodCall>\n"
		 "<methodName>%s</methodName>\n"
		 "<params>\n", method);

  xmlrpc_write_list(&q, params, "<param>", "</param>");
  htsmsg_release(params);
  htsbuf_qprintf(&q, "</params></methodCall>\n");

  int n = http_req(url,
                   HTTP_RESULT_PTR(&result),
                   HTTP_ERRBUF(errbuf, errlen),
                   HTTP_POSTDATA(&q, "text/xml"),
                   NULL);

  if(n)
    return NULL;
  xml = htsmsg_xml_deserialize_buf(result, errbuf, errlen);
  if(xml == NULL)
    return NULL;

  r = xmlrpc_convert_response(xml, errbuf, errlen);
  htsmsg_release(xml);
  return r;
}



