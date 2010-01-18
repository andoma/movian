/*
 *  Networking under POSIX
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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "net.h"




static int
getstreamsocket(int family, char *errbuf, size_t errbufsize)
{
  int fd;
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
  int val = 1;
  if(setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &val, sizeof(val)) == -1) {
    snprintf(errbuf, errbufsize, "setsockopt SO_NOSIGPIPE error: %s",
	     strerror(errno));
    close(fd);
    return -1; 
  } 
#endif
  return fd;
}

/**
 *
 */
int
tcp_connect(const char *hostname, int port, char *errbuf, size_t errbufsize,
	    int timeout)
{
  struct hostent *hp;
  char *tmphstbuf;
  int fd, val, r, err, herr;
  const char *errtxt;
#if !defined(__APPLE__)
  struct hostent hostbuf;
  size_t hstbuflen;
  int res;
#endif
  struct sockaddr_in6 in6;
  struct sockaddr_in in;
  socklen_t errlen = sizeof(int);


  if(!strcmp(hostname, "localhost")) {
    if((fd = getstreamsocket(AF_INET, errbuf, errbufsize)) == -1)
      return -1;  

    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    r = connect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in));
  } else {

#if defined(__APPLE__)
    herr = 0;
    tmphstbuf = NULL; /* free NULL is a nop */
    /* TODO: AF_INET6 */
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
	errtxt = "Unknown host";
	break;

      case NO_ADDRESS:
	errtxt = "The requested name is valid but does not have an IP address";
	break;
      
      case NO_RECOVERY:
	errtxt = "A non-recoverable name server error occurred";
	break;
      
      case TRY_AGAIN:
	errtxt = "A temporary error occurred on an authoritative name server";
	break;
      
      default:
	errtxt = "Unknown error";
	break;
      }

      snprintf(errbuf, errbufsize, "%s", errtxt);
      free(tmphstbuf);
      return -1;
    } else if(hp == NULL) {
      snprintf(errbuf, errbufsize, "Resolver internal error");
      free(tmphstbuf);
      return -1;
    }

    if((fd = getstreamsocket(hp->h_addrtype, errbuf, errbufsize)) == -1) {
      free(tmphstbuf);
      return -1;  
    }

    switch(hp->h_addrtype) {
    case AF_INET:
      memset(&in, 0, sizeof(in));
      in.sin_family = AF_INET;
      in.sin_port = htons(port);
      memcpy(&in.sin_addr, hp->h_addr_list[0], sizeof(struct in_addr));
      r = connect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in));
      break;

    case AF_INET6:
      memset(&in6, 0, sizeof(in6));
      in6.sin6_family = AF_INET6;
      in6.sin6_port = htons(port);
      memcpy(&in6.sin6_addr, hp->h_addr_list[0], sizeof(struct in6_addr));
      r = connect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in6));
      break;

    default:
      snprintf(errbuf, errbufsize, "Invalid protocol family");
      free(tmphstbuf);
      return -1;
    }

    free(tmphstbuf);
  }
  if(r == -1) {
    if(errno == EINPROGRESS) {
      struct pollfd pfd;

      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;

      r = poll(&pfd, 1, timeout);
      if(r == 0) {
	/* Timeout */
	snprintf(errbuf, errbufsize, "Connection attempt timed out");
	close(fd);
	return -1;
      }
      
      if(r == -1) {
	snprintf(errbuf, errbufsize, "poll() error: %s", strerror(errno));
	close(fd);
	return -1;
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
    close(fd);
    return -1;
  }
  
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  val = 1;
#ifdef __APPLE__
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));
#else
  setsockopt(fd, SOL_TCP, TCP_NODELAY, &val, sizeof(val));
#endif

  return fd;
}

/**
 *
 */
int
tcp_write(int fd, const void *data, size_t len)
{
#ifdef MSG_NOSIGNAL
  return send(fd, data, len, MSG_NOSIGNAL) != len ? ECONNRESET : 0;
#else
  return send(fd, data, len, 0           ) != len ? ECONNRESET : 0;
#endif
}


/**
 *
 */
int
tcp_read(int fd, void *buf, size_t len, int all)
{
  int x = recv(fd, buf, len, all ? MSG_WAITALL : 0);
  if(all && x != len)
    return -1;

  return x < 1 ? -1 : x;
}


#if 0
/**
 *
 */
int
tcp_read_timeout(int fd, void *buf, size_t len, int timeout)
{
  int x, tot = 0;
  struct pollfd fds;

  assert(timeout > 0);

  fds.fd = fd;
  fds.events = POLLIN;
  fds.revents = 0;

  while(tot != len) {

    x = poll(&fds, 1, timeout);
    if(x == 0)
      return ETIMEDOUT;

    x = recv(fd, buf + tot, len - tot, MSG_DONTWAIT);
    if(x == -1) {
      if(errno == EAGAIN)
	continue;
      return errno;
    }

    if(x == 0)
      return ECONNRESET;

    tot += x;
  }
  return 0;
}
#endif


/**
 *
 */
void
tcp_close(int fd)
{
  close(fd);
}
