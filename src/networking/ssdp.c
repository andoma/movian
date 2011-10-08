/*
 *  SSDP
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
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>


#include <netinet/in.h>

#include "showtime.h"
#include "ssdp.h"
#include "http.h"
#include "http_server.h"
#include "net.h"

#include "upnp/upnp.h"

#define SSDP_NOTIFY   1
#define SSDP_SEARCH   2
#define SSDP_RESPONSE 3


static struct sockaddr_in ssdp_selfaddr;
static int ssdp_fdm, ssdp_fdu, ssdp_run = 1;
static char *ssdp_uuid;

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
      http_header_add(list, l, s);
    }
    if(*e == '\n')
      e++;
    l = e;
  }
  return r;
}


/**
 *
 */
static void
ssdp_send_static(int fd, const char *str)
{
  struct sockaddr_in sin = {0};
  int r;
  sin.sin_family = AF_INET;
  sin.sin_port = htons(1900);
  sin.sin_addr.s_addr = htonl(0xeffffffa);

  r = sendto(fd, str, strlen(str), 0, 
	     (struct sockaddr *)&sin, sizeof(struct sockaddr_in));
  if(r == -1)
    TRACE(TRACE_INFO, "SSDP", "Unable to send: %s", strerror(errno));
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
ssdp_send(int fd, uint32_t myaddr, struct sockaddr_in *dst, 
	  const char *type, const char *usn, const char *nts,
	  const char *location)
{
  char buf[1000];
  struct sockaddr_in mcast;

  snprintf(buf, sizeof(buf),
	   "%s\r\n"
	   "USN: uuid:%s::%s\r\n"
	   "SERVER: Showtime,%s,UPnp/1.0,Showtime,%s\r\n"
	   "%s"
	   "LOCATION: http://%d.%d.%d.%d:%d%s\r\n"
	   "CACHE-CONTROL: max-age=90\r\n"
	   "%s: %s\r\n"
	   "%s%s%s"
	   "\r\n",
	   dst ? "HTTP/1.1 200 OK" : "NOTIFY * HTTP/1.1",
	   ssdp_uuid,
	   usn ?: type,
	   htsversion, htsversion,
	   dst ? "EXT:\r\n" : "",
	   (uint8_t)(myaddr >> 24),
	   (uint8_t)(myaddr >> 16),
	   (uint8_t)(myaddr >> 8),
	   (uint8_t)(myaddr),
	   http_server_port,
	   location,
	   dst ? "ST" : "NT", type,
	   nts ? "NTS: " : "", nts ?: "", nts ? "\r\n" :"");

  if(dst == NULL) {
    memset(&mcast, 0, sizeof(mcast));

    mcast.sin_family = AF_INET;
    mcast.sin_port = htons(1900);
    mcast.sin_addr.s_addr = htonl(0xeffffffa);
    dst = &mcast;
  }
  sendto(fd, buf, strlen(buf), 0, 
	 (struct sockaddr *)dst, sizeof(struct sockaddr_in));
}

	  

/**
 *
 */
static void
ssdp_send_all(int fd, uint32_t myaddr, struct sockaddr_in *dst, const char *nts)
{
  char devurn[64];

  ssdp_send(fd, myaddr, dst,
	    "upnp:rootdevice",
	    NULL, nts, "/upnp/description.xml");

  snprintf(devurn, sizeof(devurn), "urn:%s", ssdp_uuid);

  ssdp_send(fd, myaddr, dst,
	    devurn,
	    NULL, nts, "/upnp/description.xml");

  ssdp_send(fd, myaddr, dst,
	    "urn:schemas-upnp-org:device:MediaRenderer:2",
	    NULL, nts, "/upnp/description.xml");

  ssdp_send(fd, myaddr, dst,
	    "urn:schemas-upnp-org:service:ConnectionManager:2",
	    NULL, nts, "/upnp/description.xml");

  ssdp_send(fd, myaddr, dst,
	    "urn:schemas-upnp-org:service:AVTransport:2",
	    NULL, nts, "/upnp/description.xml");

  ssdp_send(fd, myaddr, dst,
	    "urn:schemas-upnp-org:service:RenderingControl:2",
	    NULL, nts, "/upnp/description.xml");
}





/**
 * mc is set if packet arrived on our multicast listening socket
 */
static void
ssdp_input(int fd, int mc)
{
  char buf[2000];
  int r, cmd, self;
  struct http_header_list args;
  uint32_t myaddr;
  const char *usn;
  struct sockaddr_in si;

#if defined(IP_RECVDSTADDR)

  struct msghdr msg;
  struct cmsghdr *cmsg;
  struct iovec iov;
  char ctrl[500];

  iov.iov_base = buf;
  iov.iov_len = sizeof(buf);

  msg.msg_name = (struct sockaddr *)&si;
  msg.msg_namelen = sizeof(struct sockaddr_in);

  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  msg.msg_control = ctrl;
  msg.msg_controllen = sizeof(ctrl);

  r = recvmsg(fd, &msg, 0);
  if(r < 1)
    return;

  buf[r] = 0;

  myaddr = 0;

  for(cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg,cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IP && cmsg->cmsg_type == IP_RECVDSTADDR) {
      struct in_addr *ia = (struct in_addr *)CMSG_DATA(cmsg);
      myaddr = ntohl(ia->s_addr);
      break;
    }
  }

#else
 
  socklen_t slen = sizeof(struct sockaddr_in);
  netif_t *ni;

  r = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&si, &slen);
  if(r < 1)
    return;
  buf[r] = 0;

  ni = net_get_interfaces();
  myaddr = ni ? ni[0].ipv4 : 0;
  free(ni);

