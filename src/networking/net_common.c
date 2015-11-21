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
#include <stdio.h>
#include <stdlib.h>

#include "misc/minmax.h"
#include "misc/bytestream.h"
#include "net_i.h"

#include "main.h"
#include "fileaccess/smb/nmb.h"

#include "prop/prop.h"

/**
 *
 */
int
tcp_write_queue(tcpcon_t *tc, htsbuf_queue_t *q)
{
  htsbuf_data_t *hd;
  int l, r = 0;

  while((hd = TAILQ_FIRST(&q->hq_q)) != NULL) {
    TAILQ_REMOVE(&q->hq_q, hd, hd_link);

    l = hd->hd_data_len - hd->hd_data_off;
    r |= tc->write(tc, hd->hd_data + hd->hd_data_off, l);
    free(hd->hd_data);
    free(hd);
  }
  q->hq_size = 0;
  return 0;
}


/**
 *
 */
int
tcp_write_queue_dontfree(tcpcon_t *tc, htsbuf_queue_t *q)
{
  htsbuf_data_t *hd;
  int l, r = 0;

  TAILQ_FOREACH(hd, &q->hq_q, hd_link) {
    l = hd->hd_data_len - hd->hd_data_off;
    r |= tc->write(tc, hd->hd_data + hd->hd_data_off, l);
  }
  return 0;
}


/**
 *
 */
void
tcp_printf(tcpcon_t *tc, const char *fmt, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  tc->write(tc, buf, len);
}


/**
 *
 */
static int
tcp_read_into_spill(tcpcon_t *tc)
{
  htsbuf_queue_t *hq = &tc->spill;
  htsbuf_data_t *hd = TAILQ_LAST(&hq->hq_q, htsbuf_data_queue);
  int c;

  if(hd != NULL) {
    /* Fill out any previous buffer */
    c = hd->hd_data_size - hd->hd_data_len;

    if(c > 0) {

      if((c = tc->read(tc, hd->hd_data + hd->hd_data_len, c, 0, NULL, 0)) < 0)
	return -1;

      hd->hd_data_len += c;
      hq->hq_size += c;
      return 0;
    }
  }
  
  hd = malloc(sizeof(htsbuf_data_t));
  
  hd->hd_data_size = 1000;
  hd->hd_data = malloc(hd->hd_data_size);

  if((c = tc->read(tc, hd->hd_data, hd->hd_data_size, 0, NULL, 0)) < 0) {
    free(hd->hd_data);
    free(hd);
    return -1;
  }
  hd->hd_data_len = c;
  hd->hd_data_off = 0;
  TAILQ_INSERT_TAIL(&hq->hq_q, hd, hd_link);
  hq->hq_size += c;
  return 0;
}


/**
 *
 */
int
tcp_read_line(tcpcon_t *tc, char *buf,const size_t bufsize)
{
  int len;

  while(1) {
    len = htsbuf_find(&tc->spill, 0xa);

    if(len == -1) {
      if(tcp_read_into_spill(tc) < 0)
	return -1;
      continue;
    }
    
    if(len >= bufsize - 1)
      return -1;

    htsbuf_read(&tc->spill, buf, len);
    buf[len] = 0;
    while(len > 0 && buf[len - 1] < 32)
      buf[--len] = 0;
    htsbuf_drop(&tc->spill, 1); /* Drop the \n */
    return 0;
  }
}


/**
 *
 */
int
tcp_read_data(tcpcon_t *tc, void *buf, size_t bufsize,
	      net_read_cb_t *cb, void *opaque)
{
  int r = buf ? htsbuf_read(&tc->spill, buf, bufsize) :
    htsbuf_drop(&tc->spill, bufsize);
  if(r == bufsize)
    return 0;

  if(buf != NULL)
    return tc->read(tc, buf + r, bufsize - r, 1, cb, opaque) < 0 ? -1 : 0;

  size_t remain = bufsize - r;

  buf = malloc(5000);

  while(remain > 0) {
    size_t n = MIN(remain, 5000);
    r = tc->read(tc, buf, n, 1, NULL, 0) < 0 ? -1 : 0;
    if(r != 0)
      break;
    remain -= n;
  }

  free(buf);
  return r;
}


/**
 *
 */
int
tcp_read_to_eof(tcpcon_t *tc, void *buf, size_t bufsize,
                net_read_cb_t *cb, void *opaque)
{
  int r = htsbuf_read(&tc->spill, buf, bufsize);
  if(r == bufsize)
    return bufsize;

  int x = tc->read(tc, buf + r, bufsize - r, 0, cb, opaque);
  if(x < 0) {
    if(r)
      return r;
    return x;
  }
  return r + x;
}


/**
 *
 */
