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

#include <libavutil/sha.h>

#include "networking/http_server.h"
#include "networking/ssdp.h"
#include "htsmsg/htsmsg_xml.h"
#include "event.h"
#include "playqueue.h"
#include "fileaccess/fileaccess.h"
#include "misc/string.h"
#include "api/soap.h"

#include "upnp.h"
#include "upnp_scpd.h"
#include "settings.h"
#include "service.h"
#include "backend/backend.h"

hts_mutex_t upnp_lock;
hts_cond_t upnp_device_cond;

static char *upnp_uuid;
struct upnp_device_list upnp_devices;


/**
 *
 */
static upnp_device_t *
dev_find(const char *url)
{
  upnp_device_t *ud;

  LIST_FOREACH(ud, &upnp_devices, ud_link)
    if(!strcmp(url, ud->ud_url))
      break;
  return ud;
}


/**
 *
 */
static void
us_destroy(upnp_service_t *us)
{
  LIST_REMOVE(us, us_link);

  free(us->us_id);
  free(us->us_event_url);
  free(us->us_control_url);
  free(us->us_local_url);

  setting_destroy(us->us_setting_enabled);
  setting_destroy(us->us_setting_title);
  setting_destroy(us->us_setting_type);
  prop_destroy(us->us_settings);

  free(us->us_settings_path);
  htsmsg_destroy(us->us_settings_store);
  free(us);
}

/**
 *
 */
static void
dev_destroy(upnp_device_t *ud)
{
  upnp_service_t *us;
  while((us = LIST_FIRST(&ud->ud_services)) != NULL)
    us_destroy(us);

  LIST_REMOVE(ud, ud_link);
  free(ud->ud_friendlyName);
  free(ud->ud_manufacturer);
  free(ud->ud_modelDescription);
  free(ud->ud_modelNumber);
  free(ud->ud_url);
  free(ud->ud_icon);
  free(ud);
}


/**
 *
 */
static void
describe_service(htsbuf_queue_t *out, const char *type, int version)
{
  htsbuf_qprintf(out,
		 "<service>"
		 "<serviceType>urn:schemas-upnp-org:service:%s:%d</serviceType>"
		 "<serviceId>urn:upnp-org:serviceId:%s</serviceId>"
		 "<SCPDURL>/upnp/%s/scpd.xml</SCPDURL>"
		 "<controlURL>/upnp/%s/control</controlURL>"
		 "<eventSubURL>/upnp/%s/subscribe</eventSubURL>"
		 "</service>",
		 type, version, type, type, type, type);
}


/**
 *
 */
static int
send_dev_description(http_connection_t *hc, const char *remain, void *opaque,
		     http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  htsbuf_qprintf(&out,
		 "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		 "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
		 "<specVersion>"
		 "<major>1</major>"
		 "<minor>0</minor>"
		 "</specVersion>"
		 "<device>"
		 "<dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMR-1.50</dlna:X_DLNADOC>"
		 "<dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">M-DMR-1.50</dlna:X_DLNADOC>"
		 "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:2</deviceType>"
		 "<friendlyName>Showtime</friendlyName>"
		 "<manufacturer>Andreas Öman</manufacturer>"
		 "<modelDescription>HTS Showtime Mediaplayer</modelDescription>"
		 "<modelName>HTS Showtime Mediaplayer</modelName>"
		 "<modelNumber>%s</modelNumber>"
		 "<UDN>uuid:%s</UDN>"
		 "<UPC/>"
		 "<presentationURL/>"
		 "<serviceList>",
		 htsversion_full,
		 upnp_uuid);


  describe_service(&out, "ConnectionManager", 2);
  describe_service(&out, "RenderingControl", 2);
  describe_service(&out, "AVTransport", 2);

  htsbuf_qprintf(&out,
		 "</serviceList>"
		 "</device>"
		 "</root>");

  return http_send_reply(hc, 0, "text/xml", NULL, NULL, 0, &out);
}


/**
 *
 */
