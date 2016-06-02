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
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <netinet/in.h>

#include "main.h"
#include "arch/arch.h"
#include "arch/threads.h"
#include "asyncio.h"
#include "misc/queue.h"
#include "prop/prop.h"
#include "misc/minmax.h"


/**
 *
 */
#if ENABLE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

#include "net_openssl.h"

static void asyncio_ssl_write(asyncio_fd_t *af);
static void asyncio_ssl_read(asyncio_fd_t *af);
static int asyncio_ssl_events(asyncio_fd_t *af);
static void asyncio_ssl_handshake(asyncio_fd_t *af);

#endif



LIST_HEAD(asyncio_fd_list, asyncio_fd);
LIST_HEAD(asyncio_worker_list, asyncio_worker);
LIST_HEAD(asyncio_timer_list, asyncio_timer);
TAILQ_HEAD(asyncio_dns_req_queue, asyncio_dns_req);
TAILQ_HEAD(asyncio_task_queue, asyncio_task);

static hts_thread_t asyncio_thread_id;

static struct asyncio_timer_list asyncio_timers;

static hts_mutex_t asyncio_worker_mutex;
static struct asyncio_worker_list asyncio_workers;

static int asyncio_pipe[2];
static struct asyncio_fd_list asyncio_fds;
static int asyncio_num_fds;

struct prop_courier *asyncio_courier;

static hts_mutex_t asyncio_dns_mutex;
static int asyncio_dns_worker;
static struct asyncio_dns_req_queue asyncio_dns_pending;
static struct asyncio_dns_req_queue asyncio_dns_completed;

static hts_mutex_t asyncio_task_mutex;
static struct asyncio_task_queue asyncio_tasks;

static void adr_deliver_cb(void);

static int64_t async_now;

static __inline void asyncio_verify_thread(void) {
  assert(hts_thread_current() == asyncio_thread_id);
}

/**
 *
 */
typedef struct asyncio_worker {
  LIST_ENTRY(asyncio_worker) link;
  void (*fn)(void);
  int id;
  int pending;
} asyncio_worker_t;


/**
 *
 */
struct asyncio_fd {
  LIST_ENTRY(asyncio_fd) af_link;
  asyncio_fd_callback_t *af_callback;
  void *af_opaque;
  char *af_name;
  union {
    asyncio_accept_callback_t *af_accept_callback;
    asyncio_udp_callback_t    *af_udp_callback;
    asyncio_error_callback_t  *af_error_callback;
  };


  asyncio_read_callback_t *af_read_callback;

  htsbuf_queue_t af_sendq;
  htsbuf_queue_t af_recvq;

  int64_t af_timeout;

  int af_refcount;
  int af_fd;
  int af_poll_events;
  int af_pending_errno;

  uint16_t af_ext_events;
  uint8_t af_connected;

  char *af_hostname;
  
  net_addr_t af_bind_addr;

  void (*af_resume)(struct asyncio_fd *af);
  
  int af_suspended : 1;
  int af_bind_any : 1;
  int af_broadcast : 1;

#if ENABLE_OPENSSL
  int af_ssl_read_status;
  int af_ssl_write_status;
  int af_ssl_established;
  SSL *af_ssl;
#endif

};


/**
 *
 */
typedef struct asyncio_task {
  TAILQ_ENTRY(asyncio_task) at_link;
  void (*at_fn)(void *aux);
  void *at_aux;
} asyncio_task_t;




/**
 *
 */
int64_t
async_current_time(void)
{
  return async_now;
}


/**
 *
 */
static void
no_sigpipe(int fd)
{
#ifdef SO_NOSIGPIPE
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val));
#endif
}


/**
 *
 */
static void
net_addr_from_sockaddr_in(net_addr_t *na, const struct sockaddr_in *sin)
{
  na->na_family = 4;
  na->na_port = ntohs(sin->sin_port);
  memcpy(na->na_addr, &sin->sin_addr, 4);
}


/**
 *
 */
static void
net_local_addr_from_fd(net_addr_t *na, int fd)
{
  socklen_t slen = sizeof(struct sockaddr_in);
  struct sockaddr_in self;

  if(!getsockname(fd, (struct sockaddr *)&self, &slen)) {
    net_addr_from_sockaddr_in(na, &self);
  } else {
    memset(na, 0, sizeof(net_addr_t));
  }
}


/**
 *
 */
static void
net_remote_addr_from_fd(net_addr_t *na, int fd)
{
  socklen_t slen = sizeof(struct sockaddr_in);
  struct sockaddr_in self;

  if(!getpeername(fd, (struct sockaddr *)&self, &slen)) {
    net_addr_from_sockaddr_in(na, &self);
  } else {
    memset(na, 0, sizeof(net_addr_t));
  }
}


/**
 *
 */
static void
asyncio_wakeup(int id)
{
  char x = id;
  int r = write(asyncio_pipe[1], &x, 1);

  if(r != 1)
    TRACE(TRACE_ERROR, "TCP", "Pipe problems r=%d errno=%d", r, errno);
}

/**
 *
 */
static void
asyncio_courier_notify(void *opaque)
{
  asyncio_wakeup(0);
}


/**
 *
 */
void
asyncio_wakeup_worker(int id)
{
  return asyncio_wakeup(id);
}


/**
 *
 */
void
asyncio_timer_init(asyncio_timer_t *at, void (*fn)(void *opaque),
		   void *opaque)
{
  at->at_fn = fn;
  at->at_opaque = opaque;
  at->at_expire = 0;
}


