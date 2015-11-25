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

#include "main.h"
#include "networking/http_server.h"
#include "fileaccess/http_client.h"
#include "htsmsg/htsmsg_xml.h"
#include "misc/str.h"
#include "api/soap.h"
#include "arch/arch.h"

#include "upnp.h"

LIST_HEAD(send_event_list, send_event);

/**
 *
 */
typedef struct send_event {
  LIST_ENTRY(send_event) link;
  struct http_header_list hdrs;
  char *url;
  htsbuf_queue_t out;
} send_event_t;



static send_event_t *
upnp_event_generate_one(upnp_local_service_t *uls,
			upnp_subscription_t *us)
{
  send_event_t *set;
  char str[32];
    
  set = malloc(sizeof(send_event_t));

  htsbuf_queue_init(&set->out, 0);
  htsbuf_qprintf(&set->out,
		 "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		 "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">");
    
  if(uls->uls_generate_props != NULL) {
    htsmsg_t *p = uls->uls_generate_props(uls, us->us_myhost, us->us_myport);
    htsmsg_field_t *f;
    
    HTSMSG_FOREACH(f, p) {
      htsbuf_qprintf(&set->out, "<e:property>");
      soap_encode_arg(&set->out, f);
      htsbuf_qprintf(&set->out, "</e:property>");
    }
    htsmsg_release(p);
  }
  htsbuf_qprintf(&set->out, "</e:propertyset>");
    

  LIST_INIT(&set->hdrs);

  http_header_add(&set->hdrs, "NT", "upnp:event", 0);
  http_header_add(&set->hdrs, "NTS", "upnp:propchange", 0);
  http_header_add(&set->hdrs, "SID", us->us_uuid, 0);

  snprintf(str, sizeof(str), "%d", us->us_seq);
  http_header_add(&set->hdrs, "SEQ", str, 0);
  us->us_seq++;
  set->url = strdup(us->us_callback);
  return set;
}


/**
 *
 */
static void
upnp_event_send_and_free(send_event_t *set)
{
  http_req(set->url,
           HTTP_POSTDATA(&set->out, "text/xml; charset=\"utf-8\""),
           HTTP_REQUEST_HEADERS(&set->hdrs),
           HTTP_METHOD("NOTIFY"),
           HTTP_READ_TIMEOUT(200),
           HTTP_CONNECT_TIMEOUT(700),
           NULL);
  http_headers_free(&set->hdrs);
  free(set->url);
  free(set);
}


/**
 *
 */
static void
upnp_event_send_all(upnp_local_service_t *uls)
{
  upnp_subscription_t *us;
  send_event_t *set;
  struct send_event_list list;

  LIST_INIT(&list);

  hts_mutex_lock(&upnp_lock);

  LIST_FOREACH(us, &uls->uls_subscriptions, us_link) {
    set = upnp_event_generate_one(uls, us);
    LIST_INSERT_HEAD(&list, set, link);
  }
  hts_mutex_unlock(&upnp_lock);
  
  while((set = LIST_FIRST(&list)) != NULL) {
    LIST_REMOVE(set, link);
    upnp_event_send_and_free(set);
  }
}


/**
 *
 */
static void
do_notify(callout_t *c, void *opaque)
{
  upnp_event_send_all(opaque);
}


/**
 *
 */
void
upnp_schedule_notify(upnp_local_service_t *uls)
{
  callout_arm_hires(&uls->uls_notifytimer, do_notify, uls, 10000);
}



/**
 *
 */
static void
do_notify_one(callout_t *c, void *opaque)
{
  upnp_event_send_and_free(opaque);
  free(c);
}





/**
 *
 */
static void
subscription_destroy(upnp_subscription_t *us, const char *reason)
{
  upnp_local_service_t *uls = us->us_service;
  UPNP_TRACE("Deleted subscription for %s:%d callback: %s SID: %s -- %s",
             uls->uls_name, uls->uls_version, us->us_callback, us->us_uuid,
             reason);

  LIST_REMOVE(us, us_link);
  free(us->us_callback);
  free(us->us_myhost);
  free(us->us_uuid);
  free(us);
}


/**
 *
 */
static upnp_subscription_t *
subscription_find(upnp_local_service_t *uls, const char *str)
{
  upnp_subscription_t *us;

  if(str == NULL)
    return NULL;

  hts_mutex_lock(&upnp_lock);
      
  LIST_FOREACH(us, &uls->uls_subscriptions, us_link)
    if(!strcmp(us->us_uuid, str))
      break;
  
  if(us == NULL)
    hts_mutex_unlock(&upnp_lock);
  return us;
}


/**
 *
 */
static char *
sub_gen_uuid(void)
{
  char uuid[64];
  uint8_t d[20];
  arch_get_random_bytes(d, sizeof(d));

  snprintf(uuid, sizeof(uuid),
	   "uuid:%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
	   "%02x%02x%02x%02x%02x%02x",
	   d[0x0], d[0x1], d[0x2], d[0x3],
	   d[0x4], d[0x5], d[0x6], d[0x7],
	   d[0x8], d[0x9], d[0xa], d[0xb],
	   d[0xc], d[0xd], d[0xe], d[0xf]);
  
  return strdup(uuid);
}


/**
 *
 */
