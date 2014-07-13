#include <pthread.h>
#include "net_i.h"


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
ssl_read(tcpcon_t *tc, void *buf, size_t len, int all,
	 net_read_cb_t *cb, void *opaque)
{
  int c, tot = 0;
  if(!all) {
    c = SSL_read(tc->ssl, buf, len);
    return c > 0 ? c : -1;
  }

  while(tot != len) {
    c = SSL_read(tc->ssl, buf + tot, len - tot);

    if(c < 1)
      return -1;

    tot += c;

    if(cb != NULL)
      cb(opaque, tot);
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


int
tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen)
{
  if(showtime_ssl_ctx == NULL) {
    snprintf(errbuf, errlen, "SSL not initialized");
    return -1;
  }
  char errmsg[120];

  if((tc->ssl = SSL_new(showtime_ssl_ctx)) == NULL) {
    ERR_error_string(ERR_get_error(), errmsg);
    snprintf(errbuf, errlen, "SSL: %s", errmsg);
    return -1;
  }

  if(SSL_set_fd(tc->ssl, tc->fd) == 0) {
    ERR_error_string(ERR_get_error(), errmsg);
    snprintf(errbuf, errlen, "SSL fd: %s", errmsg);
    return -1;
  }

  if(SSL_connect(tc->ssl) <= 0) {
    ERR_error_string(ERR_get_error(), errmsg);
    snprintf(errbuf, errlen, "SSL connect: %s", errmsg);
    return -1;
  }

  SSL_set_mode(tc->ssl, SSL_MODE_AUTO_RETRY);
  tc->read = ssl_read;
  tc->write = ssl_write;
  return 0;
}


/**
 *
 */
void
tcp_ssl_close(tcpcon_t *tc)
{
  SSL_shutdown(tc->ssl);
  SSL_free(tc->ssl);
}


/**
 *
 */
void
net_ssl_init(void)
{
  SSL_library_init();
  SSL_load_error_strings();
  showtime_ssl_ctx = SSL_CTX_new(SSLv23_client_method());

  int i, n = CRYPTO_num_locks();
  ssl_locks = malloc(sizeof(pthread_mutex_t) * n);
  for(i = 0; i < n; i++)
    pthread_mutex_init(&ssl_locks[i], NULL);
  CRYPTO_set_locking_callback(ssl_lock_fn);
  CRYPTO_set_id_callback(ssl_tid_fn);
}