static int
send_avt_scpd(http_connection_t *hc, const char *remain, void *opaque,
	      http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  htsbuf_append(&out, avt_scpd, strlen(avt_scpd));

  return http_send_reply(hc, 0, "text/xml", NULL, NULL, 0, &out);
}


/**
 *
 */
static int
send_rc_scpd(http_connection_t *hc, const char *remain, void *opaque,
	     http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  htsbuf_append(&out, rc_scpd, strlen(rc_scpd));

  return http_send_reply(hc, 0, "text/xml", NULL, NULL, 0, &out);
}


/**
 *
 */
static int
send_cm_scpd(http_connection_t *hc, const char *remain, void *opaque,
	     http_cmd_t method)
{
  htsbuf_queue_t out;
  htsbuf_queue_init(&out, 0);

  htsbuf_append(&out, cm_scpd, strlen(cm_scpd));

  return http_send_reply(hc, 0, "text/xml", NULL, NULL, 0, &out);
}




/**
 *
 */
void
upnp_init(void)
{
  extern upnp_local_service_t upnp_AVTransport_2;
  extern upnp_local_service_t upnp_RenderingControl_2;
  extern upnp_local_service_t upnp_ConnectionManager_2;

  htsmsg_t *conf = htsmsg_store_load("upnp");

  const char *s = conf ? htsmsg_get_str(conf, "uuid") : NULL;
  if(s != NULL) {
    upnp_uuid = strdup(s);
  } else {
    
    struct AVSHA *shactx = alloca(av_sha_size);
    uint64_t v;
    uint8_t d[20];
    char uuid[40];

    if(conf == NULL)
      conf = htsmsg_create_map();

    av_sha_init(shactx, 160);
    v = showtime_get_ts();
    av_sha_update(shactx, (void *)&v, sizeof(uint64_t));

    v = arch_get_seed();
    av_sha_update(shactx, (void *)&v, sizeof(uint64_t));

    av_sha_final(shactx, d);

    snprintf(uuid, sizeof(uuid),
	     "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
	     "%02x%02x%02x%02x%02x%02x",
	     d[0x0], d[0x1], d[0x2], d[0x3],
	     d[0x4], d[0x5], d[0x6], d[0x7],
	     d[0x8], d[0x9], d[0xa], d[0xb],
	     d[0xc], d[0xd], d[0xe], d[0xf]);

    upnp_uuid = strdup(uuid);
    htsmsg_add_str(conf, "uuid", uuid);
    htsmsg_store_save(conf, "upnp");
  }

  htsmsg_destroy(conf);

  upnp_avtransport_init();

  http_path_add("/upnp/description.xml", NULL, send_dev_description);
  http_path_add("/upnp/AVTransport/scpd.xml", NULL, send_avt_scpd);
  http_path_add("/upnp/ConnectionManager/scpd.xml", NULL, send_cm_scpd);
  http_path_add("/upnp/RenderingControl/scpd.xml", NULL, send_rc_scpd);

  http_path_add("/upnp/ConnectionManager/control",
		&upnp_ConnectionManager_2, upnp_control);
  http_path_add("/upnp/RenderingControl/control",
		&upnp_RenderingControl_2, upnp_control);
  http_path_add("/upnp/AVTransport/control",
		&upnp_AVTransport_2, upnp_control);

  http_path_add("/upnp/ConnectionManager/subscribe",
		&upnp_ConnectionManager_2, upnp_subscribe);
  http_path_add("/upnp/RenderingControl/subscribe",
		&upnp_RenderingControl_2, upnp_subscribe);
  http_path_add("/upnp/AVTransport/subscribe",
		&upnp_AVTransport_2, upnp_subscribe);


  ssdp_init(upnp_uuid);

  upnp_event_init();
}


/**
 *
 */
static struct strtab svctype_tab[] = {
  { "ContentDirectory:1",    UPNP_SERVICE_CONTENT_DIRECTORY_1},
  { "ContentDirectory:2",    UPNP_SERVICE_CONTENT_DIRECTORY_2},
};

/**
 *
 */