/**
 *
 */
static int
at_compar(const asyncio_timer_t *a, const asyncio_timer_t *b)
{
  if(a->at_expire < b->at_expire)
    return -1;
  return 1;
}


/**
 *
 */
void
asyncio_timer_arm(asyncio_timer_t *at, int64_t expire)
{
  asyncio_verify_thread();
  if(at->at_expire)
    LIST_REMOVE(at, at_link);

  at->at_expire = expire;
  LIST_INSERT_SORTED(&asyncio_timers, at, at_link, at_compar, asyncio_timer_t);
}


/**
 *
 */
void
asyncio_timer_arm_delta_sec(asyncio_timer_t *at, int delta)
{
  asyncio_timer_arm(at, async_now + delta * 1000000LL);
}


/**
 *
 */
void
asyncio_timer_disarm(asyncio_timer_t *at)
{
  asyncio_verify_thread();
  if(at->at_expire) {
    LIST_REMOVE(at, at_link);
    at->at_expire = 0;
  }
}


/**
 *
 */
static void
af_release(asyncio_fd_t *af)
{
  asyncio_verify_thread();
  af->af_refcount--;
  if(af->af_refcount > 0)
    return;
  htsbuf_queue_flush(&af->af_recvq);
  htsbuf_queue_flush(&af->af_sendq);
  free(af->af_name);
  free(af->af_hostname);
#if ENABLE_OPENSSL
  if(af->af_ssl != NULL) {
    SSL_shutdown(af->af_ssl);
    SSL_free(af->af_ssl);
  }
#endif
  free(af);
}


/**
 *
 */
static int
events_to_poll(int events)
{
  return
    (events & ASYNCIO_READ  ? POLLIN            : 0) |
    (events & ASYNCIO_WRITE ? POLLOUT           : 0) |
    (events & ASYNCIO_ERROR ? (POLLHUP|POLLERR) : 0);
}

/**
 *
 */
static void
asyncio_dopoll(void)
{
  asyncio_timer_t *at;

  while((at = LIST_FIRST(&asyncio_timers)) != NULL &&
        at->at_expire <= async_now) {
    LIST_REMOVE(at, at_link);
    at->at_expire = 0;
    at->at_fn(at->at_opaque);
  }

  asyncio_fd_t *af;
  struct pollfd *fds = alloca(asyncio_num_fds * sizeof(struct pollfd));
  asyncio_fd_t **afds  = alloca(asyncio_num_fds * sizeof(asyncio_fd_t *));
  int n = 0;

  int timeout = INT32_MAX;

  LIST_FOREACH(af, &asyncio_fds, af_link) {
    if(af->af_pending_errno) {
      af->af_callback(af, af->af_opaque, ASYNCIO_ERROR, af->af_pending_errno);
      goto release;
    }

    if(af->af_timeout) {
      if(af->af_timeout <= async_now) {
        af->af_timeout = 0;
        af->af_callback(af, af->af_opaque, ASYNCIO_TIMEOUT, 0);
        goto release;
      }
      timeout = MIN(timeout, (af->af_timeout - async_now + 999) / 1000);
    }

    if(af->af_fd == -1) {
      continue;
    }
    
    fds[n].fd = af->af_fd;

#if ENABLE_OPENSSL
    if(af->af_ssl != NULL)
      fds[n].events = asyncio_ssl_events(af);
    else
#endif
      fds[n].events = af->af_poll_events;

    fds[n].revents = 0;
    afds[n] = af;

    af->af_refcount++;
    n++;
  }

  if((at = LIST_FIRST(&asyncio_timers)) != NULL)
    timeout = MIN(timeout, (at->at_expire - async_now + 999) / 1000);

  if(timeout == INT32_MAX)
    timeout = -1;

  int err = poll(fds, n, timeout);

  async_now = arch_get_ts();

  for(int i = 0; i < n; i++) {
    af = afds[i];

    if(af->af_callback == NULL)
      continue;

    if(fds[i].revents & POLLHUP) {
      af->af_callback(af, af->af_opaque, ASYNCIO_ERROR, ECONNRESET);
      continue;
    }

    if(fds[i].revents & POLLERR || err < 0) {
      int err;
      socklen_t errlen = sizeof(int);

      if(getsockopt(af->af_fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen)) {
        TRACE(TRACE_ERROR, "ASYNCIO", "getsockopt failed for %s 0x%x -- %s",
              af->af_name, af->af_fd, strerror(errno));
        af->af_callback(af, af->af_opaque, ASYNCIO_ERROR, ENOBUFS);
      } else {
        if(err) {
          af->af_callback(af, af->af_opaque, ASYNCIO_ERROR, err);
          continue;
        }
      }
    }

    const int events =
      (fds[i].revents & POLLIN  ? ASYNCIO_READ  : 0) |
      (fds[i].revents & POLLOUT ? ASYNCIO_WRITE : 0);

    if(events)
      af->af_callback(af, af->af_opaque, events, 0);

    if(0) {
      int64_t now = arch_get_ts();

      if(now - async_now > 10000) {
        TRACE(TRACE_ERROR, "ASYNCIO", "Long callback on socktet %s (%d µs)",
              af->af_name, (int) (now - async_now));
      }
      async_now = now;
    }
  }


 release:

  for(int i = 0; i < n; i++)
    af_release(afds[i]);
}


/**
 *
 */
