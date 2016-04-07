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
#include "misc/minmax.h"
#include "misc/bytestream.h"
#include "asyncio.h"
#include "prop/prop.h"
#include "arch/nacl/nacl.h"

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_host_resolver.h"
#include "ppapi/c/ppb_message_loop.h"
#include "ppapi/c/ppb_network_monitor.h"
#include "ppapi/c/ppb_network_list.h"
#include "ppapi/c/ppb_tcp_socket.h"
#include "ppapi/c/ppb_udp_socket.h"
#include "ppapi/c/ppb_var.h"

struct prop_courier *asyncio_courier;

extern PP_Instance g_Instance;

static const int asyncio_dbg = 0;

LIST_HEAD(asyncio_fd_list, asyncio_fd);

struct asyncio_fd {
  void *af_opaque;
  char *af_name;

  union {
    asyncio_error_callback_t  *af_error_callback;
    asyncio_accept_callback_t *af_accept_callback;
    asyncio_udp_callback_t    *af_udp_callback;
  };

  asyncio_read_callback_t *af_read_callback;

  htsbuf_queue_t af_sendq;
  htsbuf_queue_t af_recvq;

  int af_refcount;
  PP_Resource af_sock;
  int af_pending_write;  // Number of bytes we're currently trying to write

  char *af_recv_segment;
  int af_recv_segment_size;

  PP_Resource af_incoming_connection;

  PP_Resource af_remote_addr;

  int64_t af_timeout;
  LIST_ENTRY(asyncio_fd) af_timeout_link;
};

static struct asyncio_fd_list timeout_list;
static int timeout_list_count;


extern PPB_HostResolver *ppb_hostresolver;
extern PPB_Core *ppb_core;
extern PPB_NetAddress *ppb_netaddress;
extern PPB_TCPSocket *ppb_tcpsocket;
extern PPB_UDPSocket *ppb_udpsocket;
extern PPB_Var *ppb_var;
extern const PPB_MessageLoop *ppb_messageloop;
extern const PPB_NetworkMonitor *ppb_networkmonitor;
extern const PPB_NetworkList *ppb_networklist;

static PP_Resource asyncio_msgloop;


#define MAX_WORKERS 64
static void (*workers[MAX_WORKERS])(void);
static int workers_cnt;

LIST_HEAD(asyncio_timer_list, asyncio_timer);
static struct asyncio_timer_list asyncio_timers;

static void tcp_do_write(asyncio_fd_t *af);
static void tcp_do_recv(asyncio_fd_t *af);
static void tcp_do_accept(asyncio_fd_t *af);

static void udp_do_recv(asyncio_fd_t *af);

/**
 *
 */
int
asyncio_add_worker(void (*fn)(void))
{
  int r = ++workers_cnt;
  workers[r] = fn;
  return r;
}


/**
 *
 */
void
asyncio_wakeup_worker(int id)
{
  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              (void *)workers[id], NULL}, 0);
}


/**
 *
 */
void
asyncio_run_task(void (*fn)(void *aux), void *aux)
{
  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              (void *)fn, aux}, 0);
}


/**
 *
 */
int64_t
async_current_time(void)
{
  return arch_get_ts();
}




void
asyncio_timer_init(asyncio_timer_t *at, void (*fn)(void *opaque),
                   void *opaque)
{
  at->at_fn = fn;
  at->at_opaque = opaque;
  at->at_expire = 0;
}


/**
 *
 */
static int
at_compar(const asyncio_timer_t *a, const asyncio_timer_t *b)
{
  if(a->at_expire < b->at_expire)
    return -1;
  return 1;
}


/**
 *
 */
static void
process_timers(int64_t now)
{
  asyncio_timer_t *at;

  while((at = LIST_FIRST(&asyncio_timers)) != NULL &&
        at->at_expire <= now) {
    LIST_REMOVE(at, at_link);
    at->at_expire = 0;
    at->at_fn(at->at_opaque);
  }
}


/**
 *
 */
void
asyncio_timer_arm(asyncio_timer_t *at, int64_t expire)
{
  if(at->at_expire)
    LIST_REMOVE(at, at_link);

  at->at_expire = expire;
  LIST_INSERT_SORTED(&asyncio_timers, at, at_link, at_compar, asyncio_timer_t);
}


/**
 *
 */
