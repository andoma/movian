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
#include "config.h"
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "main.h"
#include "net_i.h"

/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
  while(1) {
    int r;
#ifdef MSG_NOSIGNAL
    r = send(tc->fd, data, len, MSG_NOSIGNAL);
#else
    r = send(tc->fd, data, len, 0);
#endif
    if(r == -1 && (errno == EINTR))
      continue;

    return r != len ? ECONNRESET : 0;
  }
}


/**
 *
 */
static int
tcp_read(tcpcon_t *tc, void *buf, size_t len, int all,
	 net_read_cb_t *cb, void *opaque)
{
  int x;
  size_t off = 0;
  const int flags = cb == NULL && all ? MSG_WAITALL : 0;

  while(1) {

    x = recv(tc->fd, buf + off, len - off, flags);
    if(x <= 0) {
      if(errno == EINTR)
        continue;
      return -1;
    }
    if(all) {

      off += x;
      if(off == len)
	return len;

      if(cb != NULL)
	cb(opaque, off);

    } else {
      return x < 1 ? -1 : x;
    }
  }
}


/**
 *
 */
static int
getstreamsocket(int family, char *errbuf, size_t errbufsize)
{
  int fd;
  int val = 1;

  fd = socket(family, SOCK_STREAM, 0);
  if(fd == -1) {
    snprintf(errbuf, errbufsize, "Unable to create socket: %s",
	     strerror(errno));
    return -1;
  }

  /**
   * Switch to nonblocking
   */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  /* Darwin send() does not have MSG_NOSIGNAL, but has SO_NOSIGPIPE sockopt */
#ifdef SO_NOSIGPIPE
  if(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val)) == -1) {
    snprintf(errbuf, errbufsize, "setsockopt SO_NOSIGPIPE error: %s",
	     strerror(errno));
    close(fd);
    return -1; 
  } 
#endif

  if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) < 0)
    TRACE(TRACE_INFO, "TCP", "Unable to turn on TCP_NODELAY");

  return fd;
}


/**
 *
 */
tcpcon_t *
tcp_from_fd(int fd)
{
  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd;
  htsbuf_queue_init(&tc->spill, 0);
  tc->read = tcp_read;
  tc->write = tcp_write;
  return tc;
}


/**
 *
 */
int
net_resolve(const char *hostname, net_addr_t *addr, const char **err)
{
  struct hostent *hp;
  char *tmphstbuf;
  int herr;
#if !defined(__APPLE__)
  struct hostent hostbuf;
  size_t hstbuflen;
  int res;
#endif

#if defined(__APPLE__)
  herr = 0;
  tmphstbuf = NULL; /* free NULL is a nop */
  hp = gethostbyname(hostname);
  if(hp == NULL)
    herr = h_errno;
#else
  hstbuflen = 1024;
  tmphstbuf = malloc(hstbuflen);

  while((res = gethostbyname_r(hostname, &hostbuf, tmphstbuf, hstbuflen,
                               &hp, &herr)) == ERANGE) {
    hstbuflen *= 2;
    tmphstbuf = realloc(tmphstbuf, hstbuflen);
  }
#endif

  if(herr != 0) {
    switch(herr) {
    case HOST_NOT_FOUND:
      *err = "Unknown host";
      break;

    case NO_ADDRESS:
      *err = "The requested name is valid but does not have an IP address";
      break;

    case NO_RECOVERY:
      *err = "A non-recoverable name server error occurred";
      break;

    case TRY_AGAIN:
      *err = "A temporary error occurred on an authoritative name server";
      break;

    default:
      *err = "Unknown error";
      break;
    }

    free(tmphstbuf);
    return -1;

  } else if(hp == NULL) {
    *err = "Resolver internal error";
    free(tmphstbuf);
    return -1;
  }

  memset(addr, 0, sizeof(net_addr_t));

  switch(hp->h_addrtype) {
  case AF_INET:
    addr->na_family = 4;
    memcpy(addr->na_addr, hp->h_addr_list[0], sizeof(struct in_addr));
    break;

  case AF_INET6:
    addr->na_family = 6;
    memcpy(addr->na_addr, hp->h_addr_list[0], sizeof(struct in6_addr));
    break;

  default:
    *err = "Invalid protocol family";
    free(tmphstbuf);
    return -1;
  }

  free(tmphstbuf);
  return 0;
}



