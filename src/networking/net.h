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

#if ENABLE_SSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#include <sys/types.h>
#include <stdint.h>
#include <htsmsg/htsbuf.h>

typedef struct tcpcon {
  int fd;

  int (*write)(struct tcpcon *, const void *, size_t);
  int (*read)(struct tcpcon *, void *, size_t, int);

#if ENABLE_SSL
  SSL *ssl;
#endif

} tcpcon_t;



void net_setup(void);

tcpcon_t *tcp_connect(const char *hostname, int port, char *errbuf,
		      size_t errbufsize, int timeout, int ssl);

int tcp_write_queue(tcpcon_t *nc, htsbuf_queue_t *q);

int tcp_write_queue_dontfree(tcpcon_t *nc, htsbuf_queue_t *q);

int tcp_read_line(tcpcon_t *nc, char *buf, const size_t bufsize,
		  htsbuf_queue_t *spill);

int tcp_read_data(tcpcon_t *nc, char *buf, const size_t bufsize,
		  htsbuf_queue_t *spill);

int tcp_read_data_nowait(tcpcon_t *nc, char *buf, const size_t bufsize, 
			 htsbuf_queue_t *spill);

void tcp_close(tcpcon_t *nc);

#endif /* NET_H__ */
