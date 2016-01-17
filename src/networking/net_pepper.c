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
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "main.h"
#include "net_i.h"
#include "misc/bytestream.h"

#include "arch/nacl/nacl.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_host_resolver.h"
#include "ppapi/c/ppb_tcp_socket.h"
#include "ppapi/c/ppb_var.h"

extern PPB_HostResolver *ppb_hostresolver;
extern PPB_Core *ppb_core;
extern PPB_NetAddress *ppb_netaddress;
extern PPB_TCPSocket *ppb_tcpsocket;
extern PPB_Var *ppb_var;

extern PP_Instance g_Instance;

/**
 *
 */
static int
tcp_write(tcpcon_t *tc, const void *data, size_t len)
{
  while(len > 0) {
    int r = ppb_tcpsocket->Write(tc->fd, data, len, PP_BlockUntilComplete());
    if(r <= 0)
      return -1;
    len  -= r;
    data += r;
  }

  return 0;
}


/**
 *
 */
static int
tcp_read(tcpcon_t *tc, void *buf, size_t len, int all,
	 net_read_cb_t *cb, void *opaque)
{
  int c, tot = 0;
  if(!all) {
    c = ppb_tcpsocket->Read(tc->fd, buf, len,  PP_BlockUntilComplete());
    return c > 0 ? c : -1;
  }

  while(tot != len) {
    c = ppb_tcpsocket->Read(tc->fd, buf + tot, len - tot,
                            PP_BlockUntilComplete());

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
tcpcon_t *
tcp_from_fd(int fd)
{
  //  TRACE(TRACE_DEBUG, "NET", "%s NOT IMPLEMENTED", __FUNCTION__);
  return NULL;
}


/**
 *
 */
int
pepper_NetAddress_to_net_addr(struct net_addr *addr, PP_Resource addr_res)
{
  struct PP_NetAddress_IPv4 ipv4_addr;
  struct PP_NetAddress_IPv6 ipv6_addr;

  switch(ppb_netaddress->GetFamily(addr_res)) {

  case PP_NETADDRESS_FAMILY_IPV4:
    ppb_netaddress->DescribeAsIPv4Address(addr_res, &ipv4_addr);
    addr->na_family = 4;
    memcpy(addr->na_addr, ipv4_addr.addr, 4);
    addr->na_port = rd16_be((uint8_t *)&ipv4_addr.port);
    return 0;

  case PP_NETADDRESS_FAMILY_IPV6:
    ppb_netaddress->DescribeAsIPv6Address(addr_res, &ipv6_addr);
    addr->na_family = 6;
    memcpy(addr->na_addr, ipv6_addr.addr, 16);
    addr->na_port = rd16_be((uint8_t *)&ipv6_addr.port);
    return 0;

  default:
    TRACE(TRACE_DEBUG, "NET", "Unknown family");
    return -1;
  }
}

/**
 *
 */
int
pepper_Resolver_to_net_addr(struct net_addr *addr, PP_Resource res)
{
  PP_Resource addr_res = ppb_hostresolver->GetNetAddress(res, 0);
  if(addr_res == 0) {
    TRACE(TRACE_DEBUG, "NET", "No addresses");
    return -1;
  }

  return pepper_NetAddress_to_net_addr(addr, addr_res);
}


/**
 *
 */
int
net_resolve(const char *hostname, net_addr_t *addr, const char **err)
{
  int rval = -1;
  PP_Resource res = ppb_hostresolver->Create(g_Instance);
  struct PP_HostResolver_Hint hint = {PP_NETADDRESS_FAMILY_UNSPECIFIED, 0};
  int r = ppb_hostresolver->Resolve(res, hostname, 0, &hint,
                                    PP_BlockUntilComplete());

  if(r == 0) {
    // OK

    if(pepper_Resolver_to_net_addr(addr, res)) {
      *err = "Invalid address";
    } else {
      rval = 0;
    }

  } else {
    *err = pepper_errmsg(r);
  }

  ppb_core->ReleaseResource(res);

  return rval;
}


/**
 *
 */
tcpcon_t *
tcp_connect_arch(const net_addr_t *na,
                 char *errbuf, size_t errlen,
                 int timeout, cancellable_t *c, int dbg)
{
  PP_Resource sock = ppb_tcpsocket->Create(g_Instance);
  PP_Resource addr;

  struct PP_NetAddress_IPv4 ipv4_addr = {};
  struct PP_NetAddress_IPv6 ipv6_addr = {};

  switch(na->na_family) {
  case 4:
    memcpy(ipv4_addr.addr, na->na_addr, 4);
    wr16_be((uint8_t *)&ipv4_addr.port, na->na_port);
    addr = ppb_netaddress->CreateFromIPv4Address(g_Instance, &ipv4_addr);
    break;
  case 6:
    memcpy(ipv6_addr.addr, na->na_addr, 16);
    wr16_be((uint8_t *)&ipv6_addr.port, na->na_port);
    addr = ppb_netaddress->CreateFromIPv6Address(g_Instance, &ipv6_addr);
    break;
  default:
    abort();
  }

  if(dbg) { // debug
    struct PP_Var remote = ppb_netaddress->DescribeAsString(addr, 1);
    uint32_t len;
    const char *s = ppb_var->VarToUtf8(remote, &len);
    TRACE(TRACE_DEBUG, "TCP", "Connecting to %d %.*s", len, len, s);
    ppb_var->Release(remote);
  }

  int r = ppb_tcpsocket->Connect(sock, addr, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(addr);
  if(r) {
    snprintf(errbuf, errlen, "Unable to connect -- %s", pepper_errmsg(r));
    ppb_core->ReleaseResource(sock);
    return NULL;
  }

  tcpcon_t *tc = calloc(1, sizeof(tcpcon_t));
  tc->fd = sock;

  htsbuf_queue_init(&tc->spill, 0);
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
  ppb_core->ReleaseResource(tc->fd);
}


/**
 *
 */
void
tcp_shutdown(tcpcon_t *tc)
{
  ppb_tcpsocket->Close(tc->fd);
}


/**
 *
 */
void
tcp_huge_buffer(tcpcon_t *tc)
{
  //  TRACE(TRACE_DEBUG, "NET", "%s NOT IMPLEMENTED", __FUNCTION__);
}


/**
 *
 */
void
tcp_set_read_timeout(tcpcon_t *tc, int ms)
{
  //  TRACE(TRACE_DEBUG, "NET", "%s NOT IMPLEMENTED", __FUNCTION__);
}


/**
 *
 */
void
net_change_nonblocking(int fd, int on)
{
  //  TRACE(TRACE_DEBUG, "NET", "%s NOT IMPLEMENTED", __FUNCTION__);
}


/**
 *
 */
void
net_change_ndelay(int fd, int on)
{
  //  TRACE(TRACE_DEBUG, "NET", "%s NOT IMPLEMENTED", __FUNCTION__);
}


/**
 *
 */
int
net_resolve_numeric(const char *str, net_addr_t *addr)
{
  int a,b,c,d;
  if(sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
    memset(addr, 0, sizeof(net_addr_t));
    addr->na_family = 4;
    addr->na_addr[0] = a;
    addr->na_addr[1] = b;
    addr->na_addr[2] = c;
    addr->na_addr[3] = d;
    return 0;
  }
  return 1;
}
