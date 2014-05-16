/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
RB_HEAD(stpp_prop_tree, stpp_prop);
LIST_HEAD(stpp_prop_list, stpp_prop);

/**
 *
 */
typedef struct stpp {
  http_connection_t *stpp_hc;
  struct stpp_subscription_tree stpp_subscriptions;
  struct stpp_prop_tree stpp_props;
  int stpp_prop_tally;
} stpp_t;


/**
 * A subscription as created by the STPP client
 */
typedef struct stpp_subscription {
  RB_ENTRY(stpp_subscription) ss_link;
  unsigned int ss_id;
  prop_sub_t *ss_sub;
  stpp_t *ss_stpp;
  struct stpp_prop_list ss_props; // Exported props
} stpp_subscription_t;

static int
ss_cmp(const stpp_subscription_t *a, const stpp_subscription_t *b)
{
  return a->ss_id - b->ss_id;
}


/**
 * An exported property
 */
typedef struct stpp_prop {
  RB_ENTRY(stpp_prop) sp_link;
  unsigned int sp_id;
  prop_t *sp_prop;
  stpp_subscription_t *sp_sub;
  LIST_ENTRY(stpp_prop) sp_sub_link;
} stpp_prop_t;

static int
sp_cmp(const stpp_prop_t *a, const stpp_prop_t *b)
{
  return a->sp_id - b->sp_id;
}


/**
 *
 */
static stpp_prop_t *
stpp_property_export_from_sub(stpp_subscription_t *ss, prop_t *p)
{
  stpp_t *stpp = ss->ss_stpp;
  stpp_prop_t *sp = malloc(sizeof(stpp_prop_t));
  sp->sp_id = ++stpp->stpp_prop_tally;
  sp->sp_prop = prop_ref_inc(p);
  sp->sp_sub = ss;
  LIST_INSERT_HEAD(&ss->ss_props, sp, sp_sub_link);
  if(RB_INSERT_SORTED(&stpp->stpp_props, sp, sp_link, sp_cmp))
    abort();
  prop_tag_set(p, ss, sp);
  return sp;
}


/**
 *
 */
static void
stpp_property_unexport_from_sub(stpp_subscription_t *ss, stpp_prop_t *sp)
{
  stpp_t *stpp = ss->ss_stpp;
  prop_ref_dec(sp->sp_prop);
  LIST_REMOVE(sp, sp_sub_link);
  RB_REMOVE(&stpp->stpp_props, sp, sp_link);
  free(sp);
}


/**
 *
 */
static prop_t *
resolve_propref(stpp_t *stpp, int propref)
{
  stpp_prop_t skel, *sp;
  if(propref == 0)
    return prop_get_global();
  skel.sp_id = propref;

  if((sp = RB_FIND(&stpp->stpp_props, &skel, sp_link, sp_cmp)) == NULL) {
    TRACE(TRACE_ERROR, "STPP", "Referring unknown propref %d", propref);
    return NULL;
  }
  return sp->sp_prop;
}


/**
 *
 */
static void
stpp_sub_json_add_child(stpp_subscription_t *ss, http_connection_t *hc,
			prop_t *p, prop_t *before)
{ 
  char buf2[128];
  unsigned int b = before ? ((stpp_prop_t *)prop_tag_get(before, ss))->sp_id:0;
  stpp_prop_t *sp = stpp_property_export_from_sub(ss, p);
  snprintf(buf2, sizeof(buf2), "[5,%u,%u,[%u]]", ss->ss_id, b, sp->sp_id);
  websocket_send(hc, 1, buf2, strlen(buf2));
}


/**
 *
 */
static void
stpp_sub_json_add_childs(stpp_subscription_t *ss, http_connection_t *hc,
			 prop_vec_t *pv, prop_t *before)
{ 
  unsigned int b = before ? ((stpp_prop_t *)prop_tag_get(before, ss))->sp_id:0;
  int i;
  htsbuf_queue_t hq;

  htsbuf_queue_init(&hq, 0);
  htsbuf_qprintf(&hq, "[5,%u,%u,[", ss->ss_id, b);

  for(i = 0; i < prop_vec_len(pv); i++) {
    prop_t *p = prop_vec_get(pv, i);
    stpp_prop_t *sp = stpp_property_export_from_sub(ss, p);
    htsbuf_qprintf(&hq, "%s%u", i ? "," : "", sp->sp_id);
  }
  htsbuf_append(&hq, "]]", 1);
  websocket_sendq(hc, 1, &hq);
}



/**
 *
 */
static void
stpp_sub_json_del_child(stpp_subscription_t *ss, http_connection_t *hc,
			prop_t *p)
{ 
  stpp_prop_t *sp = prop_tag_clear(p, ss);
  char buf2[128];
  snprintf(buf2, sizeof(buf2), "[6,%u,[%u]]", ss->ss_id, sp->sp_id);
  websocket_send(hc, 1, buf2, strlen(buf2));
  stpp_property_unexport_from_sub(ss, sp);
}



/**
 *
 */
