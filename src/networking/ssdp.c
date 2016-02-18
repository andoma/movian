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
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#include "main.h"
#include "ssdp.h"
#include "http.h"
#include "http_server.h"
#include "net.h"

#include "upnp/upnp.h"


// This code executes on the asyncio thread / dispatch loop

#define SSDP_NOTIFY   1
#define SSDP_SEARCH   2
#define SSDP_RESPONSE 3


static char *ssdp_uuid;
static int http_server_port;

/**
 *
 */
static int
ssdp_parse(char *buf, struct http_header_list *list)
{
  char *l = buf, *e, *s;
  int r = 0;

  while((e = strchr(l, '\r')) != NULL && e != l) {
    *e++ = 0;

    if(l == buf) {
      if(!strcmp(l, "HTTP/1.1 200 OK"))
	r = SSDP_RESPONSE;
      else if(!strcmp(l, "M-SEARCH * HTTP/1.1"))
	r = SSDP_SEARCH;
      else if(!strcmp(l, "NOTIFY * HTTP/1.1"))
	r = SSDP_NOTIFY;
      else
	return 0;
    } else {
      if((s = strchr(l, ':')) == NULL)
	return 0;
      *s++ = 0;
      while(*s == 32)
	s++;
      http_header_add(list, l, s, 0);
    }
    if(*e == '\n')
      e++;
    l = e;
  }
  return r;
}

static net_addr_t ssdp_mcast_addr = {4, 1900, {239, 255, 255, 250}};

/**
 *
 */
static void
ssdp_send_static(asyncio_fd_t *af, const char *str)
{
  asyncio_udp_send(af, str, strlen(str), &ssdp_mcast_addr);
}


/**
 *
 */
static int
ssdp_maxage(struct http_header_list *args)
{
  int maxage = 1800;
  const char *cc = http_header_get(args, "cache-control");
  if(cc != NULL && (cc = strstr(cc, "max-age")) != NULL &&
     (cc = strchr(cc , '=')) != NULL)
    maxage = atoi(cc+1);
  return maxage;
}





/**
 *
 */
static void
ssdp_recv_notify(struct http_header_list *args)
{
  const char *nts  = http_header_get(args, "nts");
  const char *url  = http_header_get(args, "location");
  const char *type = http_header_get(args, "nt");

  if(nts == NULL || url == NULL)
    return;

  if(!strcasecmp(nts, "ssdp:alive") && type != NULL)
    upnp_add_device(url, type, ssdp_maxage(args));

  if(!strcasecmp(nts, "ssdp:byebye"))
    upnp_del_device(url);
}


/**
 *
 */
static void
ssdp_response(struct http_header_list *args)
{
  const char *url  = http_header_get(args, "location");
  const char *type = http_header_get(args, "st");

  if(url != NULL && type != NULL)
    upnp_add_device(url, type, ssdp_maxage(args));
}


/**
 *
 */
static void
ssdp_send(asyncio_fd_t *af, const net_addr_t *myaddr, const net_addr_t *dst,
	  const char *nt, const char *nts,
	  const char *location, int incl_host, const char *usn_postfix)
{
  char buf[1000];
  char date[64];

  if(dst) {
    time_t now;
    time(&now);
    http_asctime(now, date, sizeof(date));
  } else {
    date[0] = 0;
  }

  snprintf(buf, sizeof(buf),
	   "%s\r\n"
	   "USN: uuid:%s%s\r\n"
	   "%s"
	   "SERVER: "APPNAMEUSER",%s,UPnP/1.0,"APPNAMEUSER",%s\r\n"
	   "%s%s%s"
	   "%s"
	   "LOCATION: http://%d.%d.%d.%d:%d%s\r\n"
	   "CACHE-CONTROL: max-age=90\r\n"
	   "%s: %s\r\n"
	   "%s%s%s"
	   "\r\n",
	   dst ? "HTTP/1.1 200 OK" : "NOTIFY * HTTP/1.1",
	   ssdp_uuid, usn_postfix ?: "",
	   incl_host ? "HOST: 239.255.255.250:1900\r\n" : "",
	   htsversion, htsversion,
	   *date ? "DATE: " : "", date, *date ? "\r\n" : "",
	   dst ? "EXT:\r\n" : "",
           myaddr->na_addr[0],
           myaddr->na_addr[1],
           myaddr->na_addr[2],
           myaddr->na_addr[3],
	   http_server_port,
	   location,

	   dst ? "ST" : "NT", nt,
	   nts ? "NTS: " : "", nts ?: "", nts ? "\r\n" :"");

  if(dst == NULL)
    dst = &ssdp_mcast_addr;

  asyncio_udp_send(af, buf, strlen(buf), dst);
}