int
tcp_read_data_nowait(tcpcon_t *tc, char *buf, const size_t bufsize)
{
  int tot = htsbuf_read(&tc->spill, buf, bufsize);

  if(tot > 0)
    return tot;

  return tc->read(tc, buf + tot, bufsize - tot, 0, NULL, NULL);
}


/**
 *
 */
void
net_fmt_host(char *dst, size_t dstlen, const net_addr_t *na)
{
  switch(na->na_family) {
  case 4:
    if(na->na_port != 0)
      snprintf(dst, dstlen, "%d.%d.%d.%d:%d",
               na->na_addr[0], na->na_addr[1], na->na_addr[2],  na->na_addr[3],
               na->na_port);
    else
      snprintf(dst, dstlen, "%d.%d.%d.%d",
               na->na_addr[0], na->na_addr[1], na->na_addr[2],  na->na_addr[3]);

    break;

  default:
    snprintf(dst, dstlen, "family-%d",na->na_family);
    break;
  }
}



/**
 *
 */
const char *
net_addr_str(const net_addr_t *na)
{
  static __thread char buf[64];
  net_fmt_host(buf, sizeof(buf), na);
  return buf;
}


/**
 *
 */
int
net_addr_cmp(const net_addr_t *a, const net_addr_t *b)
{
  if(a->na_family != b->na_family)
    return 1;
  if(a->na_port != b->na_port)
    return 1;
  if(a->na_family == 4)
    return memcmp(a->na_addr, b->na_addr, 4);

  return 1;
}



/**
 *
 */
int
tcp_write_data(tcpcon_t *tc, const void *buf, const size_t bufsize)
{
  return tc->write(tc, buf, bufsize);
}


/**
 *
 */
int
tcp_get_fd(const tcpcon_t *tc)
{
  return tc->fd;
}


/**
 *
 */
void
tcp_cancel(void *aux)
{
  tcpcon_t *tc = aux;
  tcp_shutdown(tc);
}


/**
 *
 */
void
tcp_set_cancellable(tcpcon_t *tc, struct cancellable *c)
{
  if(tc->cancellable == c)
    return;

  if(tc->cancellable != NULL) {
    cancellable_unbind(tc->cancellable, tc);
    tc->cancellable = NULL;
  }

  if(c != NULL)
    tc->cancellable = cancellable_bind(c, tcp_cancel, tc);
}


/**
 *
 */
static int
socks_session_setup(tcpcon_t *tc, const char *hostname, int port,
                    char *errbuf, size_t errlen)
{
  uint8_t buf[300];
  int hostnamelen = strlen(hostname);
  if(hostnamelen >= 255) {
    snprintf(errbuf, errlen, "SOCK5 too long hostname");
    return -1;
  }

  const uint8_t hello[3] = {5,1,0};
  tcp_write_data(tc, hello, 3);

  if(tcp_read_data(tc, buf, 2, NULL, NULL)) {
    snprintf(errbuf, errlen, "SOCK5 Read error");
    return -1;
  }

  if(buf[0] != 5 || buf[1] != 0) {
    snprintf(errbuf, errlen, "SOCK5 Protocol error");
    return -1;
  }

  buf[0] = 5;
  buf[1] = 1; // Connect
  buf[2] = 0;
  buf[3] = 3; // Domainname

  buf[4] = hostnamelen;
  memcpy(buf + 5, hostname, hostnamelen);
  buf[5 + hostnamelen + 0] = port >> 8;
  buf[5 + hostnamelen + 1] = port;

  tcp_write_data(tc, buf, 5 + hostnamelen + 2);

  if(tcp_read_data(tc, buf, 5, NULL, NULL)) {
    snprintf(errbuf, errlen, "SOCK5 Read error");
    return -1;
  }

  if(buf[0] != 5 || buf[2] != 0) {
    snprintf(errbuf, errlen, "SOCK5 protocol error");
    return -1;
  }

  if(buf[1]) {
    // Error
    const char *errmsgs[9] = {
      [1] = "General SOCKS server failure",
      [2] = "Connection not allowed by ruleset",
      [3] = "Network unreachable",
      [4] = "Host unreachable",
      [5] = "Connection refused",
      [6] = "TTL expired",
      [7] = "Command not supported",
      [8] = "Address type not supported",
    };

    if(buf[1] > 8) {
      snprintf(errbuf, errlen, "SOCK5 reserved error 0x%x", buf[1]);
    } else {
      snprintf(errbuf, errlen, "SOCK5 error: %s", errmsgs[(int)buf[1]]);
    }
    return -1;
  }

  int alen; // Remaining bytes of address - 1
  switch(buf[3]) {
  case 1:
    alen = 3;
    break;
  case 3:
    alen = buf[4];
    break;
  case 4:
    alen = 15;
    break;
  default:
    snprintf(errbuf, errlen, "SOCK5 unrecognized address type 0x%x", buf[3]);
    return -1;
  }

  // Throw away bound address and port

  if(tcp_read_data(tc, NULL, alen + 2, NULL, NULL)) {
    snprintf(errbuf, errlen, "SOCK5 Read error");
    return -1;
  }
  return 0;
}




