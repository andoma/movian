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
#include <string.h>
#include <stdio.h>

#include "main.h"
#include "networking/asyncio.h"
#include "misc/endian.h"
#include "task.h"
#include "smbv1.h"
#include "misc/str.h"
#include "service.h"
#include "nmb.h"

static hts_mutex_t nmb_mutex;
static hts_cond_t nmb_resolver_cond;

LIST_HEAD(nmb_server_list, nmb_server);
LIST_HEAD(nmb_resolve_list, nmb_resolve);

typedef struct nmb_server {
  LIST_ENTRY(nmb_server) ns_link;
  char *ns_workgroup;
  char *ns_name;
  int ns_mark;
  service_t *ns_service;
} nmb_server_t;

static struct nmb_server_list nmb_servers;

typedef struct nmb_resolve {
  LIST_ENTRY(nmb_resolve) nr_link;
  const char *nr_hostname;
  net_addr_t *nr_addr;
  uint16_t nr_txid;
  struct asyncio_timer nr_timeout;
  int nr_status;
} nmb_resolve_t;

static struct nmb_resolve_list nmb_resolve_pending;
static struct nmb_resolve_list nmb_resolve_sent;

static asyncio_fd_t *nmb_udp_fd;
static struct asyncio_timer nmb_timer;
static struct asyncio_timer nmb_flush_timer;
static uint16_t nmb_txid;
static int nmb_resolver_signal;
static uint16_t nmb_transaction_id_tally;

typedef struct {
  uint16_t transaction_id;
  uint16_t flags;
  uint16_t question_count;
  uint16_t answer_count;
  uint16_t name_service_count;
  uint16_t additional_record_count;

} __attribute__((packed)) nmbpkt_header_t;

typedef struct {
  nmbpkt_header_t h;
  char name[34];
  uint16_t type;
  uint16_t class;
} __attribute__((packed)) nmbpkt_question_t;


typedef struct {
  nmbpkt_header_t h;
  char name[34];
  uint16_t type;
  uint16_t class;
  uint32_t ttl;
  uint16_t length;
  uint16_t nodeflags;
  uint8_t addr[4];
} __attribute__((packed)) nmbpkt_answer_t;

/**
 *
 */
static void
encode_name(const char *in, char *out, char name_type)
{
  char buf[20] = {};
  char *p = out;
  int i;
  if(!strcmp(in, "*")) {
    buf[0] = '*';
  } else {
    snprintf(buf, sizeof(buf), "%-15.15s%c", in, name_type);
  }
  p[0] = 32;
  p++;
  for(i = 0; i < 16; i++) {
    uint8_t c = buf[i] >= 'a' && buf[i] <= 'z' ? buf[i] -32 : buf[i];
    p[i * 2    ] = (c >> 4)  + 'A';
    p[i * 2 + 1] = (c & 0xf) + 'A';
  }
  p += 32;
  p[0] = 0;
}



/**
 *
 */
static void
ns_destroy(nmb_server_t *ns)
{
  LIST_REMOVE(ns, ns_link);
  service_destroy(ns->ns_service);
  free(ns->ns_name);
  free(ns->ns_workgroup);
  free(ns);
}


/**
 *
 */
static void
remove_all_servers(void *aux)
{
  nmb_server_t *ns;
  while((ns = LIST_FIRST(&nmb_servers)) != NULL)
    ns_destroy(ns);
}


/**
 *
 */
static void
query_master_browser(void *a)
{
  nmb_server_t *ns, *next;

  net_addr_t na = {.na_family = 4};
  memcpy(na.na_addr, a, 4);
  free(a);

  char **servers = smb_enum_servers(net_addr_str(&na));
  if(servers == NULL)
    return;

  hts_mutex_lock(&nmb_mutex);

  LIST_FOREACH(ns, &nmb_servers, ns_link)
    ns->ns_mark++;

  for(int i = 0; servers[i] != NULL; i+=2) {
    const char *name = servers[i];
    const char *workgroup = servers[i+1];
    LIST_FOREACH(ns, &nmb_servers, ns_link) {
      if(!strcmp(ns->ns_name, name) && !strcmp(ns->ns_workgroup, workgroup))
        break;
    }
    if(ns == NULL) {
      char id[64];
      char url[64];

      ns = calloc(1, sizeof(nmb_server_t));
      ns->ns_name = strdup(name);
      ns->ns_workgroup = strdup(workgroup);
      LIST_INSERT_HEAD(&nmb_servers, ns, ns_link);

      snprintf(id, sizeof(id), "%s/%s", workgroup, name);
      snprintf(url, sizeof(url), "smb://%s", name);

      ns->ns_service = service_create_managed(id, name, url, "server", NULL,
                                              0, 0, SVC_ORIGIN_DISCOVERED, 0);
    } else {
      ns->ns_mark = 0;
    }
  }

  strvec_free(servers);

  for(ns = LIST_FIRST(&nmb_servers); ns != NULL; ns = next) {

    next = LIST_NEXT(ns, ns_link);
    if(ns->ns_mark >= 2)
      ns_destroy(ns);

  }
  hts_mutex_unlock(&nmb_mutex);
}

/**
 *
 */