void
asyncio_timer_arm_delta_sec(asyncio_timer_t *at, int delta)
{
  asyncio_timer_arm(at, async_current_time() + delta * 1000000LL);
}


/**
 *
 */
void
asyncio_timer_disarm(asyncio_timer_t *at)
{
  if(at->at_expire) {
    LIST_REMOVE(at, at_link);
    at->at_expire = 0;
  }
}




/**
 *
 */
static asyncio_fd_t *
asyncio_fd_create(const char *name, PP_Resource sock, void *opaque)
{
  asyncio_fd_t *af = calloc(1, sizeof(asyncio_fd_t));
  af->af_sock = sock;
  af->af_name = strdup(name);
  af->af_opaque = opaque;
  af->af_refcount = 1;
  htsbuf_queue_init(&af->af_sendq, 0);
  htsbuf_queue_init(&af->af_recvq, 0);
  return af;
}


/**
 *
 */
static asyncio_fd_t *
asyncio_fd_retain(asyncio_fd_t *af)
{
  af->af_refcount++;
  return af;
}


/**
 *
 */
static void
asyncio_fd_release(asyncio_fd_t *af)
{
  af->af_refcount--;
  if(af->af_refcount > 0)
    return;

  assert(af->af_timeout == 0);
  assert(af->af_recv_segment == NULL);
  assert(af->af_pending_write == 0);
  htsbuf_queue_flush(&af->af_sendq);
  htsbuf_queue_flush(&af->af_recvq);
  ppb_core->ReleaseResource(af->af_sock);
  free(af->af_recv_segment);
  free(af->af_name);
  free(af);
}


/**
 *
 */
void
asyncio_del_fd(asyncio_fd_t *af)
{
  if(af->af_timeout) {
    LIST_REMOVE(af, af_timeout_link);
    timeout_list_count--;
    af->af_timeout = 0;
  }

  af->af_read_callback = NULL;
  af->af_error_callback = NULL;

  ppb_tcpsocket->Close(af->af_sock);
  asyncio_fd_release(af);
}


/**
 *
 */
static void
tcp_accept_completed(void *aux, int retcode)
{
  asyncio_fd_t *af = aux;

  if(retcode == 0 && af->af_accept_callback != NULL) {
    net_addr_t local = {0}, remote = {0};

    pepper_NetAddress_to_net_addr(&local,
                                  ppb_tcpsocket->GetLocalAddress(af->af_sock));

    pepper_NetAddress_to_net_addr(&remote,
                                  ppb_tcpsocket->GetRemoteAddress(af->af_sock));

    af->af_accept_callback(af->af_opaque, af->af_incoming_connection,
                           &local, &remote);
  }

  af->af_incoming_connection = 0;
  tcp_do_accept(af);
  asyncio_fd_release(af);
}


/**
 *
 */
static void
tcp_do_accept(asyncio_fd_t *af)
{
  assert(af->af_incoming_connection == 0);

  ppb_tcpsocket->Accept(af->af_sock, &af->af_incoming_connection,
                        (const struct PP_CompletionCallback) {
                          tcp_accept_completed, asyncio_fd_retain(af)});
}


/**
 *
 */
asyncio_fd_t *
asyncio_listen(const char *name,
               int port,
               asyncio_accept_callback_t *cb,
               void *opaque,
               int bind_any_on_fail)
{
  PP_Resource sock = ppb_tcpsocket->Create(g_Instance);
  PP_Resource addr;
  struct PP_NetAddress_IPv4 ipv4_addr = {};

  wr16_be((uint8_t *)&ipv4_addr.port, port);
  addr = ppb_netaddress->CreateFromIPv4Address(g_Instance, &ipv4_addr);

  int r = ppb_tcpsocket->Bind(sock, addr, PP_BlockUntilComplete());
  if(r) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Unable to bind TCP socket %s -- %s",
          name, pepper_errmsg(r));
    ppb_core->ReleaseResource(addr);
    ppb_core->ReleaseResource(sock);
    return NULL;
  }

  r = ppb_tcpsocket->Listen(sock, 10, PP_BlockUntilComplete());

  if(r) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Unable to listen on TCP socket %s -- %s",
          name, pepper_errmsg(r));
    ppb_core->ReleaseResource(addr);
    ppb_core->ReleaseResource(sock);
    return NULL;
  }

  if(asyncio_dbg) {
    struct PP_Var remote = ppb_netaddress->DescribeAsString(addr, 1);
    uint32_t len;
    const char *s = ppb_var->VarToUtf8(remote, &len);
    TRACE(TRACE_DEBUG, "ASYNCIO", "Listening on TCP %.*s", len, s);
    ppb_var->Release(remote);
  }

  ppb_core->ReleaseResource(addr);

  asyncio_fd_t *af = asyncio_fd_create(name, sock, opaque);
  tcp_do_accept(af);
  af->af_accept_callback = cb;

  return af;
}


