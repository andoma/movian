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

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>

#include "main.h"
#include "net.h"

#include "asyncio.h"


/**
 *
 */
netif_t *
net_get_interfaces(void)
{
  struct ifaddrs *ifa_list, *ifa;
  struct netif *ni, *n;
  int num = 0;

  if(getifaddrs(&ifa_list) != 0) {
    TRACE(TRACE_ERROR, "net", "getifaddrs failed: %s", strerror(errno));
    return NULL;
  }

  for(ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next)
    num++;

  n = ni = calloc(1, sizeof(struct netif) * (num + 1));

  for(ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
    if((ifa->ifa_flags & (IFF_UP | IFF_LOOPBACK | IFF_RUNNING)) !=
       (IFF_UP | IFF_RUNNING) ||
       ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
	 continue;

    memcpy(n->ipv4_addr, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, 4);
    if(n->ipv4_addr[0] == 0 &&
       n->ipv4_addr[1] == 0 &&
       n->ipv4_addr[2] == 0 &&
       n->ipv4_addr[3] == 0)
      continue;
    memcpy(n->ipv4_mask, &((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr, 4);

    snprintf(n->ifname, sizeof(n->ifname), "%s", ifa->ifa_name);
    n++;
  }

  freeifaddrs(ifa_list);

  if(ni->ifname[0] == 0) {
    free(ni);
    return NULL;
  }

  return ni;
}




#ifdef linux

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>

static int netlink_fd;

static int
routesocket_input(asyncio_fd_t *af, void *opaque, int event, int error)
{
  char buf[4096];

  if(read(netlink_fd, buf, sizeof(buf))) {}
  asyncio_trig_network_change();
  return 0;
}


static void
routesocket_init(void)
{
  struct sockaddr_nl sa;

  memset(&sa, 0, sizeof(sa));
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;

  netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
  if(netlink_fd == -1) {
    TRACE(TRACE_DEBUG, "Netlink", "Failed to open netlink socket -- %s",
          strerror(errno));
    return;
  }
  if(bind(netlink_fd, (struct sockaddr *) &sa, sizeof(sa)) == -1) {
    TRACE(TRACE_DEBUG, "Netlink", "Failed to bind netlink socket -- %s",
          strerror(errno));
    return;
  }

  asyncio_add_fd(netlink_fd, ASYNCIO_READ, routesocket_input, NULL, "netlink");
}

INITME(INIT_GROUP_ASYNCIO, routesocket_init, NULL, 0);

#endif
