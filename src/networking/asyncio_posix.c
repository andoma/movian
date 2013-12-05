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


#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <netinet/in.h>

#include "showtime.h"
#include "arch/arch.h"
#include "asyncio.h"
#include "misc/queue.h"
#include "prop/prop.h"

LIST_HEAD(asyncio_fd_list, asyncio_fd);

static int asyncio_pipe[2];
static struct asyncio_fd_list asyncio_fds;
static int asyncio_num_fds;

struct prop_courier *asyncio_courier;

/**
 *
 */
struct asyncio_fd {
  int af_refcount;
  LIST_ENTRY(asyncio_fd) af_link;
  int af_fd;
  int af_poll_events;
  int af_ext_events;
  asyncio_fd_callback_t *af_callback;
  void *af_opaque;
  asyncio_accept_callback_t *af_accept_callback;
  char *af_name;
  int af_port;
};


/**
 *
 */
static void
asyncio_courier_notify(void *opaque)
{
  if(write(asyncio_pipe[1], "x", 1) != 1)
    TRACE(TRACE_ERROR, "TCP", "Pipe problems");
}


/**
 *
 */
static void
af_release(asyncio_fd_t *af)
{
  af->af_refcount--;
  if(af->af_refcount > 0)
    return;
  free(af->af_name);
  free(af);
}

/**
 *
 */
static void
asyncio_dopoll(void)
{
  asyncio_fd_t *af;
  struct pollfd *fds = alloca(asyncio_num_fds * sizeof(struct pollfd));
  asyncio_fd_t **afds  = alloca(asyncio_num_fds * sizeof(asyncio_fd_t *));
  int n = 0;

  LIST_FOREACH(af, &asyncio_fds, af_link) {
    fds[n].fd = af->af_fd;
    fds[n].events = af->af_poll_events;
    afds[n] = af;
    af->af_refcount++;
    n++;
  }

  assert(n == asyncio_num_fds);

  int r = poll(fds, n, -1);
  if(r == -1) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Poll failed -- %s", strerror(errno));
    sleep(1);
    return;
  }

  for(int i = 0; i < n; i++) {
    af = afds[i];
    if(af->af_callback && fds[i].revents)
      af->af_callback(af,
                      af->af_opaque,
                      (fds[i].revents & POLLIN            ? ASYNCIO_READ : 0) |
                      (fds[i].revents & POLLOUT           ? ASYNCIO_WRITE : 0) |
                      (fds[i].revents & (POLLHUP|POLLERR) ? ASYNCIO_ERROR : 0));

    af_release(af);
  }
}


/**
 *
 */
void
asyncio_set_events(asyncio_fd_t *af, int events)
{
  af->af_ext_events = events;

  af->af_poll_events =
    (events & ASYNCIO_READ  ? POLLIN            : 0) |
    (events & ASYNCIO_WRITE ? POLLOUT           : 0) |
    (events & ASYNCIO_ERROR ? (POLLHUP|POLLERR) : 0);
}


/**
 *
 */
void
asyncio_add_events(asyncio_fd_t *af, int events)
{
  asyncio_set_events(af, af->af_ext_events | events);
}


/**
 *
 */
void
asyncio_rem_events(asyncio_fd_t *af, int events)
{
  asyncio_set_events(af, af->af_ext_events & ~events);
}


/**
 *
 */
asyncio_fd_t *
asyncio_add_fd(int fd, int events, asyncio_fd_callback_t *cb, void *opaque)
{
  asyncio_fd_t *af = calloc(1, sizeof(asyncio_fd_t));
  af->af_refcount = 1;
  af->af_fd = fd;
  asyncio_set_events(af, events);
  af->af_callback = cb;
  af->af_opaque = opaque;

  net_change_nonblocking(fd, 1);

  LIST_INSERT_HEAD(&asyncio_fds, af, af_link);
  asyncio_num_fds++;
  return af;
}


/**
 *
 */
