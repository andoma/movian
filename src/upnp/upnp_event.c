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

#include "showtime.h"
#include "networking/http_server.h"
#include "fileaccess/fileaccess.h"
#include "htsmsg/htsmsg_xml.h"
#include "misc/string.h"
#include "api/soap.h"

#include "upnp.h"






/**
 *
 */
static void
upnp_event_send_all(upnp_local_service_t *uls)
{
  htsbuf_queue_t out;
  htsmsg_field_t *f;
  upnp_subscription_t *us;

  if(uls->uls_generate_props == NULL)
    return;

  LIST_FOREACH(us, &uls->uls_subscriptions, us_link) {
    char str[32];
    struct http_header_list hdrs;

    htsbuf_queue_init(&out, 0);
    htsbuf_qprintf(&out,
		   "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		   "<e:propertyset xmlns:e=\"urn:schemas-upnp-org:event-1-0\">");
    
    htsmsg_t *props = uls->uls_generate_props(uls, 
					      us->us_myhost, us->us_myport);
    
    HTSMSG_FOREACH(f, props) {
      htsbuf_qprintf(&out, "<e:property>");
      soap_encode_arg(&out, f);
      htsbuf_qprintf(&out, "</e:property>");
    }
    htsbuf_qprintf(&out, "</e:propertyset>");
    
    htsmsg_destroy(props);

    LIST_INIT(&hdrs);

    http_header_add(&hdrs, "NT", "upnp:event");
    http_header_add(&hdrs, "NTS", "upnp:propchange");
    snprintf(str, sizeof(str), "%d", us->us_sid);
    http_header_add(&hdrs, "SID", str);

    snprintf(str, sizeof(str), "%d", us->us_seq);
    http_header_add(&hdrs, "SEQ", str);
    us->us_seq++;

    http_request(us->us_callback, NULL, NULL, NULL, NULL, 0, &out,
		     "text/xml;charset=\"utf-8\"", 0, NULL, &hdrs, "NOTIFY");
    http_headers_free(&hdrs);
    htsbuf_queue_flush(&out);
  }
}


/**
 *
 */
static void
do_notify(callout_t *c, void *opaque)
{
  hts_mutex_lock(&upnp_lock);
  upnp_event_send_all(opaque);
  hts_mutex_unlock(&upnp_lock);
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
subscription_destroy(upnp_subscription_t *us, const char *reason)
{
  upnp_local_service_t *uls = us->us_service;
  TRACE(TRACE_DEBUG, "UPNP", 
	"Deleted subscription for %s:%d callback: %s SID: %d -- %s",
	uls->uls_name, uls->uls_version, us->us_callback, us->us_sid, reason);

  LIST_REMOVE(us, us_link);
  free(us->us_callback);
  free(us->us_myhost);
  free(us);
}


/**
 *
 */
static upnp_subscription_t *
subscription_find(upnp_local_service_t *uls, const char *str)
{
  upnp_subscription_t *us;
  int sid = atoi(str);
  hts_mutex_lock(&upnp_lock);
      
  LIST_FOREACH(us, &uls->uls_subscriptions, us_link)
    if(us->us_sid == sid)
      break;
  
  if(us == NULL)
    hts_mutex_unlock(&upnp_lock);
  return us;
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
  static int sid_tally;

  if(tstr != NULL && !strncasecmp(tstr, "Second-", strlen("Second-"))) {
    timeout = atoi(tstr + strlen("Second-"));
  } else {
    timeout = 1800;
  }

  snprintf(timeouttxt, sizeof(timeouttxt), "Second-%d", timeout);

  switch(method) {
  default:
    return HTTP_STATUS_METHOD_NOT_ALLOWED;

  case HTTP_CMD_SUBSCRIBE:
    if(sidstr == NULL) {
      char *c, *d;
      char sidtxt[50];

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

      sid_tally++;
      
      us = calloc(1, sizeof(upnp_subscription_t));
      us->us_service = uls;
      us->us_sid = sid_tally;
      us->us_callback = strdup(c);
      us->us_expire = time(NULL) + timeout;
      us->us_myhost = strdup(http_get_my_host(hc));
      us->us_myport = http_get_my_port(hc);

      hts_mutex_lock(&upnp_lock);
      LIST_INSERT_HEAD(&uls->uls_subscriptions, us, us_link);

      snprintf(sidtxt, sizeof(sidtxt), "%d", us->us_sid);
      http_set_response_hdr(hc, "SID", sidtxt);
      
      TRACE(TRACE_DEBUG, "UPNP", 
	    "Created subscription for %s:%d callback: %s SID: %d "
	    "(timeout: %d seconds)",
	    uls->uls_name, uls->uls_version, us->us_callback, us->us_sid,
	    timeout);

    } else {
      if(callback != NULL || type != NULL)
	return http_error(hc, HTTP_STATUS_BAD_REQUEST,
			  "Callback or type sent in subscription renewal");
      
      if((us = subscription_find(uls, sidstr)) == NULL)
	return http_error(hc, HTTP_STATUS_PRECONDITION_FAILED,
			  "Subscription not found");
    
      us->us_expire = time(NULL) + timeout;
    }

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
    if(us->us_expire + 60 < now)
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
