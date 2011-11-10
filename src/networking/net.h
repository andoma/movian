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

#if ENABLE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#if ENABLE_POLARSSL
#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/havege.h"
#endif


#include <sys/types.h>
#include <stdint.h>
#include <htsmsg/htsbuf.h>

typedef struct tcpcon {
  int fd;

  htsbuf_queue_t spill;

  int (*write)(struct tcpcon *, const void *, size_t);
  int (*read)(struct tcpcon *, void *, size_t, int);

#if ENABLE_OPENSSL
  SSL *ssl;
#endif

#if ENABLE_POLARSSL
    ssl_context *ssl;
    ssl_session *ssn;
    havege_state *hs;
#endif

} tcpcon_t;


void net_initialize(void);

tcpcon_t *tcp_connect(const char *hostname, int port, char *errbuf,
		      size_t errbufsize, int timeout, int ssl);

int tcp_write_queue(tcpcon_t *nc, htsbuf_queue_t *q);

int tcp_write_queue_dontfree(tcpcon_t *nc, htsbuf_queue_t *q);

int tcp_read_line(tcpcon_t *nc, char *buf, const size_t bufsize);

#define tcp_write_data(tc, data, len) ((tc)->write(tc, data, len))

int tcp_read_data(tcpcon_t *nc, char *buf, const size_t bufsize);

int tcp_read_data_nowait(tcpcon_t *nc, char *buf, const size_t bufsize);

void tcp_close(tcpcon_t *nc);

void tcp_huge_buffer(tcpcon_t *tc);

void tcp_shutdown(tcpcon_t *tc);




typedef struct netif {
  uint32_t ipv4;
  char ifname[16];
} netif_t;


netif_t *net_get_interfaces(void);


#endif /* NET_H__ */
