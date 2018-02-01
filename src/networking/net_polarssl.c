/*
 *  Copyright (C) 2007-2018 Lonelycoder AB
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

#include "main.h"
#include <errno.h>
#include "net_i.h"
#include "polarssl/ctr_drbg.h"
#include "polarssl/entropy.h"
#include "polarssl/error.h"

/**
 *
 */
static int
polarssl_read(tcpcon_t *tc, void *buf, size_t len, int all,
	      net_read_cb_t *cb, void *opaque)
{
  int ret, tot = 0;
  if(!all) {
    ret = ssl_read(tc->ssl, buf, len);
    return ret > 0 ? ret : -1;
  }

  while(tot != len) {
    ret = ssl_read(tc->ssl, buf + tot, len - tot);
    if(ret < 0) 
      return -1;
    tot += ret;
    if(cb != NULL)
      cb(opaque, tot);
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


/**
 *
 */
static int
raw_recv(void *ctx, unsigned char *buf, size_t len)
{
  tcpcon_t *tc = ctx;
  int ret = tc->raw_read(tc, buf, len, 0, NULL, NULL);
  if(ret == -1)
    return POLARSSL_ERR_NET_CONN_RESET;

  return ret;
}


/**
 *
 */
static int
raw_send( void *ctx, const unsigned char *buf, size_t len)
{
  tcpcon_t *tc = ctx;
  if(tc->raw_write(tc, buf, len))
    return POLARSSL_ERR_NET_CONN_RESET;

  return len;
}
static void
printdbg(void *aux, int level, const char *txt)
{
  fprintf(stdout, "%s", txt);
  fflush(stdout);
}

/**
 *
 */
int
tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen, const char *hostname,
             int verify)
{
  int ret;
  entropy_context entropy;
  entropy_init(&entropy);

  tc->rndstate = malloc(sizeof(ctr_drbg_context));

  if((ret = ctr_drbg_init(tc->rndstate, entropy_func, &entropy,
                          (const uint8_t *)gconf.device_id,
                          sizeof(gconf.device_id))) != 0) {
    polarssl_strerror(ret, errbuf, errlen);
    entropy_free(&entropy);
    return -1;
  }

  entropy_free(&entropy);

  tc->ssl = malloc(sizeof(ssl_context));
  if((ret = ssl_init(tc->ssl)) != 0) {
    polarssl_strerror(ret, errbuf, errlen);
    return -1;
  }

  ssl_set_dbg(tc->ssl, printdbg, NULL);

  tc->raw_read  = tc->read;
  tc->raw_write = tc->write;

  ssl_set_endpoint(tc->ssl, SSL_IS_CLIENT);
  ssl_set_authmode(tc->ssl, SSL_VERIFY_NONE);
  if(hostname != NULL)
    ssl_set_hostname(tc->ssl, hostname);

  ssl_set_min_version(tc->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_0); // adds support for SSLv3
  ssl_set_max_version(tc->ssl, SSL_MAJOR_VERSION_3, SSL_MINOR_VERSION_3); /* added TLSv1.2 */
  ssl_set_arc4_support(tc->ssl, SSL_ARC4_DISABLED );

  ssl_set_rng(tc->ssl, ctr_drbg_random, tc->rndstate);

  ssl_set_bio(tc->ssl, raw_recv, tc, raw_send, tc);

  while((ret = ssl_handshake(tc->ssl)) != 0) {
    if(ret != POLARSSL_ERR_NET_WANT_READ &&
       ret != POLARSSL_ERR_NET_WANT_WRITE) {
      polarssl_strerror(ret, errbuf, errlen);
      return -1;
    }
  }

  tc->read = polarssl_read;
  tc->write = polarssl_write;

  return 0;
}



void
tcp_ssl_close(tcpcon_t *tc)
{
  ssl_close_notify(tc->ssl);
  ssl_free(tc->ssl);

  free(tc->ssl);
  free(tc->rndstate);
}