/**
 *
 */
tcpcon_t *
tcp_connect(const char *hostname, int port,
            char *errbuf, size_t errlen,
            int timeout, int flags, cancellable_t *c)
{
  tcpcon_t *tc;
  const int dbg = !!(flags & TCP_DEBUG);
  const char *errmsg;
  net_addr_t addr = {0};


  if(!strcmp(hostname, "localhost")) {
    addr.na_family = 4;
    addr.na_addr[0] = 127;
    addr.na_addr[3] = 1;

  } else if(gconf.proxy_host[0] && !(flags & TCP_NO_PROXY)) {

    if(!net_resolve_numeric(hostname, &addr) && addr.na_family == 4) {

      netif_t *ni = net_get_interfaces();

      if(ni != NULL) {
        for(int i = 0; ni[i].ifname[0]; i++) {
          if(net_is_addr_in_netif(ni, &addr))
            goto connect;
        }
        free(ni);
      }
    }

    tc = tcp_connect(gconf.proxy_host, gconf.proxy_port,
                     errbuf, errlen, timeout,
                     (flags & TCP_DEBUG) | TCP_NO_PROXY, c);
    if(tc == NULL)
      return NULL;

    if(socks_session_setup(tc, hostname, port, errbuf, errlen)) {
      tcp_close(tc);
      return NULL;
    }

    goto connected;

  } else {
    if(net_resolve(hostname, &addr, &errmsg)) {

      snprintf(errbuf, errlen, "Unable to resolve %s -- %s", hostname, errmsg);

      // If no dots in hostname, try to resolve using NetBIOS name lookup
      if(strchr(hostname, '.') != NULL || nmb_resolve(hostname, &addr))
        return NULL;
    }
  }

 connect:

  addr.na_port = port;
  tc = tcp_connect_arch(&addr, errbuf, errlen, timeout, c, dbg);
  if(tc == NULL)
    return NULL;

 connected:
  if(flags & TCP_SSL) {

    if(tcp_ssl_open(tc, errbuf, errlen, hostname)) {
      tcp_close(tc);
      return NULL;
    }

  }
  return tc;
}

/**
 *
 */
void
tcp_close(tcpcon_t *tc)
{
  if(tc->ssl != NULL)
    tcp_ssl_close(tc);

  tcp_set_cancellable(tc, NULL);

  htsbuf_queue_flush(&tc->spill);

  tcp_close_arch(tc);

  free(tc);
}


int
net_is_addr_in_netif(const netif_t *ni, const net_addr_t *na)
{
  uint32_t mask   = rd32_be(ni->ipv4_mask);
  uint32_t ifaddr = rd32_be(ni->ipv4_addr);
  uint32_t addr   = rd32_be(na->na_addr);

  return (addr & mask) == (ifaddr & mask);
}



void
net_refresh_network_status(void)
{
  netif_t *ni = net_get_interfaces();
  char tmp[32];
  prop_t *np = prop_create(prop_get_global(), "net");
  prop_t *interfaces = prop_create(np, "interfaces");

  if(ni == NULL || ni->ifname[0] == 0) {
    prop_set(np, "connectivity", PROP_SET_INT, 0);
    prop_destroy_childs(interfaces);
    free(ni);
    return;
  }

  prop_set(np, "connectivity", PROP_SET_INT, 1);

  prop_mark_childs(interfaces);

  for(int i = 0; ni[i].ifname[0]; i++) {

    prop_t *iface = prop_create_r(interfaces, ni[i].ifname);

    prop_unmark(iface);

    prop_set(iface, "name", PROP_SET_STRING, ni[i].ifname);

    snprintf(tmp, sizeof(tmp), "%d.%d.%d.%d",
           ni[i].ipv4_addr[0],
           ni[i].ipv4_addr[1],
           ni[i].ipv4_addr[2],
           ni[i].ipv4_addr[3]);
    prop_set(iface, "ipv4_addr", PROP_SET_STRING, tmp);

    snprintf(tmp, sizeof(tmp), "%d.%d.%d.%d",
           ni[i].ipv4_mask[0],
           ni[i].ipv4_mask[1],
           ni[i].ipv4_mask[2],
           ni[i].ipv4_mask[3]);
    prop_set(iface, "ipv4_mask", PROP_SET_STRING, tmp);
    prop_ref_dec(iface);
  }
  free(ni);
  prop_destroy_marked_childs(interfaces);
}

INITME(INIT_GROUP_NET, net_refresh_network_status, NULL);