/**
 *
 */
static void
tcp_read_completed(void *aux, int32_t result)
{
  asyncio_fd_t *af = aux;

  if(result < 0) {
    if(af->af_error_callback != NULL)
      af->af_error_callback(af->af_opaque, pepper_errmsg(result));
    free(af->af_recv_segment);
    af->af_recv_segment = NULL;
  } else {
    assert(result <= af->af_recv_segment_size);
    htsbuf_append_prealloc(&af->af_recvq, af->af_recv_segment, result);
    af->af_recv_segment = NULL;

    if(af->af_read_callback != NULL)
      af->af_read_callback(af->af_opaque, &af->af_recvq);
    tcp_do_recv(af);
  }

  asyncio_fd_release(af);
}


/**
 *
 */
static void
tcp_do_recv(asyncio_fd_t *af)
{
  assert(af->af_recv_segment == NULL);

  af->af_recv_segment_size = 8192;
  af->af_recv_segment = malloc(af->af_recv_segment_size);

  ppb_tcpsocket->Read(af->af_sock,
                      af->af_recv_segment, af->af_recv_segment_size,
                      (const struct PP_CompletionCallback) {
                        tcp_read_completed, asyncio_fd_retain(af)});
}


/**
 *
 */
static void
tcp_connected(void *aux, int32_t errcode)
{
  asyncio_fd_t *af = aux;

  if(af->af_timeout) {
    af->af_timeout = 0;
    LIST_REMOVE(af, af_timeout_link);
    timeout_list_count--;
  }

  if(af->af_error_callback != NULL) {

    if(errcode) {
      af->af_error_callback(af->af_opaque, pepper_errmsg(errcode));
    } else {
      af->af_error_callback(af->af_opaque, NULL);
      tcp_do_recv(af);
    }
  }
  asyncio_fd_release(af);
}


/**
 *
 */
asyncio_fd_t *
asyncio_connect(const char *name,
                const net_addr_t *na,
                asyncio_error_callback_t *error_cb,
                asyncio_read_callback_t *read_cb,
                void *opaque,
                int timeout, void *tls, const char *hostname)
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

  if(asyncio_dbg) { // debug
    struct PP_Var remote = ppb_netaddress->DescribeAsString(addr, 1);
    uint32_t len;
    const char *s = ppb_var->VarToUtf8(remote, &len);
    TRACE(TRACE_DEBUG, "ASYNCIO", "Connecting to %.*s", len, s);
    ppb_var->Release(remote);
  }


  asyncio_fd_t *af = asyncio_fd_create(name, sock, opaque);
  af->af_error_callback = error_cb;
  af->af_read_callback  = read_cb;

  af->af_timeout = async_current_time() + timeout * 1000;
  LIST_INSERT_HEAD(&timeout_list, af, af_timeout_link);
  timeout_list_count++;

  ppb_tcpsocket->Connect(sock, addr,
                         (const struct PP_CompletionCallback) {
                           tcp_connected, asyncio_fd_retain(af)});

  ppb_core->ReleaseResource(addr);
  return af;
}


/**
 *
 */
asyncio_fd_t *
asyncio_attach(const char *name, int fd,
               asyncio_error_callback_t *error_cb,
               asyncio_read_callback_t *read_cb,
               void *opaque,
               void *tls)
{
  asyncio_fd_t *af = asyncio_fd_create(name, fd, opaque);
  af->af_error_callback = error_cb;
  af->af_read_callback  = read_cb;

  tcp_do_recv(af);

  return af;
}


/**
 *
 */
static void
tcp_written(void *aux, int result)
{
  asyncio_fd_t *af = aux;
  if(result < 0) {
    af->af_pending_write = 0;
    if(af->af_error_callback != NULL)
      af->af_error_callback(af->af_opaque, pepper_errmsg(result));
  } else {
    assert(result <= af->af_pending_write);
    htsbuf_drop(&af->af_sendq, result);
    af->af_pending_write = 0;
    tcp_do_write(af);
  }

  asyncio_fd_release(af);
}