static void
asyncio_set_events(asyncio_fd_t *af, int events)
{
  asyncio_verify_thread();
  af->af_ext_events = events;

  af->af_poll_events = events_to_poll(events);
}


/**
 *
 */
static void
asyncio_add_events(asyncio_fd_t *af, int events)
{
  asyncio_set_events(af, af->af_ext_events | events);
}


/**
 *
 */
static void
asyncio_rem_events(asyncio_fd_t *af, int events)
{
  asyncio_set_events(af, af->af_ext_events & ~events);
}


/**
 *
 */
asyncio_fd_t *
asyncio_add_fd(int fd, int events, asyncio_fd_callback_t *cb, void *opaque,
	       const char *name)
{
  asyncio_verify_thread();
  asyncio_fd_t *af = calloc(1, sizeof(asyncio_fd_t));
  htsbuf_queue_init(&af->af_recvq, INT32_MAX);
  htsbuf_queue_init(&af->af_sendq, INT32_MAX);
  af->af_refcount = 1;
  af->af_fd = fd;
  af->af_name = strdup(name);
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
  asyncio_verify_thread();

#if ENABLE_OPENSSL
  if(af->af_ssl != NULL) {
    SSL_shutdown(af->af_ssl);
    SSL_free(af->af_ssl);
    af->af_ssl = NULL;
  }
#endif

  if(af->af_fd != -1)
    close(af->af_fd);
  af->af_fd = -1;
  LIST_REMOVE(af, af_link);
  asyncio_num_fds--;
  af->af_callback = NULL;
  af_release(af);
}


/**
 *
 */
void
asyncio_set_timeout_delta_sec(asyncio_fd_t *af, int delta)
{
  af->af_timeout = delta * 1000000LL + async_now;
}

/**
 *
 */
static int
asyncio_handle_pipe(asyncio_fd_t *af, void *opaque, int event, int error)
{
  char x;
  if(read(asyncio_pipe[0], &x, 1) != 1)
    return 0;

  if(x == 0) {
    prop_courier_poll(opaque);
    return 0;
  }

  if(x == 1) {
    struct asyncio_task_queue atq;
    asyncio_task_t *at, *next;

    hts_mutex_lock(&asyncio_task_mutex);
    TAILQ_MOVE(&atq, &asyncio_tasks, at_link);
    TAILQ_INIT(&asyncio_tasks);
    hts_mutex_unlock(&asyncio_task_mutex);

    for(at = TAILQ_FIRST(&atq); at != NULL; at = next) {
      next = TAILQ_NEXT(at, at_link);
      at->at_fn(at->at_aux);
      free(at);
    }
    return 0;
  }

  asyncio_worker_t *aw;
  hts_mutex_lock(&asyncio_worker_mutex);

  LIST_FOREACH(aw, &asyncio_workers, link)
    if(aw->id == x)
      break;

  hts_mutex_unlock(&asyncio_worker_mutex);

  if(aw != NULL)
    aw->fn();
  return 0;
}


/**
 *
 */
int
asyncio_add_worker(void (*fn)(void))
{
  asyncio_worker_t *aw = calloc(1, sizeof(asyncio_worker_t));

  aw->fn = fn;

  static  int generator = 1;

  hts_mutex_lock(&asyncio_worker_mutex);
  generator++;
  aw->id = generator;
  LIST_INSERT_HEAD(&asyncio_workers, aw, link);
  hts_mutex_unlock(&asyncio_worker_mutex);
  return aw->id;
}



/**
 *
 */
static void *
asyncio_thread(void *aux)
{
  asyncio_thread_id = hts_thread_current();

  asyncio_courier = prop_courier_create_notify(asyncio_courier_notify, NULL);

  asyncio_add_fd(asyncio_pipe[0], ASYNCIO_READ, asyncio_handle_pipe,
                 asyncio_courier, "Pipe");

  async_now = arch_get_ts();

  init_group(INIT_GROUP_ASYNCIO);

  asyncio_trig_network_change();

  while(1)
    asyncio_dopoll();
  return NULL;
}


static void
asyncio_do_shutdown(void *aux)
{
  fini_group(INIT_GROUP_ASYNCIO);
}


/**
 *
 */
static void
asyncio_shutdown(void *opaque, int retcode)
{
  TRACE(TRACE_DEBUG, "ASYNCIO", "Shutdown");
  asyncio_run_task(asyncio_do_shutdown, NULL);
}


/**
 *
 */
void
asyncio_init_early(void)
{
  TAILQ_INIT(&asyncio_tasks);
  TAILQ_INIT(&asyncio_dns_pending);
  TAILQ_INIT(&asyncio_dns_completed);
  TAILQ_INIT(&asyncio_tasks);

  hts_mutex_init(&asyncio_worker_mutex);
  hts_mutex_init(&asyncio_dns_mutex);
  hts_mutex_init(&asyncio_task_mutex);

  arch_pipe(asyncio_pipe);

  asyncio_dns_worker = asyncio_add_worker(adr_deliver_cb);
}

/**
 *
 */
void
asyncio_start(void)
{
  hts_thread_create_detached("asyncio", asyncio_thread,
                             NULL, THREAD_PRIO_MODEL);

  shutdown_hook_add(asyncio_shutdown, NULL, 1);
}

/**
 *
 */
