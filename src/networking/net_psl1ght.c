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

#include "config.h"
#include <netdb.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/net.h>
#include <errno.h>

#include "showtime.h"
#include "net.h"


#if ENABLE_HTTPSERVER
#include "http_server.h"
#include "ssdp.h"
#endif




#if ENABLE_SSL

static SSL_CTX *showtime_ssl_ctx;
static pthread_mutex_t *ssl_locks;

static unsigned long
ssl_tid_fn(void)
{
  return (unsigned long)pthread_self();
}

static void
ssl_lock_fn(int mode, int n, const char *file, int line)
{
  if(mode & CRYPTO_LOCK)
    pthread_mutex_lock(&ssl_locks[n]);
  else
    pthread_mutex_unlock(&ssl_locks[n]);
}



/**
 *
 */
static int
ssl_read(tcpcon_t *tc, void *buf, size_t len, int all)
{
  int c, tot = 0;
  if(!all)
    return SSL_read(tc->ssl, buf, len);

  while(tot != len) {
    c = SSL_read(tc->ssl, buf + tot, len - tot);

    if(c < 1)
      return -1;

    tot += c;
  }
  return tot;
}


/**
 *
 */
static int
ssl_write(tcpcon_t *tc, const void *data, size_t len)
{
  return SSL_write(tc->ssl, data, len) != len ? ECONNRESET : 0;
}

#endif


/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
#ifdef MSG_NOSIGNAL
  return netSend(tc->fd, data, len, MSG_NOSIGNAL) != len ? ECONNRESET : 0;
#else
  return netSend(tc->fd, data, len, 0           ) != len ? ECONNRESET : 0;
#endif
}


/**
 *
 */
