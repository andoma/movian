/*
 *  Networking
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

#ifndef NET_H__
#define NET_H__

#include "config.h"
#include <sys/types.h>
#include <stdint.h>
#include "htsmsg/htsbuf.h"


typedef struct net_addr {
  // Somewhat less retarded struct for passing IP addresses

  uint8_t na_family;
  uint16_t na_port;   // host order
  uint8_t na_addr[16];

} net_addr_t;



typedef int (net_read_cb_t)(void *opaque, int done);

typedef struct tcpcon tcpcon_t;

void net_initialize(void);

tcpcon_t *tcp_connect(const char *hostname, int port, char *errbuf,
		      size_t errbufsize, int timeout, int ssl);

tcpcon_t *tcp_from_fd(int fd);

int tcp_write_queue(tcpcon_t *nc, htsbuf_queue_t *q);

int tcp_write_queue_dontfree(tcpcon_t *nc, htsbuf_queue_t *q);

void tcp_printf(tcpcon_t *tc, const char *fmt, ...);

int tcp_read_line(tcpcon_t *nc, char *buf, const size_t bufsize);

int tcp_write_data(tcpcon_t *nc, const char *buf, const size_t bufsize);

int tcp_read_data(tcpcon_t *nc, void *buf, const size_t bufsize,
		  net_read_cb_t *cb, void *opaque);

int tcp_read_data_nowait(tcpcon_t *nc, char *buf, const size_t bufsize);

void tcp_close(tcpcon_t *nc);

void tcp_huge_buffer(tcpcon_t *tc);

void tcp_shutdown(tcpcon_t *tc);

void net_change_nonblocking(int fd, int on);

typedef struct netif {
  uint32_t ipv4;
  char ifname[16];
} netif_t;


netif_t *net_get_interfaces(void);

void net_local_addr_from_fd(net_addr_t *na, int fd);

void net_remote_addr_from_fd(net_addr_t *na, int fd);

void net_fmt_host(char *dst, size_t dstlen, const net_addr_t *na);

#endif /* NET_H__ */
