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
                             asyncio_fd_callback_t *cb, void *opaque);

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

