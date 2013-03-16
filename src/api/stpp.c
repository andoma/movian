/*
 *  STPP - Showtime Property Protocol
 *  Copyright (C) 2013 Andreas Öman
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
 */

#include <stdio.h>
#include <assert.h>

#include "networking/http_server.h"
#include "htsmsg/htsmsg_json.h"
#include "misc/str.h"
#include "prop/prop.h"
#include "misc/redblack.h"
#include "misc/dbl.h"


#define STPP_CMD_SUBSCRIBE   1
#define STPP_CMD_UNSUBSCRIBE 2
#define STPP_CMD_SET         3
#define STPP_CMD_NOTIFY      4

RB_HEAD(stpp_subscription_tree, stpp_subscription);

/**
 *
 */
typedef struct stpp {
  http_connection_t *stpp_hc;
  struct stpp_subscription_tree stpp_subscriptions;
} stpp_t;


/**
 *
 */
typedef struct stpp_subscription {
  RB_ENTRY(stpp_subscription) ss_link;
  unsigned int ss_id;
  prop_sub_t *ss_sub;
  stpp_t *ss_stpp;
} stpp_subscription_t;


/**
 *
 */
static int
ss_cmp(const stpp_subscription_t *a, const stpp_subscription_t *b)
{
  return a->ss_id - b->ss_id;
}


/**
 * Hardwired JSON output
 */
static void
stpp_sub_json(void *opaque, prop_event_t event, ...)
{
  stpp_subscription_t *ss = opaque;
  http_connection_t *hc = ss->ss_stpp->stpp_hc;
  va_list ap;
  htsbuf_queue_t hq;
  char buf[64];
  char buf2[128];
  va_start(ap, event);
  const char *str, *str2;

  switch(event) {
  default:

  case PROP_SET_FLOAT:
    my_double2str(buf, sizeof(buf), va_arg(ap, double));
    snprintf(buf2, sizeof(buf2), "[4,%u,%s]", ss->ss_id, buf);
    websocket_send(hc, 1, buf2, strlen(buf2));
    break;

  case PROP_SET_INT:
    snprintf(buf2, sizeof(buf2), "[4,%u,%d]", ss->ss_id, va_arg(ap, int));
    websocket_send(hc, 1, buf2, strlen(buf2));
    break;

  case PROP_SET_RSTRING:
    str = rstr_get(va_arg(ap, rstr_t *));
    if(0)
  case PROP_SET_CSTRING:
      str = va_arg(ap, const char *);

    htsbuf_queue_init(&hq, 0);
    htsbuf_qprintf(&hq, "[4,%u,", ss->ss_id);
    htsbuf_append_and_escape_jsonstr(&hq, str);
    htsbuf_append(&hq, "]", 1);
    websocket_sendq(hc, 1, &hq);
    break;

  case PROP_SET_VOID:
    snprintf(buf2, sizeof(buf2), "[4,%u,null]", ss->ss_id);
    websocket_send(hc, 1, buf2, strlen(buf2));
    break;

  case PROP_SET_RLINK:
    str = rstr_get(va_arg(ap, rstr_t *));
    str2 = rstr_get(va_arg(ap, rstr_t *));
    htsbuf_queue_init(&hq, 0);
    htsbuf_qprintf(&hq, "[4,%u,[\"link\",", ss->ss_id);
    htsbuf_append_and_escape_jsonstr(&hq, str);
    htsbuf_append(&hq, ",", 1);
    htsbuf_append_and_escape_jsonstr(&hq, str2);
    htsbuf_append(&hq, "]", 1);
    websocket_sendq(hc, 1, &hq);
    break;
  }
  va_end(ap);
}


/**
 *
 */
