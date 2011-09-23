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
#include "showtime.h"
#include <ogc/ipc.h>


hts_mutex_t net_resolve_mutex;
hts_mutex_t net_setup_mutex;
hts_cond_t  net_setup_cond;

static int net_running;

/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
  return net_send(tc->fd, data, len, 0) != len ? ECONNRESET : 0;
}



/**
 *
 */
static int
tcp_read(tcpcon_t *tc, void *buf, size_t bufsize, int all)
{
  int tot = 0, r;
  int rlen;
  int maxsize = 32768;

  while(tot < bufsize) {

    rlen = bufsize - tot;
    if(rlen > maxsize)
      rlen = maxsize;

    if((r = net_recv(tc->fd, buf + tot, rlen, 0)) == IPC_ENOMEM) {
      if(maxsize > 2048)
	maxsize /= 2;
      continue;
    }

    if(r < 1)
      return -1;
    tot += r;
    if(!all)
      break;
  }
  return tot;
}


/**
 *
 */
static void *
net_setup_thread(void *aux)
{
  char localip[32];

  hts_mutex_lock(&net_setup_mutex);

  if(!net_running && !if_config(localip, NULL, NULL, 1))
    net_running = 1;

  hts_mutex_unlock(&net_setup_mutex);
  return NULL;
}


int net_try_setup(void);
/**
 *
 */
int
net_try_setup(void)
{
  int r;
  char localip[32];

  hts_mutex_lock(&net_setup_mutex);
  r = !net_running && !if_config(localip, NULL, NULL, 1);
  hts_mutex_unlock(&net_setup_mutex);
  return r;
}


/**
 *
 */
void
net_setup(void)
{

  hts_mutex_init(&net_resolve_mutex);
  hts_mutex_init(&net_setup_mutex);

  hts_thread_create_detached("netboot", net_setup_thread, NULL);
}





/**
 *
 */
tcpcon_t *
tcp_connect(const char *hostname, int port, char *errbuf, size_t errbufsize,
	    int timeout, int ssl)
{
  struct hostent *h;
  int fd, r, retry = 0;
  struct sockaddr_in in;

  if(ssl) {
    snprintf(errbuf, errbufsize, "No SSL support");
    return NULL;
  }

  if(net_try_setup()) {
    snprintf(errbuf, errbufsize, "Unable initialize networking");
    return NULL;
  }

  memset(&in, 0, sizeof(in));
  in.sin_family = AF_INET;
  in.sin_port = htons(port);

  if(!inet_aton(hostname, &in.sin_addr)) {

    hts_mutex_lock(&net_resolve_mutex);

    do {
      while((h = net_gethostbyname((char *)hostname)) == NULL) {
	retry++;
	if(retry == 10) {
	  snprintf(errbuf, errbufsize, "Unable to resolve %s -- %d", hostname,
		   errno);
	  hts_mutex_unlock(&net_resolve_mutex);
	  return NULL;
	}
    	usleep(250000);
      }

    } while(h->h_addr_list[0] == NULL);

    memcpy(&in.sin_addr, h->h_addr_list[0], sizeof(struct in_addr));

    hts_mutex_unlock(&net_resolve_mutex);
  }
  if((fd = net_socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    snprintf(errbuf, errbufsize, "Can not create socket, error %d", fd);
    return NULL;
  }
  if((r = net_connect(fd, (struct sockaddr *)&in, sizeof(in))) < 0) {
    snprintf(errbuf, errbufsize, "Unable to connect to %s, error %d",
	     hostname, r);
    net_close(fd);
    return NULL;
  }

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  htsbuf_queue_init(&hc->hc_spill, 0);
  tc->fd = fd;
  tc->read = tcp_read;
  tc->write = tcp_write;
  return tc;
}



/**
 *
 */
void
tcp_close(tcpcon_t *tc)
{
  net_close(tc->fd);
  htsbuf_queue_flush(&hc->hc_spill);
  free(tc);
}