/**
 *
 */
static void
ssdp_send_all(asyncio_fd_t *af, const net_addr_t *myaddr,
              const net_addr_t *dst, const char *nts)
{
  char nt[100];
  snprintf(nt, sizeof(nt), "uuid:%s", ssdp_uuid);

  // Root device discovery
  ssdp_send(af, myaddr, dst,
	    "upnp:rootdevice",
	    nts, "/upnp/description.xml", 1,
	    "::upnp:rootdevice");

  ssdp_send(af, myaddr, dst,
	    nt,
	    nts, "/upnp/description.xml", 1,
	    NULL);

  ssdp_send(af, myaddr, dst,
	    "urn:schemas-upnp-org:device:MediaRenderer:2",
	    nts, "/upnp/description.xml", 1,
	    "::urn:schemas-upnp-org:device:MediaRenderer:2");

  // Service discovery

  ssdp_send(af, myaddr, dst,
	    "urn:schemas-upnp-org:service:ConnectionManager:2",
	    nts, "/upnp/description.xml", 1,
	    "::urn:schemas-upnp-org:service:ConnectionManager:2");


  ssdp_send(af, myaddr, dst,
	    "urn:schemas-upnp-org:service:AVTransport:2",
	    nts, "/upnp/description.xml", 1,
	    "::urn:schemas-upnp-org:service:AVTransport:2");


  ssdp_send(af, myaddr, dst,
	    "urn:schemas-upnp-org:service:RenderingControl:2",
	    nts, "/upnp/description.xml", 1,
	    "::urn:schemas-upnp-org:service:RenderingControl:2");

}


LIST_HEAD(ssdp_interface_list, ssdp_interface);

static struct ssdp_interface_list ssdp_interfaces;

typedef struct ssdp_interface {

  LIST_ENTRY(ssdp_interface) si_link;

  asyncio_fd_t *si_fd_mc;
  asyncio_fd_t *si_fd_uc;

  char si_ifname[NET_IFNAME_SIZE];

  net_addr_t si_myaddr;

  int si_mark;

  asyncio_timer_t si_alive_timer;
  asyncio_timer_t si_search_timer;

} ssdp_interface_t;





/**
 * mc is set if packet arrived on our multicast listening socket
 */
static void
ssdp_input(ssdp_interface_t *si, int mc, const uint8_t *input, int size,
           const net_addr_t *remote_addr)
{
  int cmd, self;
  struct http_header_list args;
  const char *usn;


  LIST_INIT(&args);

  char *buf = malloc(size + 1);
  buf[size] = 0;
  memcpy(buf, input, size);
  cmd = ssdp_parse(buf, &args);
  usn = http_header_get(&args, "usn");

  self = usn != NULL && !strncmp(usn, "uuid:", 5) &&
    !strncmp(usn + 5, ssdp_uuid, strlen(ssdp_uuid));

  if(!self) {
    if(cmd == SSDP_NOTIFY && mc)
      ssdp_recv_notify(&args);
    if(cmd == SSDP_RESPONSE && !mc)
      ssdp_response(&args);
    if(cmd == SSDP_SEARCH && mc)
      ssdp_send_all(si->si_fd_uc, &si->si_myaddr, remote_addr, NULL);
  }
  http_headers_free(&args);
  free(buf);
}


/**
 *
 */
static const char *SEARCHREQ =
  "M-SEARCH * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "MAN: \"ssdp:discover\"\r\n"
  "MX: 1\r\n"
  "ST: urn:schemas-upnp-org:service:ContentDirectory:1\r\n\r\n";

/**
 *
 */
static void
ssdp_send_notify_on_interface(ssdp_interface_t *si, const char *nts)
{
  ssdp_send_all(si->si_fd_uc, &si->si_myaddr, NULL, nts);
}


static void
ssdp_multicast_input(void *opaque, const void *data, int size,
                     const net_addr_t *remote_addr)
{
  ssdp_input(opaque, 1, data, size, remote_addr);
}


static void
ssdp_unicast_input(void *opaque, const void *data, int size,
                   const net_addr_t *remote_addr)
{
  ssdp_input(opaque, 0, data, size, remote_addr);
}


/**
 *
 */
static void
ssdp_send_alive(void *opaque)
{
  ssdp_interface_t *si = opaque;
  ssdp_send_notify_on_interface(si, "ssdp:alive");
  asyncio_timer_arm_delta_sec(&si->si_alive_timer, 15);
}


/**
 *
 */
static void
ssdp_send_search(void *opaque)
{
  ssdp_interface_t *si = opaque;
  ssdp_send_static(si->si_fd_uc, SEARCHREQ);
}


/**
 *
 */