void
asyncio_del_fd(asyncio_fd_t *af)
{
  if(af->af_ext_events & ASYNCIO_CLOSED)
    af->af_callback(af, af->af_opaque, ASYNCIO_CLOSED);
  LIST_REMOVE(af, af_link);
  asyncio_num_fds--;
  af->af_callback = NULL;
  af_release(af);
}


/**
 *
 */
static void
asyncio_courier_poll(asyncio_fd_t *af, void *opaque, int event)
{
  prop_courier_poll(opaque);
}


/**
 *
 */
static void *
asyncio_thread(void *aux)
{
  arch_pipe(asyncio_pipe);

  asyncio_courier = prop_courier_create_notify(asyncio_courier_notify, NULL);
  asyncio_add_fd(asyncio_pipe[0], ASYNCIO_READ, asyncio_courier_poll,
                 asyncio_courier);

  init_group(INIT_GROUP_ASYNCIO);

  while(1)
    asyncio_dopoll();
  return NULL;
}


/**
 *
 */
void
asyncio_init(void)
{
  hts_thread_create_detached("asyncio", asyncio_thread, NULL,
			     THREAD_PRIO_MODEL);
}


/**
 *
 */
static void
asyncio_accept(asyncio_fd_t *af, void *opaque, int events)
{
  if(events & ASYNCIO_CLOSED) {
    close(af->af_fd);
    return;
  }

  assert(events & ASYNCIO_READ);

  struct sockaddr_in si;
  socklen_t sl = sizeof(struct sockaddr_in);
  int fd, val;

  fd = accept(af->af_fd, (struct sockaddr *)&si, &sl);
  net_change_nonblocking(fd, 0);

  if(fd == -1) {
    TRACE(TRACE_ERROR, "TCP", "%s: Accept error: %s", strerror(errno));
    sleep(1);
    return;
  }

  val = 1;
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val));
#ifdef TCP_KEEPIDLE
  val = 30;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val));
#endif

#ifdef TCP_KEEPINVL
  val = 15;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val));
#endif

#ifdef TCP_KEEPCNT
  val = 5;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val));
#endif

#ifdef TCP_NODELAY
  val = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#endif

  net_addr_t local, remote;
  net_local_addr_from_fd(&local, fd);
  net_remote_addr_from_fd(&remote, fd);

  af->af_accept_callback(af->af_opaque, fd, &local, &remote);
}


/**
 *
 */
asyncio_fd_t *
asyncio_listen(const char *name, int port, asyncio_accept_callback_t *cb,
               void *opaque, int bind_any)
{
  struct sockaddr_in si = {0};
  socklen_t sl = sizeof(struct sockaddr_in);
  int one = 1;
  int fd;

  if((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    return NULL;

  si.sin_family = AF_INET;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

  if(port) {

    si.sin_port = htons(port);
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in))) {

      if(!bind_any) {
        TRACE(TRACE_ERROR, "TCP", "%s: Bind failed -- %s", name,
              strerror(errno));
        close(fd);
        return NULL;
      } else {
        port = 0;
      }
    }
  }
  if(!port) {
    si.sin_port = 0;
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
      TRACE(TRACE_ERROR, "TCP", "%s: Unable to bind -- %s", name,
            strerror(errno));
      close(fd);
      return NULL;
    }
  }

  if(getsockname(fd, (struct sockaddr *)&si, &sl) == -1) {
    TRACE(TRACE_ERROR, "TCP", "%s: Unable to figure local port", name);
    close(fd);
    return NULL;
  }
  port = ntohs(si.sin_port);

  listen(fd, 100);

  TRACE(TRACE_INFO, "TCP", "%s: Listening on port %d", name, port);

  asyncio_fd_t *af = asyncio_add_fd(fd, ASYNCIO_READ | ASYNCIO_CLOSED,
                                    asyncio_accept, opaque);
  af->af_accept_callback = cb;
  af->af_name = strdup(name);
  af->af_port = port;
  return af;
}



int
asyncio_get_port(asyncio_fd_t *af)
{
  return af->af_port;
}