int
upnp_subscribe(http_connection_t *hc, const char *remain, void *opaque,
	       http_cmd_t method)
{
  upnp_local_service_t *uls = opaque;
  const char *callback = http_arg_get_hdr(hc, "callback");
  const char *type     = http_arg_get_hdr(hc, "nt");
  const char *sidstr   = http_arg_get_hdr(hc, "sid");
  const char *tstr     = http_arg_get_hdr(hc, "timeout");
  upnp_subscription_t *us;
  int timeout;
  char timeouttxt[50];

  switch(method) {
  default:
    return HTTP_STATUS_METHOD_NOT_ALLOWED;

  case HTTP_CMD_SUBSCRIBE:
    if(tstr == NULL) {
      timeout = 1800;
    } else {
      if(!strncasecmp(tstr, "Second-", strlen("Second-"))) {
	tstr += strlen("Second-");
	if(!strcasecmp(tstr, "infinite")) {
	  timeout = -1;
	} else if(tstr[0] < '0' || tstr[0] > '9' ||
		  (timeout = atoi(tstr)) == 0) {
	  return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			    "Invalid timeout");
	}
      } else {
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Invalid timeout");
      }
    }
  
    if(timeout == -1) {
      snprintf(timeouttxt, sizeof(timeouttxt), "Second-infinite");
    } else {
      snprintf(timeouttxt, sizeof(timeouttxt), "Second-%d", timeout);
    }


    if(sidstr == NULL) {
      char *c, *d;

      // New subscription
      
      if(type == NULL || strcmp(type, "upnp:event"))
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Invalid or missing type");
      
      if(callback == NULL)
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "No callback specified");

      c = mystrdupa(callback);


      if((c = strchr(c, '<')) == NULL)
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Misformated callback");
      c++;
      if((d = strrchr(c, '>')) == NULL)
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Misformated callback");
      *d = 0;

     
      us = calloc(1, sizeof(upnp_subscription_t));
      us->us_service = uls;
      us->us_callback = strdup(c);

      us->us_myhost = strdup(http_get_my_host(hc));
      us->us_myport = http_get_my_port(hc);

      hts_mutex_lock(&upnp_lock);
      LIST_INSERT_HEAD(&uls->uls_subscriptions, us, us_link);

      us->us_uuid = sub_gen_uuid();
      http_set_response_hdr(hc, "SID", us->us_uuid);
      
      UPNP_TRACE(
                 "Created subscription for %s:%d callback: %s SID: %s "
                 "(timeout: %d seconds)",
                 uls->uls_name, uls->uls_version, us->us_callback, us->us_uuid,
                 timeout);

      callout_arm_hires(NULL, do_notify_one, upnp_event_generate_one(uls, us),
			0);

    } else {
      if(callback != NULL || type != NULL)
	return http_error(hc, HTTP_STATUS_BAD_REQUEST,
			  "Callback or type sent in subscription renewal");
      
      if((us = subscription_find(uls, sidstr)) == NULL)
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Subscription not found");
    
    }
    us->us_expire = timeout > 0 ? time(NULL) + timeout : -1;

    http_set_response_hdr(hc, "TIMEOUT", timeouttxt);
    break;

  case HTTP_CMD_UNSUBSCRIBE:
    if(callback != NULL || type != NULL)
      return http_error(hc, HTTP_STATUS_BAD_REQUEST,
			"Callback or type sent in unsubscribe request");
    if((us = subscription_find(uls, sidstr)) == NULL)
      return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			"Subscription not found");
    subscription_destroy(us, "Requested by subscriber");
    break;
  }
  hts_mutex_unlock(&upnp_lock);

  upnp_schedule_notify(uls);

  return http_send_reply(hc, 0, NULL, NULL, NULL, 0, NULL);
}


/**
 *
 */
static void
purge_subscriptions(upnp_local_service_t *uls, time_t now)
{
  upnp_subscription_t *us, *next;
  for(us = LIST_FIRST(&uls->uls_subscriptions); us != NULL; us = next) {
    next = LIST_NEXT(us, us_link);
    if(us->us_expire != -1 && us->us_expire + 60 < now)
      subscription_destroy(us, "Timed out");
  }
}


static callout_t upnp_flush_timer;
/**
 *
 */
static void
upnp_flush(callout_t *c, void *opaque)
{
  extern upnp_local_service_t upnp_AVTransport_2;
  extern upnp_local_service_t upnp_RenderingControl_2;
  extern upnp_local_service_t upnp_ConnectionManager_2;

  time_t now;
  time(&now);

  callout_arm(&upnp_flush_timer, upnp_flush, NULL, 60);

  hts_mutex_lock(&upnp_lock);
  purge_subscriptions(&upnp_ConnectionManager_2, now);
  purge_subscriptions(&upnp_RenderingControl_2, now);
  purge_subscriptions(&upnp_AVTransport_2, now);
  hts_mutex_unlock(&upnp_lock);
}


/**
 *
 */
void
upnp_event_init(void)
{
  callout_arm(&upnp_flush_timer, upnp_flush, NULL, 60);
}



/**
 *
 */
void
upnp_event_encode_str(htsbuf_queue_t *xml, const char *attrib, const char *str)
{
  str = str ?: "NOT_IMPLEMENTED";

  htsbuf_qprintf(xml, "<%s val=\"", attrib);
  htsbuf_append_and_escape_xml(xml, str);
  htsbuf_qprintf(xml, "\"/>");
}


/**
 *
 */
void
upnp_event_encode_int(htsbuf_queue_t *xml, const char *attrib, int v)
{
  htsbuf_qprintf(xml, "<%s val=\"%d\"/>", attrib, v);
}
