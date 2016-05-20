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
#pragma once
#include "net.h"
#include "misc/cancellable.h"


#if ENABLE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#elif ENABLE_POLARSSL
#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/havege.h"
#elif defined(__APPLE__)
#include <Security/SecureTransport.h>
#else
#error No SSL/TLS implementation
#endif



struct tcpcon {
  int fd;

  htsbuf_queue_t spill;

  int (*write)(struct tcpcon *, const void *, size_t);
  int (*read)(struct tcpcon *, void *, size_t, int,
	      net_read_cb_t *cb, void *opaque);

#if ENABLE_OPENSSL
  SSL *ssl;
#elif ENABLE_POLARSSL
  ssl_context *ssl;
  void *rndstate;
#elif defined(__APPLE__)
  SSLContextRef ssl;
#endif


  int (*raw_write)(struct tcpcon *, const void *, size_t);
  int (*raw_read)(struct tcpcon *, void *, size_t, int,
                  net_read_cb_t *cb, void *opaque);


  cancellable_t *cancellable;

};

void tcp_cancel(void *aux);

tcpcon_t *tcp_connect_arch(const net_addr_t *addr, char *errbuf,
                           size_t errbufsize, int timeout,
                           struct cancellable *c, int dbg);

void tcp_close_arch(tcpcon_t *tc);

int tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen,
                 const char *hostname, int verify);

void tcp_ssl_close(tcpcon_t *tc);