void
asyncio_run_task(void (*fn)(void *aux), void *aux)
{
  asyncio_task_t *at = malloc(sizeof(asyncio_task_t));
  at->at_fn = fn;
  at->at_aux = aux;

  hts_mutex_lock(&asyncio_task_mutex);
  int do_signal = TAILQ_EMPTY(&asyncio_tasks);
  TAILQ_INSERT_TAIL(&asyncio_tasks, at, at_link);
  hts_mutex_unlock(&asyncio_task_mutex);
  if(do_signal)
    asyncio_wakeup(1);
}


/**
 *
 */
static int
asyncio_tcp_accept(asyncio_fd_t *af, void *opaque, int events, int error)
{
  assert(events & ASYNCIO_READ);

  struct sockaddr_in si;
  socklen_t sl = sizeof(struct sockaddr_in);
  int fd, val;

  fd = accept(af->af_fd, (struct sockaddr *)&si, &sl);
  net_change_nonblocking(fd, 0);

  if(fd == -1) {
    TRACE(TRACE_ERROR, "TCP", "%s: Accept error: %s",
          af->af_name, strerror(errno));
    sleep(1);
    return 0;
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

#ifdef __PPU__
#define TCP_NODELAY 1
#endif

#ifdef TCP_NODELAY
  val = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#endif

  net_addr_t local, remote;
  net_local_addr_from_fd(&local, fd);
  net_remote_addr_from_fd(&remote, fd);

  af->af_accept_callback(af->af_opaque, fd, &local, &remote);
  return 0;
}



static int
asyncio_tcp_bind_socket(int bind_any, int port, const char *name)
{
  struct sockaddr_in si = {0};
  int one = 1;
  int fd;

  if((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    return -1;

  no_sigpipe(fd);

  si.sin_family = AF_INET;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

  if(port) {
  
    si.sin_port = htons(port);
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in))) {
      if(!bind_any) {
        TRACE(TRACE_ERROR, "TCP", "%s: Bind failed -- %s", name,
              strerror(errno));
        close(fd);
        return -1;
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
      return -1;
    }
  }

  listen(fd, 100);
  return fd;
}


/**
 *
 */
static void
asyncio_tcp_resume(asyncio_fd_t *af)
{
  int fd = asyncio_tcp_bind_socket(af->af_bind_any, af->af_bind_addr.na_port,
                                   af->af_name);
  if(fd == -1)
    return;

  net_change_nonblocking(fd, 1);
  af->af_fd = fd;
  asyncio_set_events(af, ASYNCIO_READ);
  af->af_suspended = 0;
  TRACE(TRACE_INFO, "TCP", "%s: Resumed listening on port %d", af->af_name, asyncio_get_port(af));
}


/**
 *
 */
asyncio_fd_t *
asyncio_listen(const char *name, int port, asyncio_accept_callback_t *cb,
               void *opaque, int bind_any)
{
  int fd = asyncio_tcp_bind_socket(bind_any, port, name);
  if(fd == -1)
    return NULL;
  
  asyncio_fd_t *af = asyncio_add_fd(fd, ASYNCIO_READ,
                                    asyncio_tcp_accept, opaque, name);
  af->af_bind_any = bind_any;
  af->af_bind_addr.na_port = port;
  
  TRACE(TRACE_INFO, "TCP", "%s: Listening on port %d", name, asyncio_get_port(af));
  
  af->af_accept_callback = cb;
  af->af_resume = asyncio_tcp_resume;
  return af;
}

/**
 *
 */
static void
do_write(asyncio_fd_t *af)
{
#if ENABLE_OPENSSL
  if(af->af_ssl != NULL) {
    asyncio_ssl_write(af);
    return;
  }
#endif

  char tmp[1024];

  while(1) {
    int avail = htsbuf_peek(&af->af_sendq, tmp, sizeof(tmp));
    if(avail == 0) {
      // Nothing more to send
      asyncio_rem_events(af, ASYNCIO_WRITE);
      return;
    }

#ifdef MSG_NOSIGNAL
    int r = send(af->af_fd, tmp, avail, MSG_NOSIGNAL);
#else
    int r = send(af->af_fd, tmp, avail, 0);
#endif
    if(r == 0)
      break;

    if(r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS))
      break;

    if(r == -1) {
      asyncio_rem_events(af, ASYNCIO_WRITE);
      af->af_pending_errno = errno;
      return;
    }

    htsbuf_drop(&af->af_sendq, r);
    if(r != avail)
      break;
  }
  asyncio_add_events(af, ASYNCIO_WRITE);
}



/**
 *
 */
static void
do_read(asyncio_fd_t *af)
{
  char tmp[1024];
  while(1) {
    int r = read(af->af_fd, tmp, sizeof(tmp));
    if(r == 0) {
      af->af_error_callback(af->af_opaque, "Connection reset");
      return;
    }

    if(r == -1 && (errno == EAGAIN))
      break;

    if(r == -1) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s", strerror(errno));
      af->af_error_callback(af->af_opaque, buf);
      return;
    }

    htsbuf_append(&af->af_recvq, tmp, r);
  }

  af->af_read_callback(af->af_opaque, &af->af_recvq);
}


/**
 *
 */
