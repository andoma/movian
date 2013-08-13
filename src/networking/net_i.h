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


#pragma once

#include "net.h"

#if ENABLE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#if ENABLE_POLARSSL
#include "polarssl/net.h"
#include "polarssl/ssl.h"
#include "polarssl/havege.h"
#endif


struct tcpcon {
  int fd;

  htsbuf_queue_t spill;

  int (*write)(struct tcpcon *, const void *, size_t);
  int (*read)(struct tcpcon *, void *, size_t, int,
	      net_read_cb_t *cb, void *opaque);

#if ENABLE_OPENSSL
  SSL *ssl;
#endif

#if ENABLE_POLARSSL
    ssl_context *ssl;
    ssl_session *ssn;
    havege_state *hs;
#endif

};