static void
stpp_cmd_sub(stpp_t *stpp, unsigned int id, int propref, const char *path)
{
  if(path == NULL)
    return;

  prop_t *p = prop_get_global();
  stpp_subscription_t *ss = calloc(1, sizeof(stpp_subscription_t));

  ss->ss_id = id;
  if(RB_INSERT_SORTED(&stpp->stpp_subscriptions, ss, ss_link, ss_cmp)) {
    // ID Collision
    TRACE(TRACE_ERROR, "STPP", "Subscription ID %d already exist", id);
    free(ss);
    return;
  }

  ss->ss_stpp = stpp;
  ss->ss_sub = prop_subscribe(PROP_SUB_DIRECT_UPDATE | PROP_SUB_ALT_PATH,
			      PROP_TAG_COURIER, http_get_courier(stpp->stpp_hc),
			      PROP_TAG_NAMESTR, path,
			      PROP_TAG_CALLBACK, stpp_sub_json, ss,
			      PROP_TAG_ROOT, p,
			      NULL);
}


/**
 *
 */
static void
ss_destroy(stpp_t *stpp, stpp_subscription_t *ss)
{
  prop_unsubscribe(ss->ss_sub);
  RB_REMOVE(&stpp->stpp_subscriptions, ss, ss_link);
  free(ss);
}


/**
 *
 */
static void
stpp_cmd_unsub(stpp_t *stpp, unsigned int id)
{
  stpp_subscription_t s, *ss;
  s.ss_id = id;
  
  if((ss = RB_FIND(&stpp->stpp_subscriptions, &s, ss_link, ss_cmp)) == NULL) {
    TRACE(TRACE_ERROR, "STPP", "Unsubscribing unknown subscription ID %d", id);
    return;
  }
  ss_destroy(stpp, ss);
}


/**
 *
 */
static void
stpp_cmd_set(int propref, const char *path, htsmsg_field_t *v)
{
  if(path == NULL || v == NULL)
    return;

  prop_t *p = prop_get_global();
  switch(v->hmf_type) {
  case HMF_S64:
    prop_setdn(NULL, p, path, PROP_SET_INT, v->hmf_s64);
    break;
  case HMF_STR:
    prop_setdn(NULL, p, path, PROP_SET_STRING, v->hmf_str);
    break;
  case HMF_DBL:
    prop_setdn(NULL, p, path, PROP_SET_FLOAT, v->hmf_dbl);
    break;
  }
}


/**
 *
 */
static void
stpp_json(stpp_t *stpp, htsmsg_t *m)
{
  htsmsg_print(m);
  int cmd = htsmsg_get_u32_or_default(m, HTSMSG_INDEX(0), 0);
  
  switch(cmd) {
  case STPP_CMD_SUBSCRIBE:
    stpp_cmd_sub(stpp,
		 htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0),
		 htsmsg_get_u32_or_default(m, HTSMSG_INDEX(2), 0),
		 htsmsg_get_str(m,            HTSMSG_INDEX(3)));
    break;

  case STPP_CMD_UNSUBSCRIBE:
    stpp_cmd_unsub(stpp, htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0));
    break;

  case STPP_CMD_SET:
    stpp_cmd_set(htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0),
		 htsmsg_get_str(m, HTSMSG_INDEX(2)),
		 htsmsg_field_find(m, HTSMSG_INDEX(3)));
    break;
  }
}

/**
 *
 */
static int
stpp_input(http_connection_t *hc, int opcode, 
	   uint8_t *data, size_t len, void *opaque)
{
  stpp_t *stpp = opaque;
  
  if(opcode != 1)
    return 0;

  htsmsg_t *m = htsmsg_json_deserialize((const char *)data);
  if(m != NULL) {
    stpp_json(stpp, m);
    htsmsg_destroy(m);
  }
  return 0;
}


/**
 *
 */
static int
stpp_init(http_connection_t *hc)
{
  if(!gconf.enable_experimental)
    return 403;

  stpp_t *stpp = calloc(1, sizeof(stpp_t));
  stpp->stpp_hc = hc;
  http_set_opaque(hc, stpp);
  return 0;
}


/**
 *
 */
static void
stpp_fini(http_connection_t *hc, void *opaque)
{
  stpp_t *stpp = opaque;
  
  while(stpp->stpp_subscriptions.root != NULL)
    ss_destroy(stpp, stpp->stpp_subscriptions.root);

  free(stpp);
}


/**
 *
 */
static void
ws_init(void)
{
  http_add_websocket("/showtime/stpp", stpp_init, stpp_input, stpp_fini);
}


INITME(INIT_GROUP_API, ws_init);