static void
ssdp_netif_update(const struct netif *ni)
{
  ssdp_interface_t *si, *next;
  LIST_FOREACH(si, &ssdp_interfaces, si_link)
    si->si_mark = 1;

  for(int i = 0; ni != NULL && ni->ifname[0]; i++, ni++) {
    LIST_FOREACH(si, &ssdp_interfaces, si_link) {
      if(!memcmp(si->si_myaddr.na_addr, ni->ipv4_addr, 4))
        break;
    }

    if(si == NULL) {

      si = calloc(1, sizeof(ssdp_interface_t));
      strcpy(si->si_ifname, ni->ifname);
      si->si_myaddr.na_family = 4;
      si->si_myaddr.na_port = 1900;
      memcpy(si->si_myaddr.na_addr, ni->ipv4_addr, 4);
      TRACE(TRACE_DEBUG, "SSDP", "Trying to start on %s", ni->ifname);
      char name[32];
      snprintf(name, sizeof(name), "SSDP/%s/multicast", ni->ifname);

      si->si_fd_mc = asyncio_udp_bind(name, &si->si_myaddr, ssdp_multicast_input,
                                      si, 0, 0);
      if(si->si_fd_mc == NULL) {
        TRACE(TRACE_ERROR, "SSDP", "Failed to bind multicast to %s on %s",
              net_addr_str(&si->si_myaddr), ni->ifname);
        free(si);
        continue;
      }

      if(asyncio_udp_add_membership(si->si_fd_mc, &ssdp_mcast_addr)) {
        TRACE(TRACE_ERROR, "SSDP", "Failed to join multicast group %s on %s",
              net_addr_str(&ssdp_mcast_addr), ni->ifname);
        asyncio_del_fd(si->si_fd_mc);
        free(si);
        continue;
      }

      snprintf(name, sizeof(name), "SSDP/%s/unicast", ni->ifname);
      si->si_myaddr.na_port = 0;
      si->si_fd_uc = asyncio_udp_bind(name, &si->si_myaddr, ssdp_unicast_input,
                                      si, 0, 0);
      if(si->si_fd_uc == NULL) {
        TRACE(TRACE_ERROR, "SSDP", "Failed to bind unicast to %s on %s",
              net_addr_str(&si->si_myaddr), ni->ifname);
        asyncio_del_fd(si->si_fd_mc);
        free(si);
        continue;
      }
      LIST_INSERT_HEAD(&ssdp_interfaces, si, si_link);

      ssdp_send_static(si->si_fd_uc, SEARCHREQ);
      ssdp_send_notify_on_interface(si, "ssdp:alive");
      asyncio_timer_init(&si->si_alive_timer,  ssdp_send_alive, si);
      asyncio_timer_init(&si->si_search_timer, ssdp_send_search, si);

      asyncio_timer_arm_delta_sec(&si->si_alive_timer, 1);
      asyncio_timer_arm_delta_sec(&si->si_search_timer, 1);

      TRACE(TRACE_DEBUG, "SSDP", "SSDP started on %s", si->si_ifname);

    } else {
      si->si_mark = 0;
    }
  }

  for(si = LIST_FIRST(&ssdp_interfaces); si != NULL; si = next) {
    next = LIST_NEXT(si, si_link);

    if(!si->si_mark)
      continue;

    TRACE(TRACE_DEBUG, "SSDP", "SSDP stopped on %s", si->si_ifname);

    LIST_REMOVE(si, si_link);
    asyncio_del_fd(si->si_fd_mc);
    asyncio_del_fd(si->si_fd_uc);
    asyncio_timer_disarm(&si->si_alive_timer);
    asyncio_timer_disarm(&si->si_search_timer);
    free(si);
  }
}


/**
 *
 */
static void
ssdp_do_shutdown(void *aux)
{
  ssdp_interface_t *si;
  LIST_FOREACH(si, &ssdp_interfaces, si_link) {
    TRACE(TRACE_DEBUG, "SSDP", "Sending byebye on %s", si->si_ifname);
    for(int i = 0; i < 3; i++) {
      ssdp_send_notify_on_interface(si, "ssdp:byebye");
    }
  }
  ssdp_netif_update(NULL); // Turn off all
}


/**
 *
 */
static void
ssdp_shutdown(void *opaque, int retcode)
{
  asyncio_run_task(ssdp_do_shutdown, NULL);
}


/**
 *
 */
void
ssdp_init(const char *uuid, int http_server_port0)
{
  http_server_port = http_server_port0;
  shutdown_hook_add(ssdp_shutdown, NULL, 1);
  ssdp_uuid = strdup(uuid);
  asyncio_register_for_network_changes(ssdp_netif_update);
}