static upnp_service_type_t
upnp_svcstr_to_type(const char *str)
{
  if(strncmp(str, "urn:schemas-upnp-org:service:",
	     strlen("urn:schemas-upnp-org:service")))
    return -1;
  str += strlen("urn:schemas-upnp-org:service:");
  return str2val(str, svctype_tab);
}


/**
 *
 */
static upnp_service_t *
service_find(upnp_device_t *ud, const char *id)
{
  upnp_service_t *us;

  LIST_FOREACH(us, &ud->ud_services, us_link)
    if(!strcmp(us->us_id, id))
      break;
  return us;
}


/**
 *
 */
upnp_service_t *
upnp_service_guess(const char *url)
{
  upnp_device_t *ud;
  upnp_service_t *us;

  char proto1[16], proto2[16];
  char hostname1[16], hostname2[16];
  int port1, port2;

  url_split(proto1, sizeof(proto1), NULL, 0,
	    hostname1, sizeof(hostname1), &port1,
	    NULL, 0, url);

  if(port1 == -1 && !strcasecmp(proto1, "http"))
    port1 = 80;

  LIST_FOREACH(ud, &upnp_devices, ud_link) {

    LIST_FOREACH(us, &ud->ud_services, us_link) {

      url_split(proto2, sizeof(proto2), NULL, 0,
		hostname2, sizeof(hostname2), &port2,
		NULL, 0, us->us_control_url);

      if(port2 == -1 && !strcasecmp(proto2, "http"))
	port2 = 80;
    
      if(!strcmp(proto1, proto2) &&
	 !strcmp(hostname1, hostname2) && 
	 port1 == port2)
	return us;
    }
  }
  return NULL;
}

/**
 *
 */
static void
remove_bad_chars(char *s)
{
  while(*s) {
    if(*s == ':' || *s == '/')
      *s = '_';
    s++;
  }
}




/**
 *
 */
static void
upnp_settings_saver(void *opaque, htsmsg_t *msg)
{
  upnp_service_t *us = opaque;
  htsmsg_store_save(msg, us->us_settings_path);
}


/**
 *
 */
static void
add_content_directory(upnp_service_t *us, const char *hostname, int port)
{
  upnp_device_t *ud = us->us_device;

  char svcid[URL_MAX];
  char buf[256];
  snprintf(svcid, sizeof(svcid),
	   "upnp/upnp:%s:%s:0", ud->ud_uuid, us->us_id);
  us->us_local_url = strdup(svcid + 5);
  remove_bad_chars(svcid);

  us->us_settings_path = strdup(svcid);
  us->us_settings_store = htsmsg_store_load(svcid) ?: htsmsg_create_map();

  const char *title = ud->ud_friendlyName ?: "UPnP content directory";

  snprintf(buf, sizeof(buf), "%s (%s) on %s:%d",
	   title, ud->ud_modelNumber ?: "Unknown version", hostname, port);
  us->us_settings = settings_add_dir_cstr(settings_sd, title, NULL, 
					  us->us_icon_url, buf);

  us->us_setting_enabled = 
    settings_create_bool(us->us_settings, "enabled",
			 _p("Enabled on home screen"), 1,
			 us->us_settings_store, NULL, NULL,
			 SETTINGS_INITIAL_UPDATE, NULL,
			 upnp_settings_saver, us);

  us->us_setting_title =
    settings_create_string(us->us_settings, "title", _p("Name"), title,
			   us->us_settings_store, NULL, NULL,
			   SETTINGS_INITIAL_UPDATE, NULL,
			   upnp_settings_saver, us);
  
  const char *contents = "other";

  us->us_setting_type = 
    settings_create_string(us->us_settings, "type", _p("Type"), contents,
			   us->us_settings_store, NULL, NULL,
			   SETTINGS_INITIAL_UPDATE, NULL,
			   upnp_settings_saver, us);

  us->us_service = service_create(NULL, us->us_local_url, NULL,
				  us->us_icon_url, 1, 0);

  prop_link(settings_get_value(us->us_setting_title), 
	    prop_create(us->us_service->s_root, "title"));
  prop_link(settings_get_value(us->us_setting_type), 
	    prop_create(us->us_service->s_root, "type"));
  prop_link(settings_get_value(us->us_setting_enabled), 
	    prop_create(us->us_service->s_root, "enabled"));


}