static int
tcp_read(tcpcon_t *tc, void *buf, size_t len, int all)
{
  int x;
  size_t off = 0;
  while(1) {

    x = netRecv(tc->fd, buf + off, len - off, all ? MSG_WAITALL : 0);
    if(x <= 0)
      return -1;
    
    if(all) {

      off += x;
      if(off == len)
	return len;

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
  int fd, optval;

  fd = netSocket(family, SOCK_STREAM, IPPROTO_TCP);
  if(fd < 0) {
    snprintf(errbuf, errbufsize, "Unable to create socket: %s",
	     strerror(net_errno));
    return -1;  
  }

  /**
   * Switch to nonblocking
   */
  optval = 1;
  int r = netSetSockOpt(fd, SOL_SOCKET, SO_NBIO, &optval, sizeof(optval));
  if(r < 0) {
    snprintf(errbuf, errbufsize, "Unable to go nonblocking: %s",
	     strerror(net_errno));
    netClose(fd);
    return -1;
  }

  return fd;
}

/**
 *
 */
tcpcon_t *
tcp_connect(const char *hostname, int port, char *errbuf, size_t errbufsize,
	    int timeout, int ssl)
{
  struct net_hostent *hp;
  char *tmphstbuf;
  int fd, r, err, herr, optval;
  const char *errtxt;
  struct sockaddr_in in;
  socklen_t errlen = sizeof(int);


  if(!strcmp(hostname, "localhost")) {
    if((fd = getstreamsocket(AF_INET, errbuf, errbufsize)) == -1)
      return NULL;

    memset(&in, 0, sizeof(in));
    in.sin_family = AF_INET;
    in.sin_port = htons(port);
    in.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    r = netConnect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in));
  } else {

    herr = 0;
    tmphstbuf = NULL; /* free NULL is a nop */
    hp = netGetHostByName(hostname);
    if(hp == NULL)
      herr = h_errno;

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
      return NULL;
    } else if(hp == NULL) {
      snprintf(errbuf, errbufsize, "Resolver internal error");
      free(tmphstbuf);
      return NULL;
    }

    if((fd = getstreamsocket(hp->h_addrtype, errbuf, errbufsize)) == -1) {
      free(tmphstbuf);
      return NULL;
    }

    switch(hp->h_addrtype) {
    case AF_INET:
      memset(&in, 0, sizeof(in));
      in.sin_family = AF_INET;
      in.sin_port = htons(port);
      lv2_void* netaddrlist = (lv2_void*)(u64)hp->h_addr_list;
      memcpy(&in.sin_addr, (char*)(u64)netaddrlist[0], sizeof(struct in_addr));
      r = netConnect(fd, (struct sockaddr *)&in, sizeof(struct sockaddr_in));
      break;

    default:
      snprintf(errbuf, errbufsize, "Invalid protocol family");
      free(tmphstbuf);
      return NULL;
    }

    free(tmphstbuf);
  }

  if(r < 0) {
    if(net_errno == NET_EINPROGRESS) {

      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLOUT;
      pfd.revents = 0;

      r = netPoll(&pfd, 1, timeout);
      if(r == 0) {
	/* Timeout */
	snprintf(errbuf, errbufsize, "Connection attempt timed out");
	netClose(fd);
	return NULL;
      }
      
      if(r == -1) {
	snprintf(errbuf, errbufsize, "poll() error: %s", 
		 strerror(net_errno));
	netClose(fd);
	return NULL;
      }

      netGetSockOpt(fd, SOL_SOCKET, SO_ERROR, (void *)&err, &errlen);
    } else {
      err = net_errno;
    }
  } else {
    err = 0;
  }

  if(err != 0) {
    snprintf(errbuf, errbufsize, "%s", strerror(err));
    netClose(fd);
    return NULL;
  }
  
  optval = 0;
  r = netSetSockOpt(fd, SOL_SOCKET, SO_NBIO, &optval, sizeof(optval));
  if(r < 0) {
    snprintf(errbuf, errbufsize, "Unable to go blocking: %s",
	     strerror(net_errno));
    netClose(fd);
    return NULL;
  }

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd;


  if(ssl) {
#if ENABLE_SSL
    if(showtime_ssl_ctx != NULL) {
      char errmsg[120];

      if((tc->ssl = SSL_new(showtime_ssl_ctx)) == NULL) {
	ERR_error_string(ERR_get_error(), errmsg);
	snprintf(errbuf, errlen, "SSL: %s", errmsg);
	tcp_close(tc);
	return NULL;
      }
      if(SSL_set_fd(tc->ssl, tc->fd) == 0) {
	ERR_error_string(ERR_get_error(), errmsg);
	snprintf(errbuf, errlen, "SSL fd: %s", errmsg);
	tcp_close(tc);
	return NULL;
      }

      if(SSL_connect(tc->ssl) <= 0) {
	ERR_error_string(ERR_get_error(), errmsg);
	snprintf(errbuf, errlen, "SSL connect: %s", errmsg);
	tcp_close(tc);
	return NULL;
      }

      SSL_set_mode(tc->ssl, SSL_MODE_AUTO_RETRY);
      tc->read = ssl_read;
      tc->write = ssl_write;
    } else 
#endif
    {

      snprintf(errbuf, errlen, "SSL not supported");
      tcp_close(tc);
      return NULL;
    }
  } else {
    tc->read = tcp_read;
    tc->write = tcp_write;
  }

  return tc;
}


/**
 *
 */
void
tcp_close(tcpcon_t *tc)
{
#if ENABLE_SSL
  if(tc->ssl != NULL) {
    SSL_shutdown(tc->ssl);
    SSL_free(tc->ssl);
  }
#endif
  netClose(tc->fd);
  free(tc);
}


/**
 *
 */
netif_t *
net_get_interfaces(void)
{
  return NULL;
}


/**
 * Called from code in arch/
 */
void
net_initialize(void)
{
#if ENABLE_SSL

  SSL_library_init();
  SSL_load_error_strings();
  showtime_ssl_ctx = SSL_CTX_new(SSLv23_client_method());
  
  int i, n = CRYPTO_num_locks();
  ssl_locks = malloc(sizeof(pthread_mutex_t) * n);
  for(i = 0; i < n; i++)
    pthread_mutex_init(&ssl_locks[i], NULL);
  
  CRYPTO_set_locking_callback(ssl_lock_fn);
  CRYPTO_set_id_callback(ssl_tid_fn);
#endif
}
