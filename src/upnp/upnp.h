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
#ifndef UPNP_H__
#define UPNP_H__

#include "main.h"
#include "misc/queue.h"
#include "misc/callout.h"

#define UPNP_TRACE(x, ...) do {                                 \
    if(gconf.enable_upnp_debug)                                 \
      TRACE(TRACE_DEBUG, "UPNP", x, ##__VA_ARGS__);		\
  } while(0)


LIST_HEAD(upnp_device_list, upnp_device);
LIST_HEAD(upnp_service_list, upnp_service);
LIST_HEAD(upnp_subscription_list, upnp_subscription);

struct http_connection;

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
  struct upnp_device *us_device;

  char *us_id;

  upnp_service_type_t us_type;

  char *us_event_url;
  char *us_control_url;
  char *us_local_url;
  char *us_icon_url;

  char *us_settings_path;
  struct htsmsg *us_settings_store;

  struct prop *us_settings;
  struct setting *us_setting_enabled;
  struct setting *us_setting_title;
  struct setting *us_setting_type;
  
  struct service *us_service;

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
  char *ud_friendlyName;
  char *ud_manufacturer;
  char *ud_modelDescription;
  char *ud_modelNumber;
  char *ud_icon;

  struct upnp_service_list ud_services;

  char ud_interesting;

} upnp_device_t;


/**
 *
 */
typedef struct upnp_service_method {
  const char *usm_name;
  struct htsmsg *(*usm_fn)(struct http_connection *hc, struct htsmsg *in,
			   const char *myhost, int myport);

} upnp_service_method_t;


/**
 *
 */
typedef struct upnp_subscription {
  LIST_ENTRY(upnp_subscription) us_link;
  char *us_callback;
  char *us_uuid;
  int us_seq;
  time_t us_expire;
  struct upnp_local_service *us_service;

  int us_myport;
  char *us_myhost;

} upnp_subscription_t;


/**
 *
 */
typedef struct upnp_local_service {
  const char *uls_name;
  int uls_version;
  struct upnp_subscription_list uls_subscriptions;
  struct htsmsg *(*uls_generate_props)(struct upnp_local_service *svc,
				       const char *myhost, int myport);
  struct callout uls_notifytimer;
  upnp_service_method_t uls_methods[];
} upnp_local_service_t;



void upnp_init(int http_server_port);

void upnp_add_device(const char *url, const char *type, int maxage);

void upnp_del_device(const char *url);

upnp_service_t *upnp_service_guess(const char *url);

struct http_connection;

int upnp_control(struct http_connection *hc, const char *remain, void *opaque,
		 http_cmd_t method);

#define UPNP_CONTROL_INVALID_ARGS ((void *)-1)

void upnp_avtransport_init(void);

struct prop;

int upnp_browse_children(const char *uri, const char *id, struct prop *nodes,
			 const char *trackid, struct prop **trackp);


/**
 * Event / Subscription handling
 */
void upnp_schedule_notify(upnp_local_service_t *uls);

void upnp_event_init(void);

int upnp_subscribe(struct http_connection *hc, const char *remain, void *opaque,
		   http_cmd_t method);

void upnp_event_encode_str(htsbuf_queue_t *xml, const char *attrib,
			   const char *str);

void upnp_event_encode_int(htsbuf_queue_t *xml, const char *attrib, int v);


/**
 * Global UPnP stuff
 */
extern hts_mutex_t upnp_lock;
extern hts_cond_t upnp_device_cond;
extern struct upnp_device_list upnp_devices;

int be_upnp_browse(struct prop *page, const char *url0, int sync);

void be_upnp_search(struct prop *source, const char *query);

#endif // UPNP_H__
