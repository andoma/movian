#include <errno.h>
#include "net_i.h"

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
  if(ret)
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


/**
 *
 */
int
tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen)
{
  tc->ssl = malloc(sizeof(ssl_context));
  if(ssl_init(tc->ssl)) {
    snprintf(errbuf, errlen, "SSL failed to initialize");
    return -1;
  }

  tc->raw_read  = tc->read;
  tc->raw_write = tc->write;

  tc->ssn = malloc(sizeof(ssl_session));
  tc->hs = malloc(sizeof(havege_state));

  havege_init(tc->hs);
  memset(tc->ssn, 0, sizeof(ssl_session));

  ssl_set_endpoint(tc->ssl, SSL_IS_CLIENT);
  ssl_set_authmode(tc->ssl, SSL_VERIFY_NONE);

  ssl_set_rng(tc->ssl, havege_random, tc->hs);
  ssl_set_bio(tc->ssl, raw_recv, tc, raw_send, tc);
  ssl_set_ciphersuites(tc->ssl, ssl_default_ciphersuites);
  ssl_set_session(tc->ssl, tc->ssn );

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
  free(tc->ssn);
  free(tc->hs);
}
