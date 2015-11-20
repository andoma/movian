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
#include "config.h"
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <net/netctl.h>
#include <errno.h>

#include "main.h"
#include "net_i.h"


/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
#ifdef MSG_NOSIGNAL
  return netSend(tc->fd, data, len, MSG_NOSIGNAL) != len ? ECONNRESET : 0;
#else
  return netSend(tc->fd, data, len, 0           ) != len ? ECONNRESET : 0;
#endif
}


/**
 *
 */
static int
tcp_read(tcpcon_t *tc, void *buf, size_t len, int all,
	 net_read_cb_t *cb, void *opaque)
{
  int x;
  size_t off = 0;
  while(1) {
    size_t r = len - off;
    x = netRecv(tc->fd, buf + off, r, 0);
    if(x < 1)
      return -1;
    
    if(all) {

      off += x;
      if(off == len)
	return len;

      if(cb != NULL)
	cb(opaque, off);

    } else {
      return x;
    }
  }
}


/**
 *
 */
static int
getstreamsocket(int family, char *errbuf, size_t errbufsize)
{
  int fd, optval, r;

  fd = netSocket(family, SOCK_STREAM, IPPROTO_TCP);
  if(fd < 0) {
    snprintf(errbuf, errbufsize, "Unable to create socket: %s",
	     strerror(net_errno));
    return -1;
  }

  /**
   * Switch to nonblocking
   */
  optval = 1;
  r = netSetSockOpt(fd, SOL_SOCKET, SO_NBIO, &optval, sizeof(optval));
  if(r < 0) {
    snprintf(errbuf, errbufsize, "Unable to go nonblocking: %s",
	     strerror(net_errno));
    netClose(fd);
    return -1;
  }

  optval = 1;
  if(netSetSockOpt(fd, 6, 1, &optval, sizeof(optval)) < 0)
    TRACE(TRACE_INFO, "TCP", "Unable to turn on TCP_NODELAY");

  return fd;
}



/**
 *
 */
int
net_resolve(const char *hostname, net_addr_t *addr, const char **err)
{
  struct net_hostent *hp;
  int herr;

  herr = 0;
  hp = netGetHostByName(hostname);
  if(hp == NULL)
    herr = h_errno;

  if(herr != 0) {
    switch(herr) {
    case HOST_NOT_FOUND:
      *err = "Unknown host";
      return -1;

    case NO_ADDRESS:
      *err = "The requested name is valid but does not have an IP address";
      return -1;

    case NO_RECOVERY:
      *err = "A non-recoverable name server error occurred";
      return -1;

    case TRY_AGAIN:
      *err = "A temporary error occurred on an authoritative name server";
      return -1;

    default:
      *err = "Unknown error";
      return -1;
    }

  } else if(hp == NULL) {
    *err = "Resolver internal error";
    return -1;
  }

  memset(addr, 0, sizeof(net_addr_t));

  switch(hp->h_addrtype) {
  case AF_INET:
    addr->na_family = 4;
    lv2_void* netaddrlist = (lv2_void*)(u64)hp->h_addr_list;
    memcpy(&addr->na_addr[0], (char*)(u64)netaddrlist[0],
           sizeof(struct in_addr));
    return 0;

  default:
    *err = "Invalid protocol family";
    return -1;
  }
}



/**
 *
 */