/**
 *
 */
static void
tcp_do_write(asyncio_fd_t *af)
{
  if(af->af_pending_write)
    return;

  const htsbuf_data_t *hd = TAILQ_FIRST(&af->af_sendq.hq_q);
  if(hd == NULL)
    return;

  int size = hd->hd_data_len - hd->hd_data_off;
  assert(size > 0);
  const void *d = hd->hd_data + hd->hd_data_off;

  af->af_pending_write = size;

  ppb_tcpsocket->Write(af->af_sock, d, size,
                       (const struct PP_CompletionCallback) {
                         tcp_written, asyncio_fd_retain(af)});
}



/**
 *
 */
void
asyncio_send(asyncio_fd_t *af, const void *buf, size_t len, int cork)
{
  htsbuf_append(&af->af_sendq, buf, len);
  if(!cork)
    tcp_do_write(af);
}


/**
 *
 */
void
asyncio_sendq(asyncio_fd_t *af, htsbuf_queue_t *q, int cork)
{
  htsbuf_appendq(&af->af_sendq, q);
  if(!cork)
    tcp_do_write(af);
}


/**
 *
 */
int
asyncio_get_port(asyncio_fd_t *af)
{
  TRACE(TRACE_DEBUG, "ASYNCIO", "%s NOT IMPLEMENTED", __FUNCTION__);
  return 0;
}

/**
 *
 */
void
asyncio_set_timeout_delta_sec(asyncio_fd_t *af, int seconds)
{
  if(af->af_timeout == 0) {
    LIST_INSERT_HEAD(&timeout_list, af, af_timeout_link);
    timeout_list_count++;
  }

  assert(af->af_error_callback != NULL);
  af->af_timeout = seconds * 1000000LL + async_current_time();
}


/**
 *
 */
static void
udp_read_completed(void *aux, int32_t result)
{
  asyncio_fd_t *af = aux;

  if(asyncio_dbg) {
    TRACE(TRACE_DEBUG, "ASYNCIO", "UDP RECV %s : %d", af->af_name, result);
  }

  if(result > 0 && af->af_udp_callback != NULL) {

    if(asyncio_dbg) {
      struct PP_Var remote =
        ppb_netaddress->DescribeAsString(af->af_remote_addr, 1);
      uint32_t len;
      const char *s = ppb_var->VarToUtf8(remote, &len);
      TRACE(TRACE_DEBUG, "ASYNCIO", "Got UDP from %.*s", len, s);
      ppb_var->Release(remote);
    }

    net_addr_t remote = {0};
    pepper_NetAddress_to_net_addr(&remote, af->af_remote_addr);

    af->af_udp_callback(af->af_opaque, af->af_recv_segment,
                        result, &remote);
  }

  ppb_core->ReleaseResource(af->af_remote_addr);
  af->af_remote_addr = 0;
  udp_do_recv(af);
  asyncio_fd_release(af);
}


/**
 *
 */
static void
udp_do_recv(asyncio_fd_t *af)
{
  assert(af->af_remote_addr == 0);

  if(af->af_recv_segment == NULL) {
    af->af_recv_segment_size = 65536;
    af->af_recv_segment = malloc(af->af_recv_segment_size);
  }

  ppb_udpsocket->RecvFrom(af->af_sock,
                          af->af_recv_segment, af->af_recv_segment_size,
                          &af->af_remote_addr,
                          (const struct PP_CompletionCallback) {
                            udp_read_completed, asyncio_fd_retain(af)});
}


/**
 *
 */