static int
asyncio_tcp_connected(asyncio_fd_t *af, void *opaque, int events, int error)
{
  if(events & ASYNCIO_TIMEOUT) {
    af->af_error_callback(af->af_opaque, "Connection timed out");
    return 0;
  }

  if(events & ASYNCIO_ERROR) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", strerror(error));
    af->af_timeout = 0;
    af->af_error_callback(af->af_opaque, buf);
    return 0;
  }

  if(events & ASYNCIO_READ) {
    af->af_timeout = 0;
#if ENABLE_OPENSSL
    if(af->af_ssl != NULL) {
      asyncio_ssl_read(af);
      return 0;
    }
#endif
    do_read(af);
    return 0;
  }

  if(events & ASYNCIO_WRITE) {

    if(af->af_connected) {
#if ENABLE_OPENSSL
      if(af->af_ssl != NULL) {
        asyncio_ssl_write(af);
        return 0;
      }
#endif
      do_write(af);
      return 0;
    }

    af->af_timeout = 0;

    asyncio_rem_events(af, ASYNCIO_WRITE);
    int err;
    socklen_t errlen = sizeof(int);

    if(getsockopt(af->af_fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen)) {
      TRACE(TRACE_ERROR, "ASYNCIO", "getsockopt failed for %s 0x%x -- %d",
            af->af_name, af->af_fd, errno);
      return 0;
    }

    if(err) {
      char buf[256];
      snprintf(buf, sizeof(buf), "%s", strerror(errno));
      af->af_error_callback(af->af_opaque, buf);
    } else {


#if ENABLE_OPENSSL
      if(af->af_ssl != NULL) {
        if(SSL_set_fd(af->af_ssl, af->af_fd) == 0) {
          TRACE(TRACE_ERROR, "ASYNCIO", "SSL: Unable to set FD");
        }
        SSL_set_connect_state(af->af_ssl);
        asyncio_ssl_handshake(af);
        af->af_connected = 2;
        return 0;
      }
#endif

      af->af_connected = 1;

      af->af_error_callback(af->af_opaque, NULL);
      do_write(af);
    }
  }
  return 0;
}




/**
 *
 */
void
asyncio_send(asyncio_fd_t *af, const void *buf, size_t len, int cork)
{
  asyncio_verify_thread();
  htsbuf_append(&af->af_sendq, buf, len);
  if(af->af_fd != -1 && !cork)
    do_write(af);
}


/**
 *
 */
void
asyncio_sendq(asyncio_fd_t *af, htsbuf_queue_t *q, int cork)
{
  asyncio_verify_thread();
  htsbuf_appendq(&af->af_sendq, q);
  if(af->af_fd != -1 && !cork)
    do_write(af);
}


/**
 *
 */
asyncio_fd_t *
asyncio_connect(const char *name, const net_addr_t *addr,
		asyncio_error_callback_t *error_cb,
		asyncio_read_callback_t *read_cb,
		void *opaque, int timeout,
                void *tlsctx, const char *hostname)
{
  struct sockaddr_in si = {0};
  int fd;

  if((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1)
    return NULL;

  no_sigpipe(fd);

  net_change_nonblocking(fd, 1);

  net_change_ndelay(fd, 1);

  si.sin_family = AF_INET;
  si.sin_port = htons(addr->na_port);
  memcpy(&si.sin_addr, addr->na_addr, 4);

  asyncio_fd_t *af = asyncio_add_fd(fd, ASYNCIO_READ,
				    asyncio_tcp_connected, opaque,
                                    name);

  af->af_error_callback = error_cb;
  af->af_read_callback  = read_cb;
  af->af_timeout = arch_get_ts() + timeout * 1000;
  af->af_hostname = hostname ? strdup(hostname) : NULL;

#if ENABLE_OPENSSL
  if(tlsctx != NULL) {
    af->af_ssl = SSL_new(tlsctx);
    if(hostname != NULL)
      SSL_set_tlsext_host_name(af->af_ssl, hostname);
  }
#endif

  int r = connect(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in));
  if(r == -1) {

    if(errno == EINPROGRESS) {
      asyncio_add_events(af, ASYNCIO_WRITE);
    } else {
      // Got fail directly, but we still want to notify the user about
      // the error asynchronously. Just to make things easier
      af->af_pending_errno = errno;
    }
  } else {
    asyncio_add_events(af, ASYNCIO_WRITE);
  }
  return af;
}


/**
 *
 */
asyncio_fd_t *
asyncio_attach(const char *name, int fd,
               asyncio_error_callback_t *error_cb,
               asyncio_read_callback_t *read_cb,
               void *opaque,
               void *tlsctx)
{
  no_sigpipe(fd);
  net_change_nonblocking(fd, 1);
  net_change_ndelay(fd, 1);

  asyncio_fd_t *af = asyncio_add_fd(fd, ASYNCIO_READ | ASYNCIO_ERROR,
				    asyncio_tcp_connected, opaque,
                                    name);
#if ENABLE_OPENSSL
  if(tlsctx != NULL) {
    af->af_ssl = SSL_new(tlsctx);
    if(SSL_set_fd(af->af_ssl, fd) == 0) {
      TRACE(TRACE_ERROR, "ASYNCIO", "SSL: Unable to set FD");
    }
    SSL_set_accept_state(af->af_ssl);
  }
#endif

  af->af_connected = 1;
  af->af_fd = fd;
  af->af_error_callback = error_cb;
  af->af_read_callback  = read_cb;
  return af;
}



int
asyncio_get_port(asyncio_fd_t *af)
{
  struct sockaddr_in si = {0};
  socklen_t sl = sizeof(struct sockaddr_in);

  if(getsockname(af->af_fd, (struct sockaddr *)&si, &sl) == -1)
    return -1;

  return ntohs(si.sin_port);
}