/**
 *
 */
tcpcon_t *
tcp_connect_arch(const net_addr_t *addr,
                 char *errbuf, size_t errbufsize,
                 int timeout, cancellable_t *c, int dbg)
{
  int fd, r, err;

  union {
    struct sockaddr_storage ss;
    struct sockaddr_in in;
    struct sockaddr_in6 in6;
  } su;

  socklen_t errlen = sizeof(int);
  socklen_t slen;

  memset(&su, 0, sizeof(su));

  switch(addr->na_family) {
  case 4:
    su.in.sin_family = AF_INET;
    su.in.sin_port = htons(addr->na_port);
    memcpy(&su.in.sin_addr, addr->na_addr, sizeof(struct in_addr));
    slen = sizeof(struct sockaddr_in);
    break;

  case AF_INET6:
    su.in6.sin6_family = AF_INET6;
    su.in6.sin6_port = htons(addr->na_port);
    memcpy(&su.in6.sin6_addr, addr->na_addr, sizeof(struct in6_addr));
    slen = sizeof(struct sockaddr_in6);
    break;

  default:
    snprintf(errbuf, errbufsize, "Invalid protocol family");
    return NULL;
  }

  if((fd = getstreamsocket(su.ss.ss_family, errbuf, errbufsize)) == -1)
    return NULL;

  r = connect(fd, (struct sockaddr *)&su, slen);

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd;
  htsbuf_queue_init(&tc->spill, 0);
  tcp_set_cancellable(tc, c);

  if(r == -1) {
    if(errno == EINPROGRESS) {
      struct pollfd pfd;

      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;

      r = poll(&pfd, 1, timeout);
      if(r < 1) {

	/* Timeout */
        if(!r)
          snprintf(errbuf, errbufsize, "Connection attempt timed out");
        else
          snprintf(errbuf, errbufsize, "poll() error: %s", strerror(errno));

        tcp_close(tc);
	return NULL;
      }

      getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen);
    } else {
      err = errno;
    }
  } else {
    err = 0;
  }

  if(err != 0) {
    snprintf(errbuf, errbufsize, "%s", strerror(err));
    tcp_close(tc);
    return NULL;
  }

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  net_change_ndelay(fd, 1);
  tc->read = tcp_read;
  tc->write = tcp_write;
  return tc;
}


/**
 *
 */
void
tcp_close_arch(tcpcon_t *tc)
{
  close(tc->fd);
}


/**
 *
 */
void
tcp_shutdown(tcpcon_t *tc)
{
  shutdown(tc->fd, SHUT_RDWR);
}


/**
 *
 */
void
tcp_huge_buffer(tcpcon_t *tc)
{
  int v = 192 * 1024;
  setsockopt(tc->fd, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v));
}


/**
 *
 */
void
tcp_set_read_timeout(tcpcon_t *tc, int ms)
{
  struct timeval tv;
  tv.tv_sec  = ms / 1000;
  tv.tv_usec = (ms % 1000) * 1000;
  setsockopt(tc->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}


/**
 *
 */
void
net_change_nonblocking(int fd, int on)
{
  int flags = fcntl(fd, F_GETFL);
  if(on) {
    flags |= O_NONBLOCK;
  } else {
    flags &= ~O_NONBLOCK;
  }
  fcntl(fd, F_SETFL, flags);
}


/**
 *
 */
void
net_change_ndelay(int fd, int on)
{
  int val = on;
  if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)))
    TRACE(TRACE_ERROR, "NET", "Unable to set ndelay on %x", fd);
}


/**
 *
 */
int
net_resolve_numeric(const char *str, net_addr_t *addr)
{
  in_addr_t ia = inet_addr(str);
  if(ia == INADDR_NONE)
    return 1;

  memset(addr, 0, sizeof(net_addr_t));
  addr->na_family = 4;
  memcpy(&addr->na_addr, &ia, 4);
  return 0;
}