asyncio_fd_t *
asyncio_udp_bind(const char *name,
                 const net_addr_t *na,
                 asyncio_udp_callback_t *cb,
                 void *opaque,
                 int bind_any_on_fail,
                 int broadcast)
{
  PP_Resource sock = ppb_udpsocket->Create(g_Instance);
  PP_Resource addr;
  struct PP_NetAddress_IPv4 ipv4_addr = {};

  if(broadcast) {
    int r = ppb_udpsocket->SetOption(sock, PP_UDPSOCKET_OPTION_BROADCAST,
                                     PP_MakeBool(1), PP_BlockUntilComplete());
    if(r)
      TRACE(TRACE_ERROR, "ASYNCIO",
            "Failed to put socket %s into broadcast modé -- %s",
            name, pepper_errmsg(r));
  }

  ppb_udpsocket->SetOption(sock, PP_UDPSOCKET_OPTION_ADDRESS_REUSE,
                           PP_MakeBool(1), PP_BlockUntilComplete());


  if(na != NULL) {
    wr16_be((uint8_t *)&ipv4_addr.port, na->na_port);
    memcpy(&ipv4_addr.addr, na->na_addr, 4);
  }
  addr = ppb_netaddress->CreateFromIPv4Address(g_Instance, &ipv4_addr);

  int r = ppb_udpsocket->Bind(sock, addr, PP_BlockUntilComplete());
  if(r) {
    TRACE(TRACE_ERROR, "ASYNCIO", "Unable to bind UDP socket %s -- %s",
          name, pepper_errmsg(r));
    ppb_core->ReleaseResource(addr);
    ppb_core->ReleaseResource(sock);
    return NULL;
  }

  if(asyncio_dbg) {
    struct PP_Var remote = ppb_netaddress->DescribeAsString(addr, 1);
    uint32_t len;
    const char *s = ppb_var->VarToUtf8(remote, &len);
    TRACE(TRACE_DEBUG, "ASYNCIO", "Listening on UDP %.*s", len, s);
    ppb_var->Release(remote);
  }

  ppb_core->ReleaseResource(addr);

  asyncio_fd_t *af = asyncio_fd_create(name, sock, opaque);
  af->af_udp_callback = cb;
  udp_do_recv(af);

  return af;
}


/**
 *
 */
int
asyncio_udp_add_membership(asyncio_fd_t *af, const net_addr_t *group,
                           const net_addr_t *interface)
{
  if(interface != NULL)
    return -1;

  PP_Resource g;
  struct PP_NetAddress_IPv4 ipv4_addr = {};

  memcpy(&ipv4_addr.addr, group->na_addr, 4);
  g = ppb_netaddress->CreateFromIPv4Address(g_Instance, &ipv4_addr);

  int r = ppb_udpsocket->JoinGroup(af->af_sock, g, PP_BlockUntilComplete());
  ppb_core->ReleaseResource(g);
  if(r)
    return -1;
  return 0;
}

/**
 *
 */
void
asyncio_udp_send(asyncio_fd_t *af, const void *data, int size,
                 const net_addr_t *remote_addr)
{
  PP_Resource addr;
  struct PP_NetAddress_IPv4 ipv4_addr = {};

  memcpy(&ipv4_addr.addr, remote_addr->na_addr, 4);
  wr16_be((uint8_t *)&ipv4_addr.port, remote_addr->na_port);
  addr = ppb_netaddress->CreateFromIPv4Address(g_Instance, &ipv4_addr);

  int r = ppb_udpsocket->SendTo(af->af_sock, data, size, addr,
                                PP_BlockUntilComplete());


  if(asyncio_dbg) {
    TRACE(TRACE_DEBUG, "ASYNCIO", "UDP SEND %s : %s", af->af_name,
          pepper_errmsg(r));
  }

  ppb_core->ReleaseResource(addr);
}





/**
 *
 */
struct asyncio_dns_req {
  char *adr_hostname;
  void *adr_opaque;
  void (*adr_cb)(void *opaque, int status, const void *data);
  int adr_cancelled;
  PP_Resource adr_res;
};


/**
 *
 */
static void
dns_lookup_done(void *aux, int result)
{
  asyncio_dns_req_t *adr = aux;

  if(!adr->adr_cancelled) {

    if(result) {
      adr->adr_cb(adr->adr_opaque, ASYNCIO_DNS_STATUS_FAILED,
                  pepper_errmsg(result));
    } else {
      net_addr_t addr;
      if(pepper_Resolver_to_net_addr(&addr, adr->adr_res)) {
        adr->adr_cb(adr->adr_opaque, ASYNCIO_DNS_STATUS_FAILED, "Bad address");
      } else {
        adr->adr_cb(adr->adr_opaque, ASYNCIO_DNS_STATUS_COMPLETED, &addr);
      }
    }
  }
  ppb_core->ReleaseResource(adr->adr_res);
  free(adr->adr_hostname);
  free(adr);
}

/**
 * Cancel a pending DNS lookup
 */
void
asyncio_dns_cancel(asyncio_dns_req_t *adr)
{
  adr->adr_cancelled = 1;
}