#endif

  if(!myaddr)
    return;

  LIST_INIT(&args);

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
      ssdp_send_all(ssdp_fdu, myaddr, &si, NULL);
  }
  http_headers_free(&args);
}


/**
 *
 */
static const char *SEARCHREQ = 
  "M-SEARCH * HTTP/1.1\r\n"
  "HOST: 239.255.255.250:1900\r\n"
  "MAN: \"ssdp:discover\"\r\n"
  "MX: 1\r\n"
  "ST: ssdp:all\r\n\r\n";


/**
 *
 */
static void
ssdp_send_notify(const char *nts)
{
  int fd, i = 0;
  netif_t *ni;
  struct sockaddr_in sin;
  
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = 0;

  if((ni = net_get_interfaces()) == NULL)
    return;

  while(ni[i].ifname[0]) {

    if((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) != -1) {
      sin.sin_addr.s_addr = htonl(ni[i].ipv4);
      if(bind(fd, (struct sockaddr *)&sin, sizeof(sin)) != -1) {
	ssdp_send_all(fd, ni[i].ipv4, NULL, nts);
      }
      close(fd);
    }
    i++;
  }
  free(ni);
}


#ifndef IP_ADD_MEMBERSHIP
#define IP_ADD_MEMBERSHIP		12
struct ip_mreq {
	struct in_addr imr_multiaddr;
	struct in_addr imr_interface;
};
#endif

/**
 *
 */
static void *
ssdp_thread(void *aux)
{
  struct sockaddr_in si = {0};
  int fdm, fdu;
  int one = 1, r;
  int64_t next_send = 0;
  struct pollfd fds[2];
  socklen_t sl = sizeof(struct sockaddr_in);
  struct ip_mreq imr;

  fdm = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  setsockopt(fdm, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
#if defined(IP_RECVDSTADDR)
  setsockopt(fdm, IPPROTO_IP, IP_RECVDSTADDR, &one, sizeof(int));
#endif
  si.sin_family = AF_INET;
  si.sin_port = htons(1900);

  if(bind(fdm, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
    TRACE(TRACE_ERROR, "SSDP", "Unable to bind");
    return NULL;
  }

  memset(&imr, 0, sizeof(imr));
  imr.imr_multiaddr.s_addr = htonl(0xeffffffa); // 239.255.255.250
  if(setsockopt(fdm, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr, 
		sizeof(struct ip_mreq)) == -1) {
    TRACE(TRACE_ERROR, "SSDP", "Unable to join 239.255.255.250: %s",
	  strerror(errno));
    return NULL;
  }

  fdu = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

#if defined(IP_RECVDSTADDR)
  setsockopt(fdu, IPPROTO_IP, IP_RECVDSTADDR, &one, sizeof(int));
#endif

  si.sin_family = AF_INET;
  si.sin_port = 0;

  if(bind(fdu, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
    TRACE(TRACE_ERROR, "SSDP", "Unable to bind");
    return NULL;
  }

  if(getsockname(fdu, (struct sockaddr *)&ssdp_selfaddr, &sl) == -1) {
    TRACE(TRACE_ERROR, "SSDP", "Unable to figure local port");
    return NULL;
  }

  ssdp_fdm = fdm;
  ssdp_fdu = fdu;
  
  ssdp_send_static(fdu, SEARCHREQ);

  fds[0].fd = fdm;
  fds[0].events = POLLIN;
  fds[1].fd = fdu;
  fds[1].events = POLLIN;

  while(ssdp_run) {
    
    int64_t delta = next_send - showtime_get_ts();
    if(delta <= 0) {
      delta = 15000000LL;
      next_send = showtime_get_ts() + delta;
      ssdp_send_notify("ssdp:alive");
    }
    r = poll(fds, 2, (delta / 1000) + 1);
    if(r > 0 && fds[0].revents & POLLIN)
      ssdp_input(fdm, 1);
    if(r > 0 && fds[1].revents & POLLIN)
      ssdp_input(fdu, 0);
  }
  return NULL;
}


/**
 *
 */
static void 
ssdp_shutdown(void *opaque, int retcode)
{
  // Prevent SSDP from announcing any more, not waterproof but should work
  ssdp_run = 0;
  ssdp_send_notify("ssdp:byebye");
}


/**
 *
 */
void
ssdp_init(const char *uuid)
{
  shutdown_hook_add(ssdp_shutdown, NULL, 1);
  ssdp_uuid = strdup(uuid);
  hts_thread_create_detached("ssdp", ssdp_thread, NULL,
			     THREAD_PRIO_NORMAL);
}
