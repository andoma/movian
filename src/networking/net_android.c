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

#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "main.h"
#include "net.h"



/**
 *
 */
netif_t *
net_get_interfaces(void)
{
  char buf[4096];
  struct ifconf ifc;
  struct ifreq *ifr;
  struct netif *ni, *n;

  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if(s < 0)
    return NULL;

  ifc.ifc_len = sizeof(buf);
  ifc.ifc_buf = buf;
  if(ioctl(s, SIOCGIFCONF, &ifc) < 0) {
    close(s);
    return NULL;
  }

  int num = ifc.ifc_len / sizeof(struct ifreq);
  n = ni = calloc(1, sizeof(struct netif) * (num + 1));

  ifr = ifc.ifc_req;
  for(int i = 0; i < num; i++, ifr++) {
    struct sockaddr_in ipv4addr = *((struct sockaddr_in *)&ifr->ifr_addr);
    if(ioctl(s, SIOCGIFFLAGS, ifr) < 0)
      continue;
    if((ifr->ifr_flags & (IFF_UP | IFF_LOOPBACK | IFF_RUNNING)) != 
       (IFF_UP | IFF_RUNNING))
      continue;
    if(ioctl(s, SIOCGIFNETMASK, ifr) < 0)
      continue;
    struct sockaddr_in ipv4mask = *((struct sockaddr_in *)&ifr->ifr_addr);

    memcpy(n->ipv4_mask, &ipv4mask.sin_addr, 4);
    memcpy(n->ipv4_addr, &ipv4addr.sin_addr, 4);
    snprintf(n->ifname, sizeof(n->ifname), "%s", ifr->ifr_name);
    n++;
  }
  close(s);
  return ni;
}