/**
 *
 */
static void
dns_do_request(void *aux, int r)
{
  asyncio_dns_req_t *adr = aux;

  struct PP_HostResolver_Hint hint = {PP_NETADDRESS_FAMILY_UNSPECIFIED, 0};

  ppb_hostresolver->Resolve(adr->adr_res, adr->adr_hostname, 0, &hint,
                            (const struct PP_CompletionCallback) {
                              dns_lookup_done, adr});
}

/**
 *
 */
asyncio_dns_req_t *
asyncio_dns_lookup_host(const char *hostname,
                        void (*cb)(void *opaque,
                                   int status,
                                   const void *data),
                        void *opaque)
{
  asyncio_dns_req_t *adr = calloc(1, sizeof(asyncio_dns_req_t));
  adr->adr_hostname = strdup(hostname);
  adr->adr_cb = cb;
  adr->adr_opaque = opaque;
  adr->adr_res = ppb_hostresolver->Create(g_Instance);

  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              dns_do_request, adr}, 0);
  return adr;
}


/**
 *
 */
static void
process_fd_timeouts(int64_t now)
{
  asyncio_fd_t *af;
  asyncio_fd_t **vec = malloc(timeout_list_count * sizeof(asyncio_fd_t *));
  int i;

  const int count = timeout_list_count;

  af = LIST_FIRST(&timeout_list);
  for(i = 0; i < timeout_list_count; i++) {
    vec[i] = asyncio_fd_retain(af);
    af = LIST_NEXT(af, af_timeout_link);
  }

  assert(af == NULL);

  for(i = 0; i < count; i++) {
    af = vec[i];
    if(af->af_timeout && af->af_timeout < now) {
      LIST_REMOVE(af, af_timeout_link);
      timeout_list_count--;
      af->af_timeout = 0;
      af->af_error_callback(af->af_opaque, "Connection timed out");
    }
    asyncio_fd_release(af);
  }
  free(vec);
}


/**
 *
 */
static void
asyncio_periodic(void *aux, int32_t res)
{
  int64_t now = async_current_time();

  process_timers(now);
  process_fd_timeouts(now);
  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              asyncio_periodic, NULL}, 1000);
}


/**
 *
 */
static void
asyncio_courier_poll(void *aux, int val)
{
  prop_courier_poll(aux);
}


/**
 *
 */
static void
asyncio_courier_notify(void *opaque)
{
  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              asyncio_courier_poll, asyncio_courier}, 0);
}


/**
 *
 */
typedef struct netifchange {
  void (*cb)(const struct netif *ni);
  LIST_ENTRY(netifchange) link;
} netifchange_t;

static LIST_HEAD(, netifchange) netifchanges;


static PP_Resource network_list;
static PP_Resource network_monitor;

static HTS_MUTEX_DECL(netif_mutex);
static struct netif *netif_current;
static int num_netif;

struct na_items {
  PP_Resource *data;
  int element_count;
};

static void *
get_data_buf(void* user_data, uint32_t count, uint32_t size)
{
  struct na_items *output = (struct na_items *)user_data;
  output->element_count = count;

  if(size) {
    output->data = malloc(count * size);
    if(output->data == NULL)
      output->element_count = 0;

  } else {
    output->data = NULL;
  }
  return output->data;
}