static void
stpp_sub_json_move_child(stpp_subscription_t *ss, http_connection_t *hc,
			 prop_t *p, prop_t *before)
{ 
  stpp_prop_t *sp =          prop_tag_get(p, ss);
  stpp_prop_t *b =  before ? prop_tag_get(before, ss) : NULL;
  char buf2[128];
  snprintf(buf2, sizeof(buf2), "[7,%u,%u,%u]", ss->ss_id, sp->sp_id,
	   b ? b->sp_id : 0);
  websocket_send(hc, 1, buf2, strlen(buf2));
}


/**
 *
 */
static void
ss_clear_props(stpp_subscription_t *ss)
{
  stpp_prop_t *sp;
  while((sp = LIST_FIRST(&ss->ss_props)) != NULL) {
    prop_tag_clear(sp->sp_prop, ss);
    stpp_property_unexport_from_sub(ss, sp);
  }
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
  prop_t *p1;
  prop_vec_t *pv;
  const char *str, *str2;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_FLOAT:
    my_double2str(buf, sizeof(buf), va_arg(ap, double));
    snprintf(buf2, sizeof(buf2), "[4,%u,%s]", ss->ss_id, buf);
    websocket_send(hc, 1, buf2, strlen(buf2));
    ss_clear_props(ss);
    break;

  case PROP_SET_INT:
    snprintf(buf2, sizeof(buf2), "[4,%u,%d]", ss->ss_id, va_arg(ap, int));
    websocket_send(hc, 1, buf2, strlen(buf2));
    ss_clear_props(ss);
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
    ss_clear_props(ss);
    break;

  case PROP_SET_VOID:
    snprintf(buf2, sizeof(buf2), "[4,%u,null]", ss->ss_id);
    websocket_send(hc, 1, buf2, strlen(buf2));
    ss_clear_props(ss);
    break;

  case PROP_SET_RLINK:
    str = rstr_get(va_arg(ap, rstr_t *));
    str2 = rstr_get(va_arg(ap, rstr_t *));
    htsbuf_queue_init(&hq, 0);
    htsbuf_qprintf(&hq, "[4,%u,[\"link\",", ss->ss_id);
    htsbuf_append_and_escape_jsonstr(&hq, str);
    htsbuf_append(&hq, ",", 1);
    htsbuf_append_and_escape_jsonstr(&hq, str2);
    htsbuf_append(&hq, "]]", 2);
    websocket_sendq(hc, 1, &hq);
    ss_clear_props(ss);
    break;

  case PROP_SET_DIR:
    snprintf(buf2, sizeof(buf2), "[4,%u,[\"dir\"]]", ss->ss_id);
    websocket_send(hc, 1, buf2, strlen(buf2));
    break;

  case PROP_ADD_CHILD:
    stpp_sub_json_add_child(ss, hc, va_arg(ap, prop_t *), NULL);
    break;
  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    stpp_sub_json_add_child(ss, hc, p1, va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD_VECTOR:
    stpp_sub_json_add_childs(ss, hc, va_arg(ap, prop_vec_t *), NULL);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    stpp_sub_json_add_childs(ss, hc, pv, va_arg(ap, prop_t *));
    break;

  case PROP_DEL_CHILD:
    stpp_sub_json_del_child(ss, hc, va_arg(ap, prop_t *));
    break;

  case PROP_MOVE_CHILD:
    p1 = va_arg(ap, prop_t *);
    stpp_sub_json_move_child(ss, hc, p1, va_arg(ap, prop_t *));
    break;

  default:
    printf("stpp_sub_json() can't deal with event %d\n", event);
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

  prop_t *p = resolve_propref(stpp, propref);
  stpp_subscription_t *ss = calloc(1, sizeof(stpp_subscription_t));

  ss->ss_id = id;
  if(RB_INSERT_SORTED(&stpp->stpp_subscriptions, ss, ss_link, ss_cmp)) {
    // ID Collision
    TRACE(TRACE_ERROR, "STPP", "Subscription ID %d already exist", id);
    free(ss);
    return;
  }

  ss->ss_stpp = stpp;
  ss->ss_sub = prop_subscribe(PROP_SUB_ALT_PATH,
			      PROP_TAG_COURIER, asyncio_courier,
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
  ss_clear_props(ss);
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
  
  if((ss = RB_FIND(&stpp->stpp_subscriptions, &s, ss_link, ss_cmp)) == NULL)
    return;
  ss_destroy(stpp, ss);
}


/**
 *
 */
static void
stpp_cmd_set(stpp_t *stpp, int propref, const char *path, htsmsg_field_t *v)
{
  if(path == NULL || v == NULL)
    return;

  prop_t *p = resolve_propref(stpp, propref);
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
  int cmd = htsmsg_get_u32_or_default(m, HTSMSG_INDEX(0), 0);
  
  switch(cmd) {
  case STPP_CMD_SUBSCRIBE:
    stpp_cmd_sub(stpp,
		 htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0),
		 htsmsg_get_u32_or_default(m, HTSMSG_INDEX(2), 0),
		 htsmsg_get_str(m,            HTSMSG_INDEX(3)));
    break;

  case STPP_CMD_UNSUBSCRIBE:
    stpp_cmd_unsub(stpp,
		   htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0));
    break;

  case STPP_CMD_SET:
    stpp_cmd_set(stpp,
		 htsmsg_get_u32_or_default(m, HTSMSG_INDEX(1), 0),
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
    htsmsg_release(m);
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

  assert(stpp->stpp_props.root == NULL);

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
