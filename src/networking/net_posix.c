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
#include <sys/ioctl.h>
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
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <pthread.h>

#include "showtime.h"
#include "net.h"


#if ENABLE_HTTPSERVER
#include "http_server.h"
#include "ssdp.h"
#endif




#if ENABLE_OPENSSL

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


#if ENABLE_POLARSSL
/**
 *
 */
static int
polarssl_read(tcpcon_t *tc, void *buf, size_t len, int all)
{
  int ret, tot = 0;
  if(!all) {
    ret = ssl_read(tc->ssl, buf, len);
    if(ret >= 0) 
      return ret;
    return -1;
  }

  while(tot != len) {
    ret = ssl_read(tc->ssl, buf + tot, len - tot);
    if(ret < 0) 
      return -1;
    tot += ret;
  }
  return tot;
}


/**
 *
 */
static int
polarssl_write(tcpcon_t *tc, const void *data, size_t len)
{
  return ssl_write(tc->ssl, data, len) != len ? ECONNRESET : 0;
}


#endif

/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
#ifdef MSG_NOSIGNAL
  return send(tc->fd, data, len, MSG_NOSIGNAL) != len ? ECONNRESET : 0;
#else
  return send(tc->fd, data, len, 0           ) != len ? ECONNRESET : 0;
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

    x = recv(tc->fd, buf + off, len - off, all ? MSG_WAITALL : 0);
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
tcp_connect(const char *hostname, int port, char *errbuf, size_t errbufsize,
	    int timeout, int ssl)
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
      return NULL;

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
      return NULL;
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
	return NULL;
      }
      
      if(r == -1) {
	snprintf(errbuf, errbufsize, "poll() error: %s", strerror(errno));
	close(fd);
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
    close(fd);
    return NULL;
  }
  
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

  val = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val));

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = fd;
  htsbuf_queue_init(&tc->spill, 0);
  

  if(ssl) {
#if ENABLE_OPENSSL
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
#elif ENABLE_POLARSSL
    if(1) {
      tc->ssl = malloc(sizeof(ssl_context));
      if(ssl_init(tc->ssl)) {
	snprintf(errbuf, errlen, "SSL failed to initialize");
	close(fd);
	free(tc->ssl);
	free(tc);
	return NULL;
      }

      tc->ssn = malloc(sizeof(ssl_session));
      tc->hs = malloc(sizeof(havege_state));

      havege_init(tc->hs);
      memset(tc->ssn, 0, sizeof(ssl_session));


      ssl_set_endpoint(tc->ssl, SSL_IS_CLIENT );
      ssl_set_authmode(tc->ssl, SSL_VERIFY_NONE );

      ssl_set_rng(tc->ssl, havege_rand, tc->hs );
      ssl_set_bio(tc->ssl, net_recv, &tc->fd, net_send, &tc->fd);
      ssl_set_ciphers(tc->ssl, ssl_default_ciphers );
      ssl_set_session(tc->ssl, 1, 600, tc->ssn );
      
      tc->read = polarssl_read;
      tc->write = polarssl_write;
      
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
#if ENABLE_OPENSSL
  if(tc->ssl != NULL) {
    SSL_shutdown(tc->ssl);
    SSL_free(tc->ssl);
  }
#endif
#if ENABLE_POLARSSL
  if(tc->ssl != NULL) {
    ssl_close_notify(tc->ssl);
    ssl_free(tc->ssl);

    free(tc->ssl);
    free(tc->ssn);
    free(tc->hs);
  }
#endif
  close(tc->fd);
  htsbuf_queue_flush(&tc->spill);
  free(tc);
}

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
  if(setsockopt(tc->fd, SOL_SOCKET, SO_RCVBUF, &v, sizeof(v)) == -1)
    TRACE(TRACE_ERROR, "TCP", "Unable to increase RCVBUF");
}

/**
 *
 */
netif_t *
net_get_interfaces(void)
{
  struct ifaddrs *ifa_list, *ifa;
  struct netif *ni, *n;
  int num = 0;

  if(getifaddrs(&ifa_list) != 0) {
    TRACE(TRACE_ERROR, "net", "getifaddrs failed: %s", strerror(errno));
    return NULL;
  }
  
  for(ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next)
    num++;

  n = ni = calloc(1, sizeof(struct netif) * (num + 1));
  
  for(ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
    if((ifa->ifa_flags & (IFF_UP | IFF_LOOPBACK)) != IFF_UP ||
       ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
	 continue;

    n->ipv4 = ntohl(((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr);
    if(n->ipv4 == 0)
      continue;

    snprintf(n->ifname, sizeof(n->ifname), "%s", ifa->ifa_name);
    n++;
  }
  
  freeifaddrs (ifa_list);

  return ni;
}


/**
 * Called from code in arch/
 */
void
net_initialize(void)
{
#if ENABLE_OPENSSL

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
