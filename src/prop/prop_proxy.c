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
#include "misc/pool.h"

#include <unistd.h>
#include <stdio.h>

/**
 *
 */
struct prop_proxy_connection {
  atomic_t ppc_refcount;
  asyncio_fd_t *ppc_connection;
  char *ppc_url;
  prop_t *ppc_status;
  htsbuf_queue_t ppc_outq;


  struct prop_sub_list ppc_subs;
  int ppc_subscription_tally;

  int ppc_websocket_open;

  websocket_state_t ppc_ws;

  int ppc_port;
};


/**
 *
 */
static void
ppc_del_fd(void *aux)
{
  asyncio_fd_t *fd = aux;
  asyncio_del_fd(fd);
  TRACE(TRACE_DEBUG, "STPP", "Disconnected from server");
}


/**
 *
 */
static prop_proxy_connection_t *
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
static void
ppc_release(prop_proxy_connection_t *ppc)
{
  if(atomic_dec(&ppc->ppc_refcount))
    return;

  if(ppc->ppc_connection != NULL)
    asyncio_run_task(ppc_del_fd, ppc->ppc_connection);
  free(ppc->ppc_ws.packet);
  free(ppc->ppc_url);
  prop_ref_dec(ppc->ppc_status);
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
          ppc->ppc_url, err);

    ppc_disconnect(ppc);
    return;
  }

  snprintf(buf, sizeof(buf),
           "GET /api/stpp HTTP/1.1\r\n"
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
#ifdef PROP_DEBUG
prop_t *
prop_proxy_make0(prop_proxy_connection_t *ppc, uint32_t id, prop_sub_t *s,
                 prop_t *owner, char **pfx, const char *file, int line)
#else
prop_t *
prop_proxy_make(prop_proxy_connection_t *ppc, uint32_t id, prop_sub_t *s,
                 prop_t *owner, char **pfx)
#endif

{
  prop_t *p;
  if(owner != NULL) {
    assert(pfx != NULL);

    LIST_FOREACH(p, &owner->hp_owned, hp_owned_prop_link) {
      for(int i = 0; ; i++) {
        if(pfx[i] == NULL && p->hp_proxy_pfx[i] == NULL) {
          strvec_free(pfx);
          return p;
        }
        if(pfx[i] == NULL || p->hp_proxy_pfx[i] == NULL)
          break;
        if(strcmp(pfx[i], p->hp_proxy_pfx[i]))
          break;
      }
    }
  }

  p = pool_get(prop_pool);
  memset(p, 0, sizeof(prop_t));
#ifdef PROP_DEBUG
  p->hp_magic = PROP_MAGIC;
  p->hp_file = file;
  p->hp_line = line;
  SIMPLEQ_INIT(&p->hp_ref_trace);
#endif

  p->hp_proxy_id = id;

  if(s != NULL) {
    assert(owner == NULL);
    p->hp_owner_sub = s;
    if(RB_INSERT_SORTED_NFL(&s->hps_prop_tree, p, hp_owner_sub_link,
                            prop_id_cmp)) {
#ifdef PROP_DEBUG
      printf("%s called from %s:%d\n", __FUNCTION__, file, line);
#endif
      printf("HELP Unable to insert node %d on sub %d, collision detected\n",
             id, s->hps_proxy_subid);
      abort();
    }
  }

  if(owner != NULL) {
    LIST_INSERT_HEAD(&owner->hp_owned, p, hp_owned_prop_link);
    p->hp_flags |= PROP_PROXY_OWNED_BY_PROP;
    if(owner->hp_flags & PROP_PROXY_FOLLOW_SYMLINK)
      p->hp_flags |= PROP_PROXY_FOLLOW_SYMLINK;
  }

  atomic_set(&p->hp_refcount, 1);
  p->hp_xref = 1;
  p->hp_type = PROP_PROXY;
#if 0
  printf("Created prop @ %d pfx: ", id);
  if(pfx != NULL)
    for(int i = 0; pfx[i]; i++)
      printf("%s ", pfx[i]);
  printf("\n");
#endif
  p->hp_proxy_ppc = ppc_retain(ppc);
  p->hp_proxy_pfx = pfx;
  return p;
}


/**
 *
 */
struct prop *
prop_proxy_create(struct prop *parent, const char *name)
{
  assert(parent->hp_type == PROP_PROXY);
  char **pfx;
  if(parent->hp_proxy_pfx != NULL) {
    int i, len = strvec_len(parent->hp_proxy_pfx);
    pfx = malloc(sizeof(char *) * (len + 2));
    for(i = 0; i < len; i++)
      pfx[i] = strdup(parent->hp_proxy_pfx[i]);
    pfx[i] = strdup(name);
    pfx[i + 1] = NULL;
  } else {
    pfx = NULL;
    strvec_addp(&pfx, name);
  }
  return prop_proxy_make(parent->hp_proxy_ppc, parent->hp_proxy_id, NULL,
                         parent, pfx);
}

/**
 *
 */
void
prop_proxy_destroy(struct prop *p)
{

  prop_t *owned;
  while((owned = LIST_FIRST(&p->hp_owned)) != NULL) {
    assert(owned->hp_flags & PROP_PROXY_OWNED_BY_PROP);

    LIST_REMOVE(owned, hp_owned_prop_link);
    owned->hp_flags &= ~PROP_PROXY_OWNED_BY_PROP;
    prop_destroy0(owned);
  }

  ppc_release(p->hp_proxy_ppc);
  if(p->hp_owner_sub != NULL) {
    RB_REMOVE_NFL(&p->hp_owner_sub->hps_prop_tree, p, hp_owner_sub_link);
    p->hp_owner_sub = NULL;
  }

  if(p->hp_flags & PROP_PROXY_OWNED_BY_PROP) {
    LIST_REMOVE(p, hp_owned_prop_link);
    p->hp_flags &= ~PROP_PROXY_OWNED_BY_PROP;
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
    if(p->hp_owner_sub != NULL) {
      RB_REMOVE_NFL(&s->hps_prop_tree, p, hp_owner_sub_link);
      p->hp_owner_sub = NULL;
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
  prop_t *p = RB_FIND(&s->hps_prop_tree, &skel,
                      hp_owner_sub_link, prop_id_cmp);
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

  // XXX .. this is probably quite slow
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
    if(len < 1)
      break;
    ppc_destroy_props_on_sub(s);
    n = prop_get_notify(s);
    n->hpn_rstring = rstr_allocl((const char *)data + 1, len - 1);
    n->hpn_rstrtype = data[0];
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
      p = prop_proxy_make(ppc, rd32_le(data + i * 4), s, NULL, NULL);
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
      p = prop_proxy_make(ppc, rd32_le(data + 4 + i * 4), s, NULL, NULL);
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

    p = prop_proxy_make(ppc, rd32_le(data), s, NULL, NULL);
    n = prop_get_notify(s);
    n->hpn_event = PROP_ADD_CHILD;
    n->hpn_flags = PROP_ADD_SELECTED;
    n->hpn_prop = prop_ref_inc(p);
    break;

  case STPP_VALUE_PROP:
    if(len != 4)
      break;

    if(s->hps_value_prop != NULL)
      prop_destroy0(s->hps_value_prop);

    s->hps_value_prop = prop_proxy_make(ppc, rd32_le(data), NULL, NULL, NULL);
    n = prop_get_notify(s);
    n->hpn_event = PROP_VALUE_PROP;
    n->hpn_prop = prop_ref_inc(s->hps_value_prop);
    break;

  case STPP_HAVE_MORE_CHILDS_YES:
    n = prop_get_notify(s);
    n->hpn_event = PROP_HAVE_MORE_CHILDS_YES;
    break;

  case STPP_HAVE_MORE_CHILDS_NO:
    n = prop_get_notify(s);
    n->hpn_event = PROP_HAVE_MORE_CHILDS_NO;
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
static int
ppc_ws_input(void *opaque, int opcode, uint8_t *data, int len)
{
  prop_proxy_connection_t *ppc = opaque;

  if(opcode != 2)
    return 1;

  if(len < 1)
    return 1;
  switch(data[0]) {
  case STPP_CMD_NOTIFY:
    ppc_ws_input_notify(ppc, data + 1, len - 1);
    break;
  }
  return 0;
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

  if(websocket_parse(q, ppc_ws_input, ppc, &ppc->ppc_ws))
    ppc_disconnect(ppc);
}


/**
 *
 */
static void
ppc_connect(void *aux, int status, const void *data)
{
  prop_proxy_connection_t *ppc = aux;
  assert(ppc->ppc_connection == NULL);

  net_addr_t na;
  switch(status) {
  case ASYNCIO_DNS_STATUS_COMPLETED:
    na = *(const net_addr_t *)data;
    break;

  case ASYNCIO_DNS_STATUS_FAILED:
    return;

  default:
    abort();
  }


  na.na_port = ppc->ppc_port;
  TRACE(TRACE_DEBUG, "STPP", "Connecting to %s -> %s",
        ppc->ppc_url, net_addr_str(&na));

  ppc->ppc_connection = asyncio_connect("stppclient", &na,
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
prop_proxy_connect(const char *url, prop_t *status)
{
  prop_proxy_connection_t *ppc;
  ppc = calloc(1, sizeof(prop_proxy_connection_t));
  htsbuf_queue_init(&ppc->ppc_outq, 0);

  char protostr[64];
  char hostname[256];

  url_split(protostr, sizeof(protostr), NULL, 0,
	    hostname, sizeof(hostname),
            &ppc->ppc_port, NULL, 0, url);

  if(ppc->ppc_port == -1)
    ppc->ppc_port = 42000;

  ppc->ppc_url = strdup(url);
  ppc->ppc_status = prop_ref_inc(status);
  asyncio_dns_lookup_host(hostname, ppc_connect, ppc_retain(ppc));

  return prop_proxy_make(ppc, 0 /* global */, NULL, NULL, NULL);
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
static void
prop_proxy_send_queue(prop_proxy_connection_t *ppc, htsbuf_queue_t *q)
{
  if(TAILQ_FIRST(&ppc->ppc_outq.hq_q) == NULL)
    asyncio_run_task(ppc_send, ppc_retain(ppc));

  websocket_append_hdr(&ppc->ppc_outq, 2, q->hq_size);
  htsbuf_appendq(&ppc->ppc_outq, q);
}

/**
 *
 */
static void
prop_proxy_send_prop(prop_t *p, htsbuf_queue_t *q)
{
  assert(p->hp_type == PROP_PROXY);

  htsbuf_append_le32(q, p->hp_proxy_id);

  char **pfx = p->hp_proxy_pfx;
  if(pfx != NULL) {
    for(int i = 0; pfx[i] != NULL; i++) {
      int len = strlen(pfx[i]);
      assert(len < 256);
      htsbuf_append_byte(q, len);
      htsbuf_append(q, pfx[i], len);
    }
  }

  htsbuf_append_byte(q, 0);
}


/**
 *
 */
static void
prop_proxy_send_str(const char *str, htsbuf_queue_t *q)
{
  int len = strlen(str);
  if(len >= 0xff) {
    htsbuf_append_byte(q, 0xff);
    htsbuf_append_le32(q, len);
  } else {
    htsbuf_append_byte(q, len);
  }
  htsbuf_append(q, str, len);
}


/**
 *
 */
void
prop_proxy_send_event(prop_t *p, const event_t *e)
{
  htsbuf_queue_t q;
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;

  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_EVENT);
  prop_proxy_send_prop(p, &q);

  htsbuf_append_byte(&q, e->e_type);
  switch(e->e_type) {
  case EVENT_ACTION_VECTOR:
    {
      const event_action_vector_t *eav = (const event_action_vector_t *)e;
      for(int i = 0; i < eav->num; i++) {
        const char *a = action_code2str(eav->actions[i]);
        int len = strlen(a);
        assert(len < 256);
        htsbuf_append_byte(&q, len);
        htsbuf_append(&q, a, len);
      }
    }
    break;
  case EVENT_DYNAMIC_ACTION:
    {
      const event_payload_t *ep = (const event_payload_t *)e;
      htsbuf_append(&q, ep->payload, strlen(ep->payload));
    }
    break;
  case EVENT_OPENURL:
    {
      const event_openurl_t *eo = (const event_openurl_t *)e;
      uint8_t flags = 0;

      flags |= eo->url          ? 0x01 : 0;
      flags |= eo->view         ? 0x02 : 0;
      flags |= eo->item_model   ? 0x04 : 0;
      flags |= eo->parent_model ? 0x08 : 0;
      flags |= eo->how          ? 0x10 : 0;
      flags |= eo->parent_url   ? 0x20 : 0;
      htsbuf_append_byte(&q, flags);

      if(eo->url != NULL)
        prop_proxy_send_str(eo->url, &q);

      if(eo->view != NULL)
        prop_proxy_send_str(eo->view, &q);

      if(eo->item_model != NULL)
        prop_proxy_send_prop(eo->item_model, &q);

      if(eo->parent_model != NULL)
        prop_proxy_send_prop(eo->parent_model, &q);

      if(eo->how != NULL)
        prop_proxy_send_str(eo->how, &q);

      if(eo->parent_url != NULL)
        prop_proxy_send_str(eo->parent_url, &q);
    }
    break;

  case EVENT_PLAYTRACK:
    {
      const event_playtrack_t *ep = (event_playtrack_t *)e;

      uint8_t flags = 0;

      flags |= ep->source       ? 0x01 : 0;
      htsbuf_append_byte(&q, flags);

      prop_proxy_send_prop(ep->track, &q);

      if(ep->source != NULL)
        prop_proxy_send_prop(ep->source, &q);

      htsbuf_append_byte(&q, ep->mode);
    }
    break;

  default:
    printf("Can't serialize event %d\n", e->e_type);
    htsbuf_queue_flush(&q);
    return;
  }
  prop_proxy_send_queue(ppc, &q);
}


/**
 *
 */
void
prop_proxy_req_move(struct prop *p, struct prop *before)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;

  uint8_t buf[9] = {STPP_CMD_REQ_MOVE};

  assert(p->hp_type == PROP_PROXY);
  if(before != NULL)
    assert(before->hp_type == PROP_PROXY);

  wr32_le(buf + 1, p->hp_proxy_id);
  if(before == NULL) {
    prop_proxy_send_data(ppc, buf, 5);
  } else {
    wr32_le(buf + 5, before->hp_proxy_id);
    prop_proxy_send_data(ppc, buf, 9);
  }
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
  RB_INIT_NFL(&s->hps_prop_tree);
  s->hps_value_prop = NULL;

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

  if(s->hps_value_prop != NULL) {
    prop_destroy0(s->hps_value_prop);
    s->hps_value_prop = NULL;
  }

  ppc_destroy_props_on_sub(s);
  LIST_REMOVE(s, hps_value_prop_link);
  uint8_t *data = alloca(5);
  data[0] = STPP_CMD_UNSUBSCRIBE;
  wr32_le(data + 1, s->hps_proxy_subid);
  prop_proxy_send_data(ppc, data, 5);
}


/**
 *
 */
void
prop_proxy_link(struct prop *src, struct prop *dst)
{
  printf("%s not implemeted\n", __FUNCTION__);
}


/**
 *
 */
void
prop_proxy_set_string(struct prop *p, const char *str, int type)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;
  htsbuf_queue_t q;

  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_SET);
  prop_proxy_send_prop(p, &q);
  htsbuf_append_byte(&q, STPP_SET_STRING);
  htsbuf_append_byte(&q, type);
  htsbuf_append(&q, str, strlen(str));
  prop_proxy_send_queue(ppc, &q);
}


/**
 *
 */
void
prop_proxy_set_float(struct prop *p, float v)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;
  htsbuf_queue_t q;

  union {
    float f;
    uint32_t u32;
  } u;
  u.f = v;
  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_SET);
  prop_proxy_send_prop(p, &q);
  htsbuf_append_byte(&q, STPP_SET_FLOAT);
  htsbuf_append_le32(&q, u.u32);
  prop_proxy_send_queue(ppc, &q);
}


/**
 *
 */
void
prop_proxy_set_int(struct prop *p, int v)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;
  htsbuf_queue_t q;
  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_SET);
  prop_proxy_send_prop(p, &q);
  htsbuf_append_byte(&q, STPP_SET_INT);
  htsbuf_append_le32(&q, v);
  prop_proxy_send_queue(ppc, &q);
}


