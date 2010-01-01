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

#include "net.h"
#include <network.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arch/threads.h>
#include <errno.h>

static hts_mutex_t resolve_mutex;

/**
 *
 */
void
net_setup(void)
{
  char localip[32];

  if(if_config(localip, NULL, NULL, 1) == 0) {
    printf("Network initialized: %s\n", localip);
  } else {
    printf("Network failed to initialize\n");
    exit(0);
  }
}





/**
 *
 */
int
tcp_connect(const char *hostname, int port, char *errbuf, size_t errbufsize,
	    int timeout)
{
  struct hostent *h;
  int fd, r, retry = 0;
  struct sockaddr_in in;

  memset(&in, 0, sizeof(in));
  in.sin_family = AF_INET;
  in.sin_port = htons(port);

  if(!inet_aton(hostname, &in.sin_addr)) {

    hts_mutex_lock(&resolve_mutex);

    do {
      while((h = net_gethostbyname((char *)hostname)) == NULL) {
	retry++;
	if(retry == 10) {
	  snprintf(errbuf, errbufsize, "Unable to resolve %s -- %d", hostname,
		   errno);
	  hts_mutex_unlock(&resolve_mutex);
	  return -1;
	}
    	usleep(250000);
      }

    } while(h->h_addr_list[0] == NULL);

    memcpy(&in.sin_addr, h->h_addr_list[0], sizeof(struct in_addr));

    hts_mutex_unlock(&resolve_mutex);
  }
  if((fd = net_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    snprintf(errbuf, errbufsize, "Can not create socket, error %d", fd);
    return -1;
  }
  if((r = net_connect(fd, (struct sockaddr *)&in, sizeof(in))) < 0) {
    snprintf(errbuf, errbufsize, "Unable to connect to %s, error %d",
	     hostname, r);
    net_close(fd);
    return -1;
  }
  return fd;
}

/**
 *
 */
int
tcp_write(int fd, const void *data, size_t len)
{
  return net_send(fd, data, len, 0) != len ? ECONNRESET : 0;
}



#define MAX_READ_SIZE 4096
/**
 *
 */
int
tcp_read(int fd, void *buf, size_t bufsize, int all)
{
  int tot = 0, r;
  int rlen;

  while(tot < bufsize) {

    rlen = bufsize - tot;
    if(rlen > MAX_READ_SIZE)
      rlen = MAX_READ_SIZE;

    r = net_recv(fd, buf + tot, rlen, 0);
    if(r < 1) { 
      printf("NET_RCV ERROR %d\n", r);
      return -1;
    }
    tot += r;
    if(!all)
      break;
  }
  return tot;
}


/**
 *
 */
void
tcp_close(int fd)
{
  net_close(fd);
}