/**
 * DNS handling
 */

struct asyncio_dns_req {
  TAILQ_ENTRY(asyncio_dns_req) adr_link;
  char *adr_hostname;
  void *adr_opaque;
  void (*adr_cb)(void *opaque, int status, const void *data);

  int adr_status;
  int adr_cancelled;
  const void *adr_data;
  const char *adr_errmsg;
  net_addr_t adr_addr;
};


static int adr_resolver_running;

/**
 *
 */
static int
adr_resolve(asyncio_dns_req_t *adr)
{
  return net_resolve(adr->adr_hostname, &adr->adr_addr, &adr->adr_errmsg);
}


/**
 *
 */
static void *
adr_resolver(void *aux)
{
  asyncio_dns_req_t *adr;
  hts_mutex_lock(&asyncio_dns_mutex);
  while((adr = TAILQ_FIRST(&asyncio_dns_pending)) != NULL) {
    TAILQ_REMOVE(&asyncio_dns_pending, adr, adr_link);

    hts_mutex_unlock(&asyncio_dns_mutex);

    
    if(adr_resolve(adr)) {
      adr->adr_status = ASYNCIO_DNS_STATUS_FAILED;
      adr->adr_data = adr->adr_errmsg;
    } else {
      adr->adr_status = ASYNCIO_DNS_STATUS_COMPLETED;
      adr->adr_data = &adr->adr_addr;
    }
    hts_mutex_lock(&asyncio_dns_mutex);
    TAILQ_INSERT_TAIL(&asyncio_dns_completed, adr, adr_link);
    asyncio_wakeup(asyncio_dns_worker);
  }

  adr_resolver_running = 0;
  hts_mutex_unlock(&asyncio_dns_mutex);
  return NULL;
}

/**
 *
 */
asyncio_dns_req_t *
asyncio_dns_lookup_host(const char *hostname, 
			void (*cb)(void *opaque,
				   int status,
				   const void *data),
			void *opaque)
{
  asyncio_dns_req_t *adr;

  adr = calloc(1, sizeof(asyncio_dns_req_t));
  adr->adr_hostname = strdup(hostname);
  adr->adr_cb = cb;
  adr->adr_opaque = opaque;
  
  hts_mutex_lock(&asyncio_dns_mutex);
  TAILQ_INSERT_TAIL(&asyncio_dns_pending, adr, adr_link);
  if(!adr_resolver_running) {
    adr_resolver_running = 1;
    hts_thread_create_detached("DNS resolver", adr_resolver, NULL, 
			       THREAD_PRIO_BGTASK);
  }
  hts_mutex_unlock(&asyncio_dns_mutex);
  return adr;
}


/**
 * Return async DNS requests to caller
 */
static void
adr_deliver_cb(void)
{
  asyncio_dns_req_t *adr;

  hts_mutex_lock(&asyncio_dns_mutex);

  while((adr = TAILQ_FIRST(&asyncio_dns_completed)) != NULL) {
    TAILQ_REMOVE(&asyncio_dns_completed, adr, adr_link);
    hts_mutex_unlock(&asyncio_dns_mutex);
    if(!adr->adr_cancelled)
      adr->adr_cb(adr->adr_opaque, adr->adr_status, adr->adr_data);

    free(adr->adr_hostname);
    free(adr);
    hts_mutex_lock(&asyncio_dns_mutex);
  }
  hts_mutex_unlock(&asyncio_dns_mutex);
}


/**
 * Cancel a pending DNS lookup
 */
void
asyncio_dns_cancel(asyncio_dns_req_t *adr)
{
  asyncio_verify_thread();
  adr->adr_cancelled = 1;
}




/*************************************************************************
 * UDP
 *************************************************************************/




static int
asyncio_udp_bind_socket(int bind_any, int broadcast, const net_addr_t *na,
                        const char *name)
{
  int fd;
  int one = 1;
  struct sockaddr_in si = {0};
  
  if((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    return -1;
  
  no_sigpipe(fd);
  
  si.sin_family = AF_INET;

  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
#if defined(SO_REUSEPORT)
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(int));
#endif
  
  if(broadcast)
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));

  if(na) {
  
    si.sin_port = htons(na->na_port);
    memcpy(&si.sin_addr, na->na_addr, 4);
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in))) {
      
      if(!bind_any) {
        TRACE(TRACE_ERROR, "UDP", "%s: Bind failed -- %s", name,
              strerror(errno));
        close(fd);
        return -1;
      } else {
        na = NULL;
      }
    }
  }
  if(na == NULL) {
    si.sin_port = 0;
    if(bind(fd, (struct sockaddr *)&si, sizeof(struct sockaddr_in)) == -1) {
      TRACE(TRACE_ERROR, "UDP", "%s: Unable to bind -- %s", name,
            strerror(errno));
      close(fd);
      return -1;
    }
  }
  return fd;
}


/**
 *
 */
static void
asyncio_udp_resume(asyncio_fd_t *af)
{
  int fd = asyncio_udp_bind_socket(af->af_bind_any, af->af_broadcast,
                                   af->af_bind_addr.na_family ?
                                   &af->af_bind_addr : NULL, af->af_name);
  if(fd == -1)
    return;

  net_change_nonblocking(fd, 1);
  af->af_fd = fd;
  asyncio_set_events(af, af->af_ext_events);
  af->af_suspended = 0;
  TRACE(TRACE_INFO, "UDP", "%s: Resumed listening on port %d", af->af_name, asyncio_get_port(af));
}


