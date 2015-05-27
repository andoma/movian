
#include "misc/bytestream.h"
#include "arch/atomic.h"
#include "networking/asyncio.h"
#include "networking/websocket.h"
#include "networking/http.h"
#include "htsmsg/htsbuf.h"
#include "prop_proxy.h"
#include "prop_i.h"
#include "task.h"
#include "api/stpp.h"

#include <unistd.h>

struct prop_proxy_connection {
  atomic_t ppc_refcount;
  asyncio_fd_t *ppc_connection;
  net_addr_t ppc_addr;
  //  hts_mutex_t ppc_mutex;
  htsbuf_queue_t ppc_outq;

  struct prop_sub_list ppc_subs;
  int ppc_subscription_tally;

  int ppc_websocket_open;

  websocket_state_t ppc_ws;

};

/**
 *
 */
static void
ppc_del_fd(void *aux)
{
  asyncio_fd_t *fd = aux;
  asyncio_del_fd(fd);
}


/**
 *
 */
prop_proxy_connection_t *
ppc_retain(prop_proxy_connection_t *ppc)
{
  atomic_inc(&ppc->ppc_refcount);
  return ppc;
}


/**
 *
 */
static void
ppc_disconnect(prop_proxy_connection_t *ppc)
{
  asyncio_del_fd(ppc->ppc_connection);
  ppc->ppc_connection = NULL;
  ppc->ppc_websocket_open = 0;
  // Umh.. Reconnect ?
}


/**
 *
 */
void
ppc_release(prop_proxy_connection_t *ppc)
{
  if(atomic_dec(&ppc->ppc_refcount))
    return;

  if(ppc->ppc_connection != NULL)
    asyncio_run_task(ppc_del_fd, ppc->ppc_connection);
  //  hts_mutex_destroy(&ppc->ppc_mutex);
  free(ppc->ppc_ws.packet);
  free(ppc);
}


/**
 *
 */
prop_t *
prop_proxy_make(prop_proxy_connection_t *ppc, uint32_t id)
{
  prop_t *p = prop_make(NULL, 0, NULL);
  p->hp_type = PROP_PROXY;
  p->hp_proxy_ppc = ppc_retain(ppc);
  p->hp_proxy_id = id;
  return p;
}


/**
 *
 */
