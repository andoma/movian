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

extern struct prop_courier *asyncio_courier;

typedef struct asyncio_fd asyncio_fd_t;

typedef void (asyncio_fd_callback_t)(asyncio_fd_t *af, void *opaque, int event);

void asyncio_init(void);

#define ASYNCIO_READ            0x1
#define ASYNCIO_WRITE           0x2
#define ASYNCIO_ERROR           0x4
#define ASYNCIO_CLOSED          0x8

asyncio_fd_t *asyncio_add_fd(int fd, int events,
                             asyncio_fd_callback_t *cb, void *opaque,
			     const char *name);

void asyncio_set_events(asyncio_fd_t *af, int events);

void asyncio_rem_events(asyncio_fd_t *af, int events);

void asyncio_add_events(asyncio_fd_t *af, int events);

void asyncio_del_fd(asyncio_fd_t *af);


typedef void (asyncio_accept_callback_t)(void *opaque,
                                         int fd,
                                         const net_addr_t *local_addr,
                                         const net_addr_t *remote_addr);

asyncio_fd_t *asyncio_listen(const char *name,
                             int port,
                             asyncio_accept_callback_t *cb,
                             void *opaque,
                             int bind_any_on_fail);

int asyncio_get_port(asyncio_fd_t *af);