/**
 *
 */
static int
asyncio_udp_event(asyncio_fd_t *af, void *opaque, int events, int error)
{
  static uint8_t udp_recv_buf[8192];

  if(events & ASYNCIO_ERROR) {
    close(af->af_fd);
    af->af_fd = -1;
    af->af_suspended = 1;
    return 0;
  }

  assert(events & ASYNCIO_READ);

  struct sockaddr_in sin;
  socklen_t sl = sizeof(struct sockaddr_in);


  int r = recvfrom(af->af_fd, &udp_recv_buf, sizeof(udp_recv_buf), 0,
		   (struct sockaddr *)&sin, &sl);
  if(r <= 0)
    return 0;
  net_addr_t na = {0};

  na.na_family = 4;
  na.na_port = ntohs(sin.sin_port);
  memcpy(na.na_addr, &sin.sin_addr, 4);
  af->af_udp_callback(opaque, udp_recv_buf, r, &na);
  return 0;
}


/**
 *
 */
asyncio_fd_t *
asyncio_udp_bind(const char *name,
                 const net_addr_t *na,
		 asyncio_udp_callback_t *cb,
		 void *opaque,
		 int bind_any,
                 int broadcast)
{
  int fd = asyncio_udp_bind_socket(bind_any, broadcast, na, name);
  if(fd == -1)
    return NULL;

  asyncio_fd_t *af = asyncio_add_fd(fd, ASYNCIO_READ,
                                    asyncio_udp_event, opaque, name);
  af->af_udp_callback = cb;
  af->af_bind_any = bind_any;
  af->af_broadcast = broadcast;
  if(na != NULL)
    af->af_bind_addr = *na;
  
  af->af_resume = asyncio_udp_resume;
  TRACE(TRACE_INFO, "UDP", "%s: Listening on port %d", name, asyncio_get_port(af));
  return af;
}


/**
 *
 */
void
asyncio_udp_send(asyncio_fd_t *af, const void *data, int size,
		 const net_addr_t *remote_addr)
{
  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));

  sin.sin_family = AF_INET;
  sin.sin_port = htons(remote_addr->na_port);
  memcpy(&sin.sin_addr, remote_addr->na_addr, 4);
  sendto(af->af_fd, data, size, 0,
	 (const struct sockaddr *)&sin, sizeof(struct sockaddr_in));
}


#ifndef IP_ADD_MEMBERSHIP
#define IP_ADD_MEMBERSHIP		12
struct ip_mreq {
  struct in_addr imr_multiaddr;
  struct in_addr imr_interface;
};
#endif

/**
 *
 */
int
asyncio_udp_add_membership(asyncio_fd_t *af, const net_addr_t *group,
                           const net_addr_t *interface)
{
  struct ip_mreq imr = {};
  memcpy(&imr.imr_multiaddr.s_addr, group->na_addr, 4);
  if(interface != NULL)
    memcpy(&imr.imr_interface.s_addr, interface->na_addr, 4);
  return setsockopt(af->af_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imr,
                    sizeof(struct ip_mreq));
}

/*************************************************************************
 * Network interface changes
 *************************************************************************/
typedef struct netifchange {
  void (*cb)(const struct netif *ni);
  LIST_ENTRY(netifchange) link;
} netifchange_t;

static LIST_HEAD(, netifchange) netifchanges;


void
asyncio_register_for_network_changes(void (*cb)(const struct netif *ni))
{
  asyncio_verify_thread();
  netifchange_t *nic = malloc(sizeof(netifchange_t));
  nic->cb = cb;
  LIST_INSERT_HEAD(&netifchanges, nic, link);
  struct netif *ni = net_get_interfaces();
  nic->cb(ni);
  free(ni);
}


/**
 *
 */
static void
asyncio_do_network_change(void *aux)
{
  netifchange_t *nic;
  struct netif *ni = net_get_interfaces();
  LIST_FOREACH(nic, &netifchanges, link) {
    nic->cb(ni);
  }
  free(ni);
}


/**
 *
 */
void
asyncio_trig_network_change(void)
{
  net_refresh_network_status();
  asyncio_run_task(asyncio_do_network_change, NULL);
}


/**
 *
 */
static void
asyncio_do_suspend(void *aux)
{
  netifchange_t *nic;
  LIST_FOREACH(nic, &netifchanges, link) {
    nic->cb(NULL);
  }
  
  asyncio_fd_t *af;
  LIST_FOREACH(af, &asyncio_fds, af_link) {
    if(af->af_resume == NULL)
      continue; // Socket can't be resumed, skip
    
    if(af->af_fd == -1)
      continue;
    af->af_suspended = 1;
    close(af->af_fd);
    af->af_fd = -1;
  }
}

/**
 *
 */
static void
asyncio_do_resume(void *aux)
{
  asyncio_fd_t *af;
  LIST_FOREACH(af, &asyncio_fds, af_link) {
    if(af->af_suspended) {
      af->af_resume(af);
    }
  }
  asyncio_do_network_change(NULL);
}


/**
 *
 */
void
asyncio_suspend(void)
{
  asyncio_run_task(asyncio_do_suspend, NULL);
}

/**
 *
 */
void
asyncio_resume(void)
{
  asyncio_run_task(asyncio_do_resume, NULL);
}



#if ENABLE_OPENSSL


/**
 *
 */
