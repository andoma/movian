#pragma once
/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include "misc/redblack.h"

extern int64_t async_now;

typedef struct asyncio_timer {
  LIST_ENTRY(asyncio_timer) at_link;
  int64_t at_expire;
  void (*at_fn)(void *opaque);
  void *at_opaque;
} asyncio_timer_t;

extern struct prop_courier *asyncio_courier;

typedef struct asyncio_fd asyncio_fd_t;

typedef void (asyncio_fd_callback_t)(asyncio_fd_t *af, void *opaque, int event,
				     int error);

typedef void (asyncio_udp_callback_t)(void *opaque,
				      const void *data,
				      int size,
				      const net_addr_t *remote_addr);

typedef void (asyncio_read_callback_t)(void *opaque, htsbuf_queue_t *q);

void asyncio_init(void);

/*************************************************************************
 * Low level FD
 *************************************************************************/

#define ASYNCIO_READ            0x1
#define ASYNCIO_WRITE           0x2
#define ASYNCIO_ERROR           0x4
#define ASYNCIO_TIMEOUT         0x8

asyncio_fd_t *asyncio_add_fd(int fd, int events,
                             asyncio_fd_callback_t *cb, void *opaque,
			     const char *name);

void asyncio_set_events(asyncio_fd_t *af, int events);

void asyncio_rem_events(asyncio_fd_t *af, int events);

void asyncio_add_events(asyncio_fd_t *af, int events);

void asyncio_del_fd(asyncio_fd_t *af);

/*************************************************************************
 * Workers
 *************************************************************************/

int asyncio_add_worker(void (*fn)(void));

void asyncio_wakeup_worker(int id);

/*************************************************************************
 * TCP
 *************************************************************************/

typedef void (asyncio_accept_callback_t)(void *opaque,
                                         int fd,
                                         const net_addr_t *local_addr,
                                         const net_addr_t *remote_addr);

typedef void (asyncio_error_callback_t)(void *opaque, const char *err);

asyncio_fd_t *asyncio_listen(const char *name,
                             int port,
                             asyncio_accept_callback_t *cb,
                             void *opaque,
                             int bind_any_on_fail);

asyncio_fd_t *asyncio_connect(const char *name,
			      const net_addr_t *remote_addr,
			      asyncio_error_callback_t *connect_cb,
			      asyncio_read_callback_t *read_cb,
			      void *opaque,
			      int timeout);

void asyncio_send(asyncio_fd_t *af, const void *buf, size_t len, int cork);

void asyncio_sendq(asyncio_fd_t *af, htsbuf_queue_t *q, int cork);

int asyncio_get_port(asyncio_fd_t *af);

void asyncio_set_timeout(asyncio_fd_t *af, int64_t timeout);

/*************************************************************************
 * UDP
 *************************************************************************/

asyncio_fd_t *asyncio_udp_bind(const char *name,
			       int port,
			       asyncio_udp_callback_t *cb,
			       void *opaque,
			       int bind_any_on_fail);

void asyncio_udp_send(asyncio_fd_t *af, const void *data, int size,
		      const net_addr_t *remote_addr);

/*************************************************************************
 * Timers
 *************************************************************************/

void asyncio_timer_init(asyncio_timer_t *at, void (*fn)(void *opaque),
			void *opque);

void asyncio_timer_arm(asyncio_timer_t *at, int64_t expire);

void asyncio_timer_disarm(asyncio_timer_t *at);

static __inline int asyncio_timer_is_armed(const asyncio_timer_t *at)
{
  return at->at_expire != 0;
}

/*************************************************************************
 * DNS
 *************************************************************************/

typedef struct asyncio_dns_req asyncio_dns_req_t;

#define ASYNCIO_DNS_STATUS_QUEUED    1
#define ASYNCIO_DNS_STATUS_PENDING   2
#define ASYNCIO_DNS_STATUS_COMPLETED 3
#define ASYNCIO_DNS_STATUS_FAILED    4

asyncio_dns_req_t *asyncio_dns_lookup_host(const char *hostname,
					   void (*cb)(void *opaque,
						      int status,
						      const void *data),
					   void *opaque);

