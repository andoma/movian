/*
 *  Networking under POSIX
 *  Copyright (C) 2007-2008 Andreas Öman
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

#include "config.h"

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <ifaddrs.h>

#include "showtime.h"
#include "net.h"


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

    n->ipv4 = ntohl(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
    if(n->ipv4 == 0)
      continue;

    snprintf(n->ifname, sizeof(n->ifname), "%s", ifa->ifa_name);
    n++;
  }
  
  freeifaddrs(ifa_list);

  if(ni->ipv4 == 0) {
    free(ni);
    return NULL;
  }

  return ni;
}