static void
asyncio_ssl_handshake(asyncio_fd_t *af)
{
  char errbuf[512];

  int r = SSL_do_handshake(af->af_ssl);
  int err = SSL_get_error(af->af_ssl, r);
  switch(err) {

  case SSL_ERROR_WANT_READ:
  case SSL_ERROR_WANT_WRITE:
    af->af_ssl_read_status = err;
    break;

  case SSL_ERROR_NONE:
    af->af_ssl_read_status = 0;
    af->af_ssl_established = 1;

    if(af->af_connected == 2) {
      if(openssl_verify_connection(af->af_ssl, af->af_hostname,
                                   errbuf, sizeof(errbuf), 1)) {
        af->af_error_callback(af->af_opaque, errbuf);
        return;
      }
      af->af_connected = 1;
      af->af_error_callback(af->af_opaque, NULL);
    }
    break;

  default:
    TRACE(TRACE_ERROR, "ASYNCIO",
          "SSL: Unable to handshake, err:%d r:%d errno:%d",
          err, r, errno);

    unsigned long e;
    while((e = ERR_get_error()) != 0) {
      ERR_error_string_n(e, errbuf, sizeof(errbuf));
      TRACE(TRACE_ERROR, "ASYNCIO", "SSL: %s", errbuf);
    }
    af->af_error_callback(af->af_opaque, "SSL Handshake error");
    break;
  }
}

static int
asyncio_ssl_events(asyncio_fd_t *af)
{
  int events = 0;

  if(!af->af_connected)
    return POLLOUT;

  if(af->af_ssl_read_status == SSL_ERROR_WANT_WRITE) {
    events |= POLLOUT;
  } else {
    events |= POLLIN;
    if(af->af_ssl_established) {
      asyncio_ssl_write(af);
    }
  }

  if(af->af_ssl_write_status == SSL_ERROR_WANT_WRITE) {
    events |= POLLOUT;
  } else if(af->af_ssl_write_status == SSL_ERROR_WANT_READ) {
    events |= POLLIN;
  }
  return events;
}


static void
asyncio_ssl_write(asyncio_fd_t *af)
{
  if(!af->af_ssl_established) {
    asyncio_ssl_handshake(af);
    return;
  }

  htsbuf_data_t *hd;
  htsbuf_queue_t *q = &af->af_sendq;
  int len;

  af->af_ssl_write_status = 0;

  while((hd = TAILQ_FIRST(&q->hq_q)) != NULL) {

    len = hd->hd_data_len - hd->hd_data_off;
    assert(len > 0);

    int r = SSL_write(af->af_ssl, hd->hd_data + hd->hd_data_off, len);
    int err = SSL_get_error(af->af_ssl, r);

    switch(err) {
    case SSL_ERROR_NONE:
      hd->hd_data_off += r;

      assert(hd->hd_data_off <= hd->hd_data_len);

      if(hd->hd_data_off == hd->hd_data_len) {
        TAILQ_REMOVE(&q->hq_q, hd, hd_link);
        free(hd->hd_data);
        free(hd);
      }
      continue;

    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      af->af_ssl_write_status = err;
      return;

    default:
      return;
    }
  }
}

/**
 *
 */
static void
asyncio_ssl_read(asyncio_fd_t *af)
{
  if(!af->af_ssl_established) {
    asyncio_ssl_handshake(af);
    return;
  }


  while(af->af_ssl != NULL) {
    char buf[4096];
    if(af->af_ssl_write_status == SSL_ERROR_WANT_READ) {
      return;
    }

    af->af_ssl_read_status = 0;
    int r = SSL_read(af->af_ssl, buf, sizeof(buf));
    int err = SSL_get_error(af->af_ssl, r);
    switch(err) {
    case SSL_ERROR_NONE:
      htsbuf_append(&af->af_recvq, buf, r);
      break;

    default:
      af->af_error_callback(af->af_opaque, "SSL Error");
      return;

    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      af->af_ssl_read_status = err;
      return;
    }
    af->af_read_callback(af->af_opaque, &af->af_recvq);
  }
}


void *
asyncio_ssl_create_server(const char *privkeyfile, const char *certfile)
{
  SSL_CTX *ctx = SSL_CTX_new(TLSv1_server_method());

  int r = SSL_CTX_use_PrivateKey_file(ctx, privkeyfile, SSL_FILETYPE_PEM);
  if(r != 1) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Unable to load private key file %s",
          privkeyfile);
    return NULL;
  }
  r = SSL_CTX_use_certificate_file(ctx, certfile, SSL_FILETYPE_PEM);
  if(r != 1) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Unable to load certificate file %s",
          certfile);
    return NULL;
  }

  r = SSL_CTX_check_private_key(ctx);
  if(r != 1) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Certificate/private key file mismatch");
    return NULL;
  }
  return ctx;
}


void *
asyncio_ssl_create_client(void)
{
  SSL_CTX *ctx = SSL_CTX_new(TLSv1_client_method());

  if(!SSL_CTX_load_verify_locations(ctx, NULL, "/etc/ssl/certs")) {
    return NULL;
  }

  return ctx;
}

void
asyncio_ssl_free(void *ctx)
{
  SSL_CTX_free(ctx);
}

#else


void *
asyncio_ssl_create_server(const char *privkeyfile, const char *certfile)
{
  return NULL;
}

void *
asyncio_ssl_create_client(void)
{
  return NULL;
}

void
asyncio_ssl_free(void *ctx)
{
}

#endif