static void
ppc_sendq(prop_proxy_connection_t *ppc)
{
  hts_mutex_lock(&prop_mutex);
  printf("Sending data\n");
  htsbuf_hexdump(&ppc->ppc_outq, "PPC OUT");
  asyncio_sendq(ppc->ppc_connection, &ppc->ppc_outq, 0);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
ppc_connected(void *aux, const char *err)
{
  prop_proxy_connection_t *ppc = aux;
  char buf[1024];
  if(err != NULL) {
    TRACE(TRACE_ERROR, "REMOTE", "Unable to connect to %s -- %s",
          net_addr_str(&ppc->ppc_addr), err);

    ppc_disconnect(ppc);
    return;
  }

  snprintf(buf, sizeof(buf),
           "GET /showtime/stpp HTTP/1.1\r\n"
           "Connection: Upgrade\r\n"
           "Upgrade: websocket\r\n"
           "Sec-WebSocket-Key: 1\r\n"
           "\r\n");
  asyncio_send(ppc->ppc_connection, buf, strlen(buf), 0);
}


/**
 *
 */
static void
ppc_ws_input_notify(prop_proxy_connection_t *ppc, const uint8_t *data, int len)
{
  prop_sub_t *s;
  if(len < 5)
    return;
  int setop = data[0];
  int subid = rd32_le(data + 1);

  hts_mutex_lock(&prop_mutex);

  LIST_FOREACH(s, &ppc->ppc_subs, hps_value_prop_link) {
    if(s->hps_proxy_subid == subid)
      break;
  }

  if(s == NULL) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  data += 5;
  len -= 5;
  switch(setop) {
  case STPP_SET_STRING:
    break;

  }

  hts_mutex_unlock(&prop_mutex);

}

/**
 *
 */
static void
ppc_ws_input(void *opaque, int opcode, uint8_t *data, int len)
{
  prop_proxy_connection_t *ppc = opaque;

  if(opcode != 2)
    return;

  if(len < 1)
    return;
  switch(data[0]) {
  case STPP_CMD_NOTIFY:
    ppc_ws_input_notify(ppc, data + 1, len - 1);
    break;
  }

  
}

/**
 *
 */
static void
ppc_input(void *opaque, htsbuf_queue_t *q)
{
  prop_proxy_connection_t *ppc = opaque;
  while(ppc->ppc_websocket_open == 0) {
    char *line = http_read_line(q);
    if(line == NULL)
      return; // Not full line yet
    if(line == (void *)-1) {
      ppc_disconnect(ppc);
      return;
    }
    if(*line == 0) {
      ppc->ppc_websocket_open = 1;
      ppc_sendq(ppc);
      // Done
    }
    free(line);
  }

  while(1) {
    int r = websocket_parse(q, ppc_ws_input, ppc, &ppc->ppc_ws);
    if(r == 1) { // Bad data
      ppc_disconnect(ppc);
      break;
    }
    if(r == 0)
      break;
  }
}


/**
 *
 */
static void
ppc_connect(void *aux)
{
  prop_proxy_connection_t *ppc = aux;
  assert(ppc->ppc_connection == NULL);
  printf("Connecting to %s\n", net_addr_str(&ppc->ppc_addr));
  ppc->ppc_connection = asyncio_connect("stppclient", &ppc->ppc_addr,
                                        ppc_connected, ppc_input, ppc, 3000);
  ppc_release(ppc);
}


/**
 *
 */
static void
ppc_send(void *aux)
{
  prop_proxy_connection_t *ppc = aux;

  if(ppc->ppc_websocket_open)
    ppc_sendq(ppc);

  ppc_release(ppc);
}


/**
 *
 */
prop_t *
prop_proxy_connect(net_addr_t *addr)
{
  prop_proxy_connection_t *ppc;
  ppc = calloc(1, sizeof(prop_proxy_connection_t));
  htsbuf_queue_init(&ppc->ppc_outq, 0);
  ppc->ppc_addr = *addr;
  asyncio_run_task(ppc_connect, ppc_retain(ppc));

  return prop_proxy_make(ppc, 0 /* global */);
}



/**
 *
 */
void
prop_proxy_subscribe(prop_proxy_connection_t *ppc, prop_sub_t *s,
                     prop_t *p, const char **name)
{
  printf("proxy subx\n");
  s->hps_proxy = 1;

  s->hps_ppc = ppc;
  LIST_INSERT_HEAD(&ppc->ppc_subs, s, hps_value_prop_link);

  ppc->ppc_subscription_tally++;
  s->hps_proxy_subid = ppc->ppc_subscription_tally;

  int datalen = 9;
  for(int i = 0; name[i] != NULL; i++) {
    int len = strlen(name[i]);
    assert(len < 256);
    datalen += 1 + len;
  }

  uint8_t *data = alloca(datalen);
  data[0] = 1; // SUBSCRIBE
  wr32_le(data + 1, s->hps_proxy_subid);
  wr32_le(data + 5, p->hp_proxy_id);

  uint8_t *ptr = data + 9;
  for(int i = 0; name[i] != NULL; i++) {
    int len = strlen(name[i]);
    *ptr++ = len;
    memcpy(ptr, name[i], len);
    ptr += len;
  }
  hexdump("PROXYSUB", data, datalen);

  if(TAILQ_FIRST(&ppc->ppc_outq.hq_q) == NULL)
    asyncio_run_task(ppc_send, ppc_retain(ppc));

  websocket_append_hdr(&ppc->ppc_outq, 2, datalen);
  htsbuf_append(&ppc->ppc_outq, data, datalen);
}

static void
test_callback(void *opaque, rstr_t *str)
{
  printf("test is %s\n", rstr_get(str));
}

static void
test_task(void *aux)
{
  sleep(1);
  printf("Doing proxy stuff\n");

  net_addr_t na;
  net_resolve("localhost", &na, NULL);
  na.na_port = 42000;
  prop_t *p = prop_proxy_connect(&na);
  prop_print_tree(p, 1);
  
  prop_subscribe(0,
                 PROP_TAG_CALLBACK_RSTR, test_callback, NULL,
                 PROP_TAG_NAMED_ROOT, p, "remote",
                 PROP_TAG_NAMESTR, "remote.app.copyright",
                 NULL);
}

static void
proxy_test(void)
{
  if(0)task_run(test_task, NULL);
}

INITME(INIT_GROUP_API, proxy_test, NULL);
