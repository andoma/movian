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
#include "misc/redblack.h"


typedef struct asyncio_timer {
  LIST_ENTRY(asyncio_timer) at_link;
  int64_t at_expire;
  void (*at_fn)(void *opaque);
  void *at_opaque;
} asyncio_timer_t;

extern struct prop_courier *asyncio_courier;

typedef struct asyncio_fd asyncio_fd_t;

typedef int (asyncio_fd_callback_t)(asyncio_fd_t *af, void *opaque, int event,
                                    int error);

typedef void (asyncio_udp_callback_t)(void *opaque,
				      const void *data,
				      int size,
				      const net_addr_t *remote_addr);

typedef void (asyncio_read_callback_t)(void *opaque, htsbuf_queue_t *q);

void asyncio_init_early(void);

void asyncio_start(void);

/*************************************************************************
 * Non portable API for direct manipulation of fds
 *************************************************************************/
#define ASYNCIO_READ            0x1
#define ASYNCIO_WRITE           0x2
#define ASYNCIO_ERROR           0x4
#define ASYNCIO_TIMEOUT         0x8

asyncio_fd_t *asyncio_add_fd(int fd, int events,
                             asyncio_fd_callback_t *cb, void *opaque,
                             const char *name);


/*************************************************************************
 *
 *************************************************************************/

void asyncio_del_fd(asyncio_fd_t *af);

void asyncio_run_task(void (*fn)(void *aux), void *aux);

// Return current time, must be same time domain as arch_get_ts();
int64_t async_current_time(void);

void asyncio_register_for_network_changes(void (*cb)(const struct netif *ni));

void asyncio_trig_network_change(void);

/*************************************************************************
 * Workers
 *************************************************************************/

int asyncio_add_worker(void (*fn)(void));

void asyncio_wakeup_worker(int id);

/*************************************************************************
 * TCP
 *************************************************************************/

void *asyncio_ssl_create_server(const char *privatekeyfile,
                                const char *certfile);

void *asyncio_ssl_create_client(void);

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
			      int timeout,
                              void *tls,
                              const char *hostname);

asyncio_fd_t *asyncio_attach(const char *name, int fd,
                             asyncio_error_callback_t *error_cb,
                             asyncio_read_callback_t *read_cb,
                             void *opaque,
                             void *tls);

void asyncio_send(asyncio_fd_t *af, const void *buf, size_t len, int cork);

void asyncio_sendq(asyncio_fd_t *af, htsbuf_queue_t *q, int cork);

int asyncio_get_port(asyncio_fd_t *af);

void asyncio_set_timeout_delta_sec(asyncio_fd_t *af, int seconds);

/*************************************************************************
 * UDP
 *************************************************************************/

asyncio_fd_t *asyncio_udp_bind(const char *name,
                               const net_addr_t *na,
			       asyncio_udp_callback_t *cb,
			       void *opaque,
			       int bind_any_on_fail,
                               int broadcast);

void asyncio_udp_send(asyncio_fd_t *af, const void *data, int size,
		      const net_addr_t *remote_addr);

int asyncio_udp_add_membership(asyncio_fd_t *af, const net_addr_t *group,
                               const net_addr_t *interface);

/*************************************************************************
 * Timers
 *************************************************************************/

void asyncio_timer_init(asyncio_timer_t *at, void (*fn)(void *opaque),
			void *opaque);

void asyncio_timer_arm(asyncio_timer_t *at, int64_t ts);

void asyncio_timer_arm_delta_sec(asyncio_timer_t *at, int seconds);

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


void asyncio_dns_cancel(asyncio_dns_req_t *adr);

/*************************************************************************
 * HTTP(S)
 *************************************************************************/

typedef struct asyncio_http_req asyncio_http_req_t;
struct http_req_aux;

asyncio_http_req_t *asyncio_http_req(const char *url,
                                     void (*cb)(struct http_req_aux *req,
                                                void *opaque),
                                     void *opaque,
                                     ...);

void asyncio_http_cancel(asyncio_http_req_t *req);
