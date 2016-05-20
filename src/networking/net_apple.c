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

#include "main.h"
#include <errno.h>
#include "net_i.h"

/**
 *
 */
static int
ssl_read(tcpcon_t *tc, void *buf, size_t len, int all,
	      net_read_cb_t *cb, void *opaque)
{
  int tot = 0;
  size_t ret;

  if(!all) {
    OSStatus err = SSLRead(tc->ssl, buf, len, &ret);
    if(err == noErr)
      return ret;
    return -1;
  }

  while(tot != len) {
    OSStatus err = SSLRead(tc->ssl, buf + tot, len - tot, &ret);
    if(err != noErr)
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
ssl_write(tcpcon_t *tc, const void *data, size_t len)
{
  size_t written;
  OSStatus ret = SSLWrite(tc->ssl, data, len, &written);
  if(ret != noErr || written != len)
    return ECONNRESET;
  return 0;
}



/**
 *
 */
static OSStatus
ssl_read_func(SSLConnectionRef connection, void *data, size_t *len)
{
  tcpcon_t *tc = (tcpcon_t *)connection;
  int ret = tc->raw_read(tc, data, *len, 1, NULL, NULL);
  if(ret < 0)
    return errSSLClosedAbort;
  return noErr;
}



static OSStatus
ssl_write_func(SSLConnectionRef connection, const void *data, size_t *len)
{
  tcpcon_t *tc = (tcpcon_t *)connection;
  if(tc->raw_write(tc, data, *len))
    return errSSLClosedAbort;

  return noErr;
}


/**
 *
 */
int
tcp_ssl_open(tcpcon_t *tc, char *errbuf, size_t errlen, const char *hostname,
             int verify)
{
  tc->ssl = SSLCreateContext(NULL, kSSLClientSide, kSSLStreamType);

  if(tc->ssl == NULL) {
    snprintf(errbuf, errlen, "SSL: Unable to create context");
    return -1;
  }

  tc->raw_read  = tc->read;
  tc->raw_write = tc->write;

  SSLSetIOFuncs(tc->ssl, ssl_read_func, ssl_write_func);
  SSLSetConnection(tc->ssl, tc);

  if(hostname != NULL)
    SSLSetPeerDomainName(tc->ssl, hostname, strlen(hostname));

  OSStatus retValue = SSLHandshake(tc->ssl);

  if(retValue != noErr) {
    snprintf(errbuf, errlen, "SSL: Error 0x%x", (int)retValue);
    return -1;
  }
  tc->read =  ssl_read;
  tc->write = ssl_write;

  return 0;
}


/**
 *
 */
void
tcp_ssl_close(tcpcon_t *tc)
{
  SSLClose(tc->ssl);
  CFRelease(tc->ssl);
  tc->ssl = 0;
}