static void
nmb_udp_input(void *opaque, const void *data, int size,
              const net_addr_t *remote_addr)
{
  if(size < sizeof(nmbpkt_answer_t))
    return;

  const nmbpkt_answer_t *pkt = data;
  uint16_t flags = betoh_16(pkt->h.flags);
  if(!(flags & 0x8000))
    return;
  if(betoh_16(pkt->h.answer_count) < 1)
    return;
  if(betoh_16(pkt->length) != 6)
    return;


  if(pkt->h.transaction_id == nmb_txid) {
    void *a = malloc(4);
    memcpy(a, pkt->addr, 4);
    task_run(query_master_browser, a);
    asyncio_timer_arm_delta_sec(&nmb_flush_timer, 60);
    return;
  }

  nmb_resolve_t *nr;

  LIST_FOREACH(nr, &nmb_resolve_sent, nr_link) {
    if(nr->nr_txid == pkt->h.transaction_id) {
      LIST_REMOVE(nr, nr_link);
      asyncio_timer_disarm(&nr->nr_timeout);
      nr->nr_addr->na_family = 4;
      memcpy(nr->nr_addr->na_addr, pkt->addr, 4);
      hts_mutex_lock(&nmb_mutex);
      nr->nr_status = 0;
      hts_cond_broadcast(&nmb_resolver_cond);
      hts_mutex_unlock(&nmb_mutex);

      break;
    }
  }
}


/**
 *
 */
static uint16_t
nmb_send_query(const char *name, uint8_t type, int dups)
{
  net_addr_t na = {.na_family=4, .na_port=137, .na_addr={0xff,0xff,0xff,0xff}};

  nmbpkt_question_t pkt = {};

  uint16_t txid =  ++nmb_transaction_id_tally;

  pkt.h.transaction_id = txid;
  pkt.h.flags = htobe_16(0x0110);
  pkt.h.question_count = htobe_16(1);
  encode_name(name, pkt.name, type);
  pkt.type = htobe_16(0x20);
  pkt.class = htobe_16(0x01);

  for(int i = 0; i <= dups; i++) {
    asyncio_udp_send(nmb_udp_fd, &pkt, sizeof(pkt), &na);
  }
  return txid;
}


/**
 *
 */
static void
nmb_send_msb_query(void *aux)
{
  asyncio_timer_arm_delta_sec(&nmb_timer, 15);

  nmb_txid = nmb_send_query("\001\002__MSBROWSE__\002\001", 1, 0);
}


/**
 *
 */
int
nmb_resolve(const char *hostname, struct net_addr *na)
{
  nmb_resolve_t nr = {
    .nr_hostname = hostname,
    .nr_addr = na,
    .nr_status = 1
  };

  hts_mutex_lock(&nmb_mutex);

  LIST_INSERT_HEAD(&nmb_resolve_pending, &nr, nr_link);
  asyncio_wakeup_worker(nmb_resolver_signal);

  while(nr.nr_status == 1)
    hts_cond_wait(&nmb_resolver_cond, &nmb_mutex);

  hts_mutex_unlock(&nmb_mutex);
  return nr.nr_status;
}


/**
 *
 */
static void
nmb_resolve_timeout(void *aux)
{
  nmb_resolve_t *nr = aux;

  hts_mutex_lock(&nmb_mutex);
  LIST_REMOVE(nr, nr_link);
  nr->nr_status = -1;
  hts_cond_broadcast(&nmb_resolver_cond);
  hts_mutex_unlock(&nmb_mutex);
}

/**
 *
 */
static void
nmb_resolver_process(void)
{
  nmb_resolve_t *nr;
  hts_mutex_lock(&nmb_mutex);

  while((nr = LIST_FIRST(&nmb_resolve_pending)) != NULL) {
    LIST_REMOVE(nr, nr_link);
    hts_mutex_unlock(&nmb_mutex);
    LIST_INSERT_HEAD(&nmb_resolve_sent, nr, nr_link);
    nr->nr_txid = nmb_send_query(nr->nr_hostname, 0x20, 1);
    asyncio_timer_init(&nr->nr_timeout, nmb_resolve_timeout, nr);
    asyncio_timer_arm_delta_sec(&nr->nr_timeout, 3);
    hts_mutex_lock(&nmb_mutex);
  }
  hts_mutex_unlock(&nmb_mutex);
}


/**
 *
 */
static void
nmb_resolver_init(void)
{
  nmb_resolver_signal = asyncio_add_worker(nmb_resolver_process);
  hts_mutex_init(&nmb_mutex);
  hts_cond_init(&nmb_resolver_cond, &nmb_mutex);
}


INITME(INIT_GROUP_NET, nmb_resolver_init, NULL, 0);

/**
 *
 */
static void
nmb_init(void)
{
  nmb_udp_fd = asyncio_udp_bind("nmb", 0, nmb_udp_input, NULL, 0, 1);

  asyncio_timer_init(&nmb_timer, nmb_send_msb_query, NULL);
  asyncio_timer_init(&nmb_flush_timer, remove_all_servers, NULL);

  nmb_send_msb_query(NULL);
}

INITME(INIT_GROUP_ASYNCIO, nmb_init, NULL, 0);
