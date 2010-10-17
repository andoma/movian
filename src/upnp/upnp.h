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

#ifndef UPNP_H__
#define UPNP_H__

#include "misc/queue.h"
#include "misc/callout.h"

LIST_HEAD(upnp_device_list, upnp_device);
LIST_HEAD(upnp_service_list, upnp_service);
LIST_HEAD(upnp_subscription_list, upnp_subscription);

/**
 *
 */
typedef enum {
  UPNP_SERVICE_UNKNOWN = -1,
  UPNP_SERVICE_CONTENT_DIRECTORY_1 = 0,
  UPNP_SERVICE_CONTENT_DIRECTORY_2,
} upnp_service_type_t;


/**
 * Represents a remote service
 */
typedef struct upnp_service {
  LIST_ENTRY(upnp_service) us_link;

  char *us_id;

  upnp_service_type_t us_type;

  char *us_event_url;
  char *us_control_url;
} upnp_service_t;


/**
 * Represents a remote device
 *
 * TODO: Expire these periodically
 */
typedef struct upnp_device {
  LIST_ENTRY(upnp_device) ud_link;
  char *ud_url;
  char *ud_uuid;
  
  struct upnp_service_list ud_services;

  char ud_interesting;

} upnp_device_t;


/**
 *
 */
typedef struct upnp_service_method {
  const char *usm_name;
  htsmsg_t *(*usm_fn)(htsmsg_t *in);

} upnp_service_method_t;


/**
 *
 */
typedef struct upnp_subscription {
  LIST_ENTRY(upnp_subscription) us_link;
  char *us_callback;
  int us_sid;
  int us_seq;
  time_t us_expire;
  struct upnp_local_service *us_service;
} upnp_subscription_t;


/**
 *
 */
typedef struct upnp_local_service {
  const char *uls_name;
  int uls_version;
  struct upnp_subscription_list uls_subscriptions;
  htsmsg_t *(*uls_generate_props)(struct upnp_local_service *svc);
  struct callout uls_notifytimer;
  upnp_service_method_t uls_methods[];
} upnp_local_service_t;



void upnp_init(void);

void upnp_add_device(const char *url, const char *type, int maxage);

void upnp_del_device(const char *url);

upnp_service_t *upnp_service_guess(const char *url);

int upnp_control(http_connection_t *hc, const char *remain, void *opaque,
		 http_cmd_t method);

void upnp_avtransport_init(void);

struct prop;

int upnp_browse_children(const char *uri, const char *id, struct prop *nodes,
			 const char *trackid, struct prop **trackp);


/**
 * Event / Subscription handling
 */
void upnp_schedule_notify(upnp_local_service_t *uls);

void upnp_event_init(void);

int upnp_subscribe(http_connection_t *hc, const char *remain, void *opaque,
		   http_cmd_t method);

extern hts_mutex_t upnp_lock;

#endif // UPNP_H__