/**
 *
 */
static void
introspect_service(upnp_device_t *ud, htsmsg_t *svc)
{
  const char *typestr, *id, *e_url, *c_url;
  upnp_service_t *us;
  upnp_service_type_t type;
  char proto[16];
  char hostname[128];
  char path[256];
  int port;

  id = htsmsg_get_str_multi(svc, "serviceId", "cdata", NULL);
  typestr = htsmsg_get_str_multi(svc, "serviceType", "cdata", NULL);
  e_url = htsmsg_get_str_multi(svc, "eventSubURL", "cdata", NULL);
  c_url = htsmsg_get_str_multi(svc, "controlURL", "cdata", NULL);

  if(id == NULL || typestr == NULL || e_url == NULL || c_url == NULL ||
     (type = upnp_svcstr_to_type(typestr)) == -1)
    return;
  
  if((us = service_find(ud, id)) == NULL) {
    us = calloc(1, sizeof(upnp_service_t));
    us->us_device = ud;
    us->us_id = strdup(id);
    LIST_INSERT_HEAD(&ud->ud_services, us, us_link);
  }
  us->us_type = type;

  url_split(proto, sizeof(proto), NULL, 0, hostname, sizeof(hostname), &port,
	    path, sizeof(path), ud->ud_url);

  free(us->us_event_url);
  us->us_event_url = url_resolve_relative(proto, hostname, port, path, e_url);

  free(us->us_control_url);
  us->us_control_url = url_resolve_relative(proto, hostname, port, path, c_url);

  free(us->us_icon_url);
  us->us_icon_url =
    ud->ud_icon ? url_resolve_relative(proto, hostname, port, path,
				       ud->ud_icon) : NULL;

  switch(us->us_type) {
  case UPNP_SERVICE_CONTENT_DIRECTORY_1:
  case UPNP_SERVICE_CONTENT_DIRECTORY_2:
    add_content_directory(us, hostname, port);
    break;
  default:
    break;
  }
}


/**
 *
 */
static const char *
device_get_icon(htsmsg_t *dev)
{
  htsmsg_field_t *f;
  const char *best = NULL;
  int bestscore = 0;

  htsmsg_t *iconlist = htsmsg_get_map_multi(dev,
					    "tags", "iconList",
					    "tags", NULL);

  if(iconlist == NULL)
    return NULL;

  HTSMSG_FOREACH(f, iconlist) {
    htsmsg_t *icon;
    int width, height;
    const char *mimetype, *url;
    int score;

    if(strcmp(f->hmf_name, "icon") ||
       (icon = htsmsg_get_map_by_field(f)) == NULL ||
       (icon = htsmsg_get_map(icon, "tags")) == NULL)
	continue;

    mimetype = htsmsg_get_str_multi(icon, "mimetype", "cdata", NULL);
    url = htsmsg_get_str_multi(icon, "url", "cdata", NULL);

    if(mimetype == NULL || url == NULL)
      continue;

    if(!strcmp(mimetype, "image/png"))
      score = 2;
    else if(!strcmp(mimetype, "image/jpeg"))
      score = 1;
    else
      continue;

    width  = atoi(htsmsg_get_str_multi(icon, "width",  "cdata", NULL) ?: "0");
    height = atoi(htsmsg_get_str_multi(icon, "height", "cdata", NULL) ?: "0");
    score += width * height;

    if(score > bestscore) {
      best = url;
      bestscore = score;
    }
  }
  return best;
}



/**
 *
 */