static void
network_list_updated(void *aux, int val)
{
  if(val) {
    TRACE(TRACE_DEBUG, "Network", "Unable to get list of networks -- %s",
          pepper_errmsg(val));
    return;
  }

  int count = ppb_networklist->GetCount(network_list);
  TRACE(TRACE_DEBUG, "Network", "Got network update %d networks", count);

  hts_mutex_lock(&netif_mutex);
  free(netif_current);

  netif_current = NULL;
  num_netif = 0;

  for(int i = 0; i < count; i++) {


    struct PP_Var name = ppb_networklist->GetDisplayName(network_list, i);

    uint32_t len;
    const char *s = ppb_var->VarToUtf8(name, &len);
    if(s == NULL) {
      ppb_var->Release(name);
      continue;
    }
    char *ifname = alloca(len + 1);
    memcpy(ifname, s, len);
    ifname[len] = 0;

    ppb_var->Release(name);

    const int state = ppb_networklist->GetState(network_list, i);
    TRACE(TRACE_DEBUG, "Network", "%s - %s",
          ifname, state == PP_NETWORKLIST_STATE_UP ? "Up" : "Down");

    if(state != PP_NETWORKLIST_STATE_UP)
      continue;

    struct na_items array = {};
    struct PP_ArrayOutput output = {&get_data_buf, &array};

    ppb_networklist->GetIpAddresses(network_list, i, output);

    int postfixcnt = 0;
    for(int j = 0; j < array.element_count; j++) {

      net_addr_t local = {0};
      pepper_NetAddress_to_net_addr(&local, array.data[j]);

      if(local.na_family == 4) {
        TRACE(TRACE_DEBUG, "Network", "  Address %s",
              net_addr_str(&local));

        netif_current = realloc(netif_current,
                                sizeof(netif_t) * (num_netif + 2));
        memset(netif_current + num_netif + 1, 0, sizeof(netif_t));

        netif_t *ni = netif_current + num_netif;


        if(postfixcnt == 0)
          snprintf(ni->ifname, sizeof(ni->ifname), "%s", ifname);
        else
          snprintf(ni->ifname, sizeof(ni->ifname), "%s:%d", ifname, postfixcnt);

        memcpy(ni->ipv4_addr, local.na_addr, 4);

        // Pepper does not provide this, so we just guess
        ni->ipv4_mask[0] = 255;
        ni->ipv4_mask[1] = 255;
        ni->ipv4_mask[2] = 255;
        ni->ipv4_mask[3] = 0;

        postfixcnt++;
        num_netif++;
      }
      ppb_core->ReleaseResource(array.data[j]);
    }
    free(array.data);
  }

  hts_mutex_unlock(&netif_mutex);

  ppb_core->ReleaseResource(network_list);

  ppb_networkmonitor->UpdateNetworkList(network_monitor, &network_list,
                                        (const struct PP_CompletionCallback) {
                                           network_list_updated, NULL});
  TRACE(TRACE_DEBUG, "Network", "Total netifs: %d   %p",
        num_netif, netif_current);

  netifchange_t *nic;
  LIST_FOREACH(nic, &netifchanges, link) {
    nic->cb(netif_current);
  }

  net_refresh_network_status();
}


/**
 *
 */
static void *
asyncio_thread(void *aux)
{
  ppb_messageloop->AttachToCurrentThread(asyncio_msgloop);

  network_monitor = ppb_networkmonitor->Create(g_Instance);
  ppb_networkmonitor->UpdateNetworkList(network_monitor, &network_list,
                                        (const struct PP_CompletionCallback) {
                                          network_list_updated, NULL});
  ppb_messageloop->Run(asyncio_msgloop);
  return NULL;
}


/**
 *
 */
void
asyncio_init_early(void)
{
  pthread_t p;
  asyncio_msgloop = ppb_messageloop->Create(g_Instance);
  pthread_create(&p, NULL, asyncio_thread, NULL);
}


/**
 *
 */
static void
asyncio_start_on_thread(void *aux, int val)
{
  asyncio_courier = prop_courier_create_notify(asyncio_courier_notify, NULL);

  init_group(INIT_GROUP_ASYNCIO);
  asyncio_periodic(NULL, 0);
}


/**
 *
 */
void
asyncio_start(void)
{
  ppb_messageloop->PostWork(asyncio_msgloop,
                            (const struct PP_CompletionCallback) {
                              asyncio_start_on_thread, NULL}, 0);
}


/**
 *
 */
void
asyncio_register_for_network_changes(void (*cb)(const struct netif *ni))
{
  netifchange_t *nic = malloc(sizeof(netifchange_t));
  nic->cb = cb;
  LIST_INSERT_HEAD(&netifchanges, nic, link);
  struct netif *ni = net_get_interfaces();
  nic->cb(ni);
  free(ni);
}


netif_t *
net_get_interfaces(void)
{
  hts_mutex_lock(&netif_mutex);

  netif_t *ni;
  if(num_netif == 0) {
    ni = NULL;
  } else {
    size_t s = sizeof(netif_t) * (num_netif + 1);
    ni = malloc(s);
    memcpy(ni, netif_current, s);
  }
  hts_mutex_unlock(&netif_mutex);
  return ni;
}

void *
asyncio_ssl_create_server(const char *privkeyfile, const char *certfile)
{
  return NULL;
}

void *
asyncio_ssl_create_client(void)
{
  return NULL;
}