/**
 *
 */
void
prop_proxy_add_int(struct prop *p, int v)
{
  printf("%s not implemeted\n", __FUNCTION__);
}



/**
 *
 */
void
prop_proxy_toggle_int(struct prop *p)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;
  htsbuf_queue_t q;
  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_SET);
  prop_proxy_send_prop(p, &q);
  htsbuf_append_byte(&q, STPP_TOGGLE_INT);
  prop_proxy_send_queue(ppc, &q);
}


/**
 *
 */
void
prop_proxy_want_more_childs(struct prop_sub *s)
{
  prop_proxy_connection_t *ppc = s->hps_ppc;

  uint8_t *data = alloca(5);
  data[0] = STPP_CMD_WANT_MORE_CHILDS;
  wr32_le(data + 1, s->hps_proxy_subid);
  prop_proxy_send_data(ppc, data, 5);
}


/**
 *
 */
void
prop_proxy_set_void(struct prop *p)
{
  prop_proxy_connection_t *ppc = p->hp_proxy_ppc;
  htsbuf_queue_t q;
  htsbuf_queue_init(&q, 0);
  htsbuf_append_byte(&q, STPP_CMD_SET);
  prop_proxy_send_prop(p, &q);
  htsbuf_append_byte(&q, STPP_SET_VOID);
  prop_proxy_send_queue(ppc, &q);
}


#ifdef POOL_DEBUG

static void
prop_proxy_check_items(void *ptr, void *pc)
{
  prop_t *p = ptr;
  if(p->hp_type != PROP_PROXY)
    return;
  printf("Item %p  created by %s:%d\n", p, p->hp_file, p->hp_line);
}


void prop_proxy_check(void);

void
prop_proxy_check(void)
{
  printf("Scanning prop pool\n");
  pool_foreach(prop_pool, prop_proxy_check_items, NULL);
}

#endif