static void
introspect_device(upnp_device_t *ud)
{
  char *xmldata;
  size_t xmlsize;
  char errbuf[200];
  htsmsg_t *m, *svclist, *dev;
  const char *uuid;

  if(http_request(ud->ud_url, NULL, &xmldata, &xmlsize, errbuf, sizeof(errbuf),
		  NULL, NULL, 0, NULL, NULL, NULL)) {
    TRACE(TRACE_INFO, "UPNP", "Unable to introspect %s -- %s",
	  ud->ud_url, errbuf);
    return;
  }

  if((m = htsmsg_xml_deserialize(xmldata, errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_INFO, "UPNP", "Unable to introspect %s XML -- %s",
	  ud->ud_url, errbuf);
    return;
  }

  dev = htsmsg_get_map_multi(m, "tags", "root", "tags", "device", NULL);
  if(dev == NULL) {
    htsmsg_destroy(m);
    return;
  }

  uuid = htsmsg_get_str_multi(dev, "tags", "UDN", "cdata", NULL);

  if(uuid == NULL) {
    htsmsg_destroy(m);
    return;
  }

  mystrset(&ud->ud_uuid, uuid);


  mystrset(&ud->ud_friendlyName, 
	   htsmsg_get_str_multi(dev, "tags", "friendlyName", "cdata", NULL));

  mystrset(&ud->ud_manufacturer, 
	   htsmsg_get_str_multi(dev, "tags", "manufacturer", "cdata", NULL));

  mystrset(&ud->ud_modelDescription, 
	   htsmsg_get_str_multi(dev, "tags", "modelDescription", "cdata", NULL));

  mystrset(&ud->ud_modelNumber, 
	   htsmsg_get_str_multi(dev, "tags", "modelNumber", "cdata", NULL));

  mystrset(&ud->ud_icon, device_get_icon(dev));

  svclist = htsmsg_get_map_multi(dev,
				 "tags", "serviceList",
				 "tags", NULL);
  if(svclist == NULL) {
    TRACE(TRACE_INFO, "UPNP", "Unable to introspect %s -- No services",
	  ud->ud_url);
  } else {

    htsmsg_field_t *f;

    HTSMSG_FOREACH(f, svclist) {
      htsmsg_t *svc;
      if(strcmp(f->hmf_name, "service") ||
	 (svc = htsmsg_get_map_by_field(f)) == NULL ||
	 (svc = htsmsg_get_map(svc, "tags")) == NULL)
	continue;
      introspect_service(ud, svc);
    }
  }

  htsmsg_destroy(m);
}
    

/**
 *
 */
void
upnp_add_device(const char *url, const char *type, int maxage)
{
  upnp_device_t *ud;
  hts_mutex_lock(&upnp_lock);

  if((ud = dev_find(url)) == NULL) {
    ud = calloc(1, sizeof(upnp_device_t));
    ud->ud_url = strdup(url);
    LIST_INSERT_HEAD(&upnp_devices, ud, ud_link);
  }

  if(!strcmp(type, "urn:schemas-upnp-org:service:ContentDirectory:1") ||
     !strcmp(type, "urn:schemas-upnp-org:service:ContentDirectory:2")) {

    if(ud->ud_interesting == 0) {
      ud->ud_interesting = 1;
      introspect_device(ud);
    }
  }
  hts_cond_broadcast(&upnp_device_cond);
    
  hts_mutex_unlock(&upnp_lock);
}


/**
 *
 */
void
upnp_del_device(const char *url)
{
  upnp_device_t *ud;
  hts_mutex_lock(&upnp_lock);
  if((ud = dev_find(url)) != NULL) {
    dev_destroy(ud);
  }
  hts_mutex_unlock(&upnp_lock);
}


/**
 *
 */
static int
be_upnp_canhandle(const char *url)
{
  if(!strncmp(url, "upnp:", strlen("upnp:")))
    return 1;
  return 0;
}


/**
 *
 */
static int
be_upnp_init(void)
{
  hts_mutex_init(&upnp_lock);
  hts_cond_init(&upnp_device_cond, &upnp_lock);
  return 0;
}


/**
 *
 */
static backend_t be_upnp = {
  .be_init = be_upnp_init,
  .be_canhandle = be_upnp_canhandle,
  .be_open = be_upnp_browse,
};

BE_REGISTER(upnp);