tcpcon_t *
tcp_connect_arch(const net_addr_t *addr,
                 char *errbuf, size_t errbufsize,
                 int timeout, cancellable_t *c, int dbg)
{
  int fd, r, err, optval;
  struct sockaddr_in in;
  socklen_t errlen = sizeof(int);

  switch(addr->na_family) {
  case 4:
    if((fd = getstreamsocket(AF_INET, errbuf, errbufsize)) == -1)
      return NULL;

    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = addr->na_port;
    memcpy(&in.sin_addr, addr->na_addr, 4);
    r = netConnect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in));
    if(dbg) {
      TRACE(TRACE_DEBUG, "NET", "Connecting fd 0x%x = 0x%x errno=%d",
            fd, r, net_errno);
      hexdump("netConnect", (struct sockaddr *)&in, sizeof(struct sockaddr_in));
    }
    break;

  default:
    snprintf(errbuf, errbufsize, "Invalid protocol family");
    return NULL;
  }

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd;
  htsbuf_queue_init(&tc->spill, 0);
  tcp_set_cancellable(tc, c);

  const char *errtype = "";

  if(r < 0) {
    if(net_errno == NET_EINPROGRESS) {

      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT | POLLERR;
      pfd.revents = 0;

      r = netPoll(&pfd, 1, timeout);
      if(r == 0) {
	/* Timeout */
	snprintf(errbuf, errbufsize, "Connection attempt timed out");
        tcp_close(tc);
	return NULL;
      }
      
      if(r == -1) {
	snprintf(errbuf, errbufsize, "poll() error: %s", 
		 strerror(net_errno));
        tcp_close(tc);
	return NULL;
      }

      if(pfd.revents & POLLERR) {
	snprintf(errbuf, errbufsize, "Connection refused");
        tcp_close(tc);
	return NULL;
      }

      netGetSockOpt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen);
    } else {
      err = net_errno;
      errtype = ", direct";
    }
  } else {
    err = 0;
  }

  if(err != 0) {
    snprintf(errbuf, errbufsize, "%s%s", strerror(err), errtype);
    tcp_close(tc);
    return NULL;
  }
  
  optval = 0;
  r = netSetSockOpt(fd, SOL_SOCKET, SO_NBIO, &optval, sizeof(optval));
  if(r < 0) {
    snprintf(errbuf, errbufsize, "Unable to go blocking: %s",
	     strerror(net_errno));
    netClose(fd);
    return NULL;
  }

  tc->read = tcp_read;
  tc->write = tcp_write;

  return tc;
}



/**
 *
 */
void
tcp_huge_buffer(tcpcon_t *tc)
{
  int v = 128 * 1024;
  int r = netSetSockOpt(tc->fd, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v));
  if(r < 0)
    TRACE(TRACE_ERROR, "TCP", "Unable to increase RCVBUF");
}


/**
 *
 */
void
tcp_set_read_timeout(tcpcon_t *tc, int ms)
{
  struct timeval tv;
  tv.tv_sec  = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  if(netSetSockOpt(tc->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
    TRACE(TRACE_ERROR, "TCP", "Unable to set RCVTIMO");
}


void
tcp_close_arch(tcpcon_t *tc)
{
  netClose(tc->fd);
}


void
tcp_shutdown(tcpcon_t *tc)
{
  shutdown(tc->fd, SHUT_RDWR);
}


/**
 *
 */
tcpcon_t *
tcp_from_fd(int fd)
{
  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd & ~0x40000000;
  htsbuf_queue_init(&tc->spill, 0);
  tc->read = tcp_read;
  tc->write = tcp_write;
  return tc;
}




/**
 *
 */
netif_t *
net_get_interfaces(void)
{
  union net_ctl_info info;

  if(netCtlGetInfo(NET_CTL_INFO_IP_ADDRESS, &info))
    return NULL;

  netif_t *ni = calloc(2, sizeof(netif_t));

  snprintf(ni[0].ifname, sizeof(ni[0].ifname), "eth");
  uint32_t a = inet_addr(info.ip_address);
  memcpy(&ni[0].ipv4_addr, &a, 4);
  uint32_t m = inet_addr(info.netmask);
  memcpy(&ni[0].ipv4_mask, &m, 4);
  return ni;
}


/**
 *
 */
void
net_change_nonblocking(int fd, int on)
{
  int optval = on;
  if(setsockopt(fd, SOL_SOCKET, SO_NBIO, &optval, sizeof(optval)))
    TRACE(TRACE_INFO, "TCP", "Unable to set nonblock to %d", on);
}



/**
 *
 */
void
net_change_ndelay(int fd, int on)
{
  int optval = on;

  if(setsockopt(fd, 6, 1, &optval, sizeof(optval)) < 0)
    TRACE(TRACE_INFO, "TCP", "Unable to turn on TCP_NODELAY");
}


/**
 *
 */
int
net_resolve_numeric(const char *str, net_addr_t *addr)
{
  in_addr_t ia = inet_addr(str);
  if(ia == INADDR_NONE)
    return 1;

  memset(addr, 0, sizeof(net_addr_t));
  addr->na_family = 4;
  memcpy(&addr->na_addr, &ia, 4);
  return 0;
}
