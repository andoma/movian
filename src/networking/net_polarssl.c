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
int
tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen)
{
  tc->ssl = malloc(sizeof(ssl_context));
  if(ssl_init(tc->ssl)) {
    snprintf(errbuf, errlen, "SSL failed to initialize");
    return -1;
  }

  tc->ssn = malloc(sizeof(ssl_session));
  tc->hs = malloc(sizeof(havege_state));

  havege_init(tc->hs);
  memset(tc->ssn, 0, sizeof(ssl_session));

  ssl_set_endpoint(tc->ssl, SSL_IS_CLIENT );
  ssl_set_authmode(tc->ssl, SSL_VERIFY_NONE );

  ssl_set_rng(tc->ssl, havege_random, tc->hs );
  ssl_set_bio(tc->ssl, net_recv, &tc->fd, net_send, &tc->fd);
  ssl_set_ciphersuites(tc->ssl, ssl_default_ciphersuites );
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


void
net_ssl_init(void)
{
}
