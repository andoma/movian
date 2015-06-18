
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
#include "misc/str.h"

#include <unistd.h>

/**
 *
 */
struct prop_proxy_connection {
  atomic_t ppc_refcount;
  asyncio_fd_t *ppc_connection;
  net_addr_t ppc_addr;
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
  free(ppc->ppc_ws.packet);
  free(ppc);
}


/**
 *
 */
static void
ppc_sendq(prop_proxy_connection_t *ppc)
{
  hts_mutex_lock(&prop_mutex);
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
static int
prop_id_cmp(const prop_t *a, const prop_t *b)
{
  return a->hp_proxy_id - b->hp_proxy_id;
}


/**
 *
 */
prop_t *
prop_proxy_make(prop_proxy_connection_t *ppc, uint32_t id, prop_sub_t *s,
                char **pfx)
{
  prop_t *p = pool_get(prop_pool);
  memset(p, 0, sizeof(prop_t));
#ifdef PROP_DEBUG
  p->hp_magic = PROP_MAGIC;
  SIMPLEQ_INIT(&p->hp_ref_trace);
#endif

  p->hp_proxy_id = id;

  if(s != NULL) {
    p->hp_sub = s;
    if(RB_INSERT_SORTED(&s->hps_prop_tree, p, hp_sub_link, prop_id_cmp)) {
      printf("HELP Unable to insert node %d, collision detected\n", id);
      pool_put(prop_pool, p);
      strvec_free(pfx);
      return NULL;
    }
  }

  atomic_set(&p->hp_refcount, 1);
  p->hp_xref = 1;
  p->hp_type = PROP_PROXY;
  p->hp_proxy_ppc = ppc_retain(ppc);
  p->hp_proxy_pfx = pfx;
  return p;
}


/**
 *
 */
void
prop_proxy_destroy(struct prop *p)
{
  ppc_release(p->hp_proxy_ppc);
  if(p->hp_sub != NULL) {
    RB_REMOVE(&p->hp_sub->hps_prop_tree, p, hp_sub_link);
    p->hp_sub = NULL;
  }
  strvec_free(p->hp_proxy_pfx);
}


/**
 *
 */
static void
ppc_destroy_props_on_sub(prop_sub_t *s)
{
  prop_t *p;
  while((p = s->hps_prop_tree.root) != NULL) {
    if(p->hp_sub != NULL) {
      RB_REMOVE(&s->hps_prop_tree, p, hp_sub_link);
      p->hp_sub = NULL;
    }
    prop_destroy0(p);
  }
}


/**
 *
 */
static prop_t *
ppc_find_prop_on_sub(prop_sub_t *s, int id)
{
  prop_t skel;
  skel.hp_proxy_id = id;
  prop_t *p = RB_FIND(&s->hps_prop_tree, &skel, hp_sub_link, prop_id_cmp);
  assert(p != NULL);
  return p;
}


/**
 *
 */
static void
ppc_ws_input_notify(prop_proxy_connection_t *ppc, const uint8_t *data, int len)
{
  prop_vec_t *pv;
  union {
    float f;
    int i;
  } u;
  int cnt;
  prop_sub_t *s;
  prop_t *p;
  
  if(len < 5)
    return;

  int setop = data[0];
  int subid = rd32_le(data + 1);

  hts_mutex_lock(&prop_mutex);

  // XXX .. this is slow
  LIST_FOREACH(s, &ppc->ppc_subs, hps_value_prop_link) {
    if(s->hps_proxy_subid == subid)
      break;
  }

  if(s == NULL) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  prop_notify_t *n = NULL;

  data += 5;
  len -= 5;
  switch(setop) {
  case STPP_SET_STRING:
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_rstring = rstr_allocl((const char *)data, len);
    n->hpn_rstrtype = 0;
    n->hpn_event = PROP_SET_RSTRING;
    break;

  case STPP_SET_VOID:
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_event = PROP_SET_VOID;
    break;

  case STPP_SET_DIR:
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_event = PROP_SET_DIR;
    break;

  case STPP_SET_INT:
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_event = PROP_SET_INT;
    n->hpn_int = len == 4 ? rd32_le(data) : 0;
    break;

  case STPP_SET_FLOAT:
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_event = PROP_SET_FLOAT;
    u.i = len == 4 ? rd32_le(data) : 0;
    n->hpn_float = u.f;
    break;

  case STPP_ADD_CHILDS:
    if(len & 3)
      return;
    cnt = len / 4;
    pv = prop_vec_create(cnt);
    for(int i = 0; i < cnt; i++) {
      p = prop_proxy_make(ppc, rd32_le(data + i * 4), s, NULL);
      pv = prop_vec_append(pv, p);
    }
    n = prop_get_notify(s);
    n->hpn_event = PROP_ADD_CHILD_VECTOR;
    n->hpn_propv = pv;
    break;

  case STPP_ADD_CHILDS_BEFORE:
    if(len & 3 || len == 0)
      return;
    cnt = len / 4 - 1;
    pv = prop_vec_create(cnt);
    for(int i = 0; i < cnt; i++) {
      p = prop_proxy_make(ppc, rd32_le(data + 4 + i * 4), s, NULL);
      pv = prop_vec_append(pv, p);
    }

    n = prop_get_notify(s);
    n->hpn_event = PROP_ADD_CHILD_VECTOR_BEFORE;
    n->hpn_propv = pv;
    n->hpn_prop_extra = prop_ref_inc(ppc_find_prop_on_sub(s, rd32_le(data)));
    break;

  case STPP_DEL_CHILD:
    if(len != 4)
      break;
    n = prop_get_notify(s);
    n->hpn_event = PROP_DEL_CHILD;
    p = ppc_find_prop_on_sub(s, rd32_le(data));
    n->hpn_prop = prop_ref_inc(p);
    prop_destroy0(p);
    break;

  case STPP_MOVE_CHILD:
    if(len < 4)
      break;
    n = prop_get_notify(s);
    n->hpn_event = PROP_MOVE_CHILD;
    n->hpn_prop = prop_ref_inc(ppc_find_prop_on_sub(s, rd32_le(data)));
    if(len == 8)
      n->hpn_prop_extra =
        prop_ref_inc(ppc_find_prop_on_sub(s, rd32_le(data + 4)));
    else
      n->hpn_prop_extra = NULL;
    break;

  case STPP_SELECT_CHILD:
    if(len != 4)
      break;
    n = prop_get_notify(s);
    n->hpn_event = PROP_SELECT_CHILD;
    n->hpn_prop = prop_ref_inc(ppc_find_prop_on_sub(s, rd32_le(data)));
    n->hpn_prop_extra = NULL;
    break;

  case STPP_ADD_CHILD_SELECTED:
    if(len != 4)
      break;

    p = prop_proxy_make(ppc, rd32_le(data), s, NULL);
    n = prop_get_notify(s);
    n->hpn_event = PROP_ADD_CHILD;
    n->hpn_flags = PROP_ADD_SELECTED;
    n->hpn_prop = prop_ref_inc(p);
    break;


  default:
    printf("WARNING: STPP input can't handle op %d\n", setop);
    break;
  }

  if(n != NULL)
    prop_courier_enqueue(s, n);
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

  return prop_proxy_make(ppc, 0 /* global */, NULL, NULL);
}


/**
 *
 */
static void
prop_proxy_send_data(prop_proxy_connection_t *ppc,
                     const uint8_t *data, int len)
{
  if(TAILQ_FIRST(&ppc->ppc_outq.hq_q) == NULL)
    asyncio_run_task(ppc_send, ppc_retain(ppc));

  websocket_append_hdr(&ppc->ppc_outq, 2, len);
  htsbuf_append(&ppc->ppc_outq, data, len);
}


/**
 *
 */
void
prop_proxy_send_event(prop_t *p, event_t *e)
{
  printf("prop proxy event sending not supported yet\n");
}


/**
 *
 */
void
prop_proxy_subscribe(prop_proxy_connection_t *ppc, prop_sub_t *s,
                     prop_t *p, const char **name)
{
  assert(p->hp_type == PROP_PROXY);

  s->hps_ppc = ppc;
  RB_INIT(&s->hps_prop_tree);
  LIST_INSERT_HEAD(&ppc->ppc_subs, s, hps_value_prop_link);

  ppc->ppc_subscription_tally++;
  s->hps_proxy_subid = ppc->ppc_subscription_tally;

  int datalen = 11;
  char **pfx = p->hp_proxy_pfx;
  if(pfx != NULL) {
    assert(p->hp_flags & PROP_PROXY_FOLLOW_SYMLINK);
    for(int i = 0; pfx[i] != NULL; i++) {
      int len = strlen(pfx[i]);
      assert(len < 256);
      datalen += 1 + len;
    }
  }

  if(name != NULL) {
    for(int i = 0; name[i] != NULL; i++) {
      int len = strlen(name[i]);
      assert(len < 256);
      datalen += 1 + len;
    }
  }

  uint8_t *data = alloca(datalen);
  data[0] = STPP_CMD_SUBSCRIBE;
  wr32_le(data + 1, s->hps_proxy_subid);
  wr32_le(data + 5, p->hp_proxy_id);
  wr16_le(data + 9, s->hps_flags);

  uint8_t *ptr = data + 11;

  if(pfx != NULL) {
    for(int i = 0; pfx[i] != NULL; i++) {
      int len = strlen(pfx[i]);
      *ptr++ = len;
      memcpy(ptr, pfx[i], len);
      ptr += len;
    }
  }
  if(name != NULL) {
    for(int i = 0; name[i] != NULL; i++) {
      int len = strlen(name[i]);
      *ptr++ = len;
      memcpy(ptr, name[i], len);
      ptr += len;
    }
  }

  //  hexdump("SUBSEND", data, datalen);
  prop_proxy_send_data(ppc, data, datalen);
}


/**
 *
 */
void
prop_proxy_unsubscribe(prop_sub_t *s)
{
  prop_proxy_connection_t *ppc = s->hps_ppc;
  LIST_REMOVE(s, hps_value_prop_link);
  uint8_t *data = alloca(5);
  data[1] = STPP_CMD_UNSUBSCRIBE;
  wr32_le(data + 1, s->hps_proxy_subid);
  prop_proxy_send_data(ppc, data, 5);
}

#if 0
/**
 *
 */
static void
test_callback(void *opaque, prop_event_t event, ...)
{
  printf("Event %d\n", event);
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
                 PROP_TAG_CALLBACK, test_callback, NULL,
                 PROP_TAG_NAMED_ROOT, p, "remote",
                 PROP_TAG_NAMESTR, "remote.navigators.current.pages",
                 NULL);
}

static void
proxy_test(void)
{
  if(1)task_run(test_task, NULL);
}

INITME(INIT_GROUP_API, proxy_test, NULL);

#endif
