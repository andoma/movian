/*
 *  Copyright (C) 2007-2016 Lonelycoder AB
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
#include <unistd.h>
#include <assert.h>

#include <libavutil/base64.h>

#include "main.h"
#include "ecmascript.h"
#include "task.h"
#include "networking/asyncio.h"
#include "networking/websocket.h"
#include "networking/http.h"
#include "networking/http_server.h"
#include "misc/str.h"
#include "misc/str.h"
#include "misc/bytestream.h"
#include "arch/arch.h"

/**
 *
 */
typedef struct es_websocket_client {
  es_resource_t super;
  asyncio_fd_t *ewc_connection;
  task_group_t *ewc_task_group;

  char *ewc_hostname;
  char *ewc_path;
  int ewc_port;

  void *ewc_tlsctx;

  char *ewc_protocol;

  htsbuf_queue_t ewc_outq;

  websocket_state_t ewc_ws;

  asyncio_dns_req_t *ewc_dns_lookup;

  int ewc_state;
#define EWC_CONNECTING 0
#define EWC_CONNECTED  1
#define EWC_CLOSING    2
#define EWC_CLOSED     3

  int ewc_http_linecnt;
  int ewc_http_status;

  char *ewc_status_str; // Current status, used for failure reporting

  asyncio_timer_t ewc_timer;

  int ewc_alive;

} es_websocket_client_t;


/**
 *
 */
typedef struct es_websocket_client_xfer_task {
  es_websocket_client_t *ewc;
  int opcode;
  void *buf;
  size_t bufsize;
} es_websocket_client_xfer_task_t;


/**
 *
 */
typedef struct es_websocket_client_close_task {
  es_websocket_client_t *ewc;
  int status;
  char *msg;
} es_websocket_client_close_task_t;


/**
 *
 */
static void
es_websocket_client_close_task_fn(void *aux)
{
  es_websocket_client_close_task_t *t = aux;
  es_websocket_client_t *ewc = t->ewc;

  es_context_t *ec = ewc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);

  if(!ewc->super.er_zombie) {
    es_push_root(ctx, ewc);

    duk_get_prop_string(ctx, -1, "onClose");
    duk_push_int(ctx, t->status);
    duk_push_string(ctx, t->msg);
    int rc = duk_pcall(ctx, 2);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);
  }

  es_context_end(ec, 1, ctx);
  free(t->msg);
  free(t);
  es_resource_release(&ewc->super);
}


/**
 *
 */
static void
es_websocket_client_close(es_websocket_client_t *ewc,
                          int statuscode, const char *statusmsg)
{
  es_websocket_client_close_task_t *t =
    malloc(sizeof(es_websocket_client_close_task_t));

  es_resource_retain(&ewc->super);

  t->ewc = ewc;
  t->msg = statusmsg ? strdup(statusmsg) : NULL;
  t->status = statuscode;
  task_run_in_group(es_websocket_client_close_task_fn, t, ewc->ewc_task_group);
}



/**
 *
 */
static void
es_websocket_client_connect_task_fn(void *aux)
{
  es_websocket_client_t *ewc = aux;

  es_context_t *ec = ewc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);

  if(!ewc->super.er_zombie) {
    es_push_root(ctx, ewc);

    duk_get_prop_string(ctx, -1, "onConnect");
    int rc = duk_pcall(ctx, 0);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);
  }

  es_context_end(ec, 1, ctx);
  es_resource_release(&ewc->super);
}



/**
 *
 */
static void
es_websocket_client_input_task_fn(void *aux)
{
  es_websocket_client_xfer_task_t *t = aux;
  es_websocket_client_t *ewc = t->ewc;

  es_context_t *ec = ewc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);

  if(!ewc->super.er_zombie) {
    es_push_root(ctx, ewc);

    duk_get_prop_string(ctx, -1, "onInput");

    if(t->opcode == 2) {

      void *ptr = duk_push_fixed_buffer(ctx, t->bufsize);
      memcpy(ptr, t->buf, t->bufsize);
      duk_push_buffer_object(ctx, -1, 0, t->bufsize, DUK_BUFOBJ_ARRAYBUFFER);
      duk_swap_top(ctx, -1);
      duk_pop(ctx);

    } else {
      duk_push_lstring(ctx, t->buf, t->bufsize);
    }

    int rc = duk_pcall(ctx, 1);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);
  }

  es_context_end(ec, 0, ctx);
  es_resource_release(&ewc->super);

  free(t->buf);
  free(t);
}


/**
 *
 */
static void
es_websocket_client_net_destroy(void *aux)
{
  es_websocket_client_t *ewc = aux;
  if(ewc->ewc_connection != NULL) {
    asyncio_del_fd(ewc->ewc_connection);
    ewc->ewc_connection = NULL;
  }

  if(ewc->ewc_dns_lookup != NULL) {
    asyncio_dns_cancel(ewc->ewc_dns_lookup);
    es_resource_release(&ewc->super); // DNS lookup held a refcount
    ewc->ewc_dns_lookup = NULL;
  }

  asyncio_timer_disarm(&ewc->ewc_timer);

  free(ewc->ewc_ws.packet);
  ewc->ewc_ws.packet = NULL;

  htsbuf_queue_flush(&ewc->ewc_outq);

  if(ewc->ewc_tlsctx != NULL) {
    asyncio_ssl_free(ewc->ewc_tlsctx);
    ewc->ewc_tlsctx = NULL;
  }

  es_resource_release(&ewc->super);
}







/**
 *
 */
static void
es_websocket_client_send_task_fn(void *aux)
{
  es_websocket_client_xfer_task_t *t = aux;
  es_websocket_client_t *ewc = t->ewc;

  if(ewc->ewc_connection != NULL) {
    htsbuf_queue_t q;
    htsbuf_queue_init(&q, 0);
    websocket_append(&q, t->opcode, t->buf, t->bufsize, &ewc->ewc_ws);
    asyncio_sendq(ewc->ewc_connection, &q, 0);
  }

  es_resource_release(&ewc->super);
  free(t->buf);
  free(t);
}




/**
 *
 */
static void
es_websocket_client_destroy(es_resource_t *eres)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)eres;
  es_resource_retain(eres);
  asyncio_run_task(es_websocket_client_net_destroy, eres);

  es_root_unregister(eres->er_ctx->ec_duk, ewc);
  es_resource_unlink(eres);
}


/**
 *
 */
static void
es_websocket_client_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)eres;
  snprintf(dst, dstsize, "%s:%d%s", ewc->ewc_hostname,
           ewc->ewc_port, ewc->ewc_path);
}



static void
es_websocket_client_finalizer(es_resource_t *eres)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)eres;
  free(ewc->ewc_hostname);
  free(ewc->ewc_path);
  free(ewc->ewc_status_str);
  task_group_destroy(ewc->ewc_task_group);
}


/**
 *
 */
static const es_resource_class_t es_resource_websocket_client = {
  .erc_name = "websocket_client",
  .erc_size = sizeof(es_websocket_client_t),
  .erc_destroy = es_websocket_client_destroy,
  .erc_info = es_websocket_client_info,
  .erc_finalizer = es_websocket_client_finalizer,
};





/**
 *
 */
static int
es_websocket_client_input_ws(void *opaque, int opcode, uint8_t *data, int len)
{
  es_websocket_client_t *ewc = opaque;
  htsbuf_queue_t q;
  es_websocket_client_xfer_task_t *t;

  switch(opcode) {
  case 1: // Text
  case 2:
    t = malloc(sizeof(es_websocket_client_xfer_task_t));
    es_resource_retain(&ewc->super);
    t->ewc = ewc;
    t->buf = malloc(len);
    t->bufsize = len;
    t->opcode = opcode;
    memcpy(t->buf, data, len);

    task_run_in_group(es_websocket_client_input_task_fn, t,
                      ewc->ewc_task_group);
    break;

  case 9: // PING -> Send PONG
    htsbuf_queue_init(&q, 0);
    websocket_append_hdr(&q, 10, len, NULL);
    htsbuf_append(&q, data, len);
    asyncio_sendq(ewc->ewc_connection, &q, 0);
    break;

  case 10:
    ewc->ewc_alive = 1;
    break;
  default:
    break;
  }

  return 0;
}


/**
 *
 */
static int
es_websocket_client_input_http(es_websocket_client_t *ewc, htsbuf_queue_t *q)
{
  while(1) {
    char *line = http_read_line(q);
    if(line == NULL)
      return 0;
    if(line == (void *)-1) {
      mystrset(&ewc->ewc_status_str, "Long line");
      return -1;
    }
    if(ewc->ewc_http_linecnt == 0) {
      if(strncmp(line, "HTTP/1.1 ", 9))
        goto bad;
      mystrset(&ewc->ewc_status_str, line + 9);
      ewc->ewc_http_status = atoi(line + 9);
    } else if(*line == 0) {
      // Last line

      if(ewc->ewc_http_status == 101) {
        ewc->ewc_alive = 1;
        ewc->ewc_state = EWC_CONNECTED;
        free(line);

        es_resource_retain(&ewc->super);
        task_run_in_group(es_websocket_client_connect_task_fn, ewc,
                          ewc->ewc_task_group);

        return 0;
      } else {
        goto bad;
      }

    } else {

    }

    ewc->ewc_http_linecnt++;

    free(line);
    continue;
  bad:
    free(line);
    return -1;
  }
}


/**
 *
 */
static void
es_websocket_client_input(void *opaque, htsbuf_queue_t *q)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)opaque;
  int r = 0;
  switch(ewc->ewc_state) {
  case EWC_CONNECTING:
    r = es_websocket_client_input_http(ewc, q);
    if(r || ewc->ewc_state == EWC_CONNECTING)
      break;
    // FALLTHRU
  case EWC_CONNECTED:
    if(websocket_parse(q, es_websocket_client_input_ws, ewc, &ewc->ewc_ws)) {
      mystrset(&ewc->ewc_status_str, "Websocket error");
      r = 1;
    } else {
      r = 0;
    }
    break;

  default:
    return;
  }
  if(r) {
    es_websocket_client_close(ewc, WS_STATUS_ABNORMAL_CLOSE,
                              ewc->ewc_status_str);
    es_websocket_client_net_destroy(ewc); // Will release the ref we have
  }
}


/**
 *
 */
static void
es_websocket_timeout(void *aux)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)aux;
  if(!ewc->ewc_alive) {
    es_websocket_client_close(ewc, WS_STATUS_ABNORMAL_CLOSE, "Timeout");
    es_websocket_client_net_destroy(ewc); // Will release the ref we have
    return;
  }

  ewc->ewc_alive = 0;
  htsbuf_queue_t q;
  htsbuf_queue_init(&q, 0);
  websocket_append_hdr(&q, 9, 4, NULL);
  uint32_t payload = 0;
  htsbuf_append(&q, &payload, 4);
  asyncio_sendq(ewc->ewc_connection, &q, 0);
  asyncio_timer_arm_delta_sec(&ewc->ewc_timer, 20);
}


/**
 *
 */
static void
es_websocket_client_connected(void *aux, const char *err)
{
  es_websocket_client_t *ewc = (es_websocket_client_t *)aux;

  if(err != NULL) {
    es_websocket_client_close(ewc, WS_STATUS_ABNORMAL_CLOSE, err);
    es_websocket_client_net_destroy(ewc); // Will release the ref we have
    return;
  }

  char buf[1024];

  uint8_t nonce[16];
  arch_get_random_bytes(nonce, sizeof(nonce));
  char key[32];
  av_base64_encode(key, sizeof(key), nonce, sizeof(nonce));


  snprintf(buf, sizeof(buf),
           "GET %s HTTP/1.1\r\n"
           "Host: %s\r\n"
           "Connection: Upgrade\r\n"
           "Upgrade: websocket\r\n"
           "Sec-WebSocket-Version: 13\r\n"
           "Sec-WebSocket-Key: %s\r\n",
           ewc->ewc_path, ewc->ewc_hostname, key);
  asyncio_send(ewc->ewc_connection, buf, strlen(buf), 1);
  asyncio_send(ewc->ewc_connection, "\r\n", 2, 0);

  asyncio_timer_init(&ewc->ewc_timer, es_websocket_timeout, ewc);
  asyncio_timer_arm_delta_sec(&ewc->ewc_timer, 20);
}

/**
 * Deal with DNS lookup result
 */
static void
es_websocket_client_connect(void *opaque, int dns_lookup_status,
                            const void *data)
{
  es_websocket_client_t *ewc = opaque;
  ewc->ewc_dns_lookup = NULL;
  if(dns_lookup_status == ASYNCIO_DNS_STATUS_COMPLETED) {
    net_addr_t na = *(net_addr_t *)data;
    na.na_port = ewc->ewc_port;

    // Reference we hold is passed on to socket
    ewc->ewc_connection =
      asyncio_connect("ecmascript/websocket", &na,
                      es_websocket_client_connected,
                      es_websocket_client_input,
                      opaque, 60000,
                      ewc->ewc_tlsctx,
                      ewc->ewc_hostname);
  } else {

    es_websocket_client_close(ewc, WS_STATUS_ABNORMAL_CLOSE, data);
    es_resource_release(&ewc->super);

  }
}

/**
 *
 */
static int
es_websocket_client_create(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  const char *url = duk_safe_to_string(ctx, 0);
  const char *proto = duk_get_string(ctx, 1);

  es_websocket_client_t *ewc = es_resource_alloc(&es_resource_websocket_client);
  ewc->ewc_task_group = task_group_create();
  prng_init2(&ewc->ewc_ws.maskgen);

  es_resource_link(&ewc->super, ec, 1);

  ewc->ewc_protocol = proto ? strdup(proto) : NULL;
  es_root_register(ctx, 2, ewc);

  htsbuf_queue_init(&ewc->ewc_outq, 0);

  char protostr[64];
  char hostname[256];
  char path[1024];
  int port = -1;

  url_split(protostr, sizeof(protostr), NULL, 0,
	    hostname, sizeof(hostname),
            &port, path, sizeof(path), url);

  if(port == -1) {
    if(!strcmp(protostr, "wss"))
      port = 443;
    else
      port = 80;
  }

  if(!strcmp(protostr, "wss")) {
    ewc->ewc_tlsctx = asyncio_ssl_create_client();
  }

  ewc->ewc_hostname = strdup(hostname);
  ewc->ewc_path = strdup(path);
  ewc->ewc_port = port;

  es_resource_retain(&ewc->super); // for DNS lookup
  ewc->ewc_dns_lookup =
    asyncio_dns_lookup_host(ewc->ewc_hostname,
                            es_websocket_client_connect,
                            ewc);

  es_resource_push(ctx, &ewc->super);
  return 1;
}

/**
 *
 */
static int
es_websocket_client_send(duk_context *ctx)
{
  es_websocket_client_t *ewc =
    es_resource_get(ctx, 0, &es_resource_websocket_client);

  duk_size_t bufsize;
  const void *buf;
  int opcode;
  buf = duk_get_buffer_data(ctx, 1, &bufsize);
  if(buf != NULL) {
    opcode = 2; // binary
  } else {
    buf = duk_to_string(ctx, 1);
    bufsize = strlen(buf);
    opcode = 1; // text
  }

  es_websocket_client_xfer_task_t *t =
    malloc(sizeof(es_websocket_client_xfer_task_t));

  t->opcode = opcode;
  t->buf = malloc(bufsize);
  memcpy(t->buf, buf, bufsize);
  t->bufsize = bufsize;

  es_resource_retain(&ewc->super);
  t->ewc = ewc;

  asyncio_run_task(es_websocket_client_send_task_fn, t);
  return 0;
}




/**
 *
 */
typedef struct es_websocket_server {
  es_resource_t super;

  http_path_t *ews_path;

  char *ews_pathstr;

} es_websocket_server_t;



/**
 *
 */
static void
es_websocket_server_destroy(es_resource_t *eres)
{
  es_websocket_server_t *ews = (es_websocket_server_t *)eres;

  http_path_remove(ews->ews_path);
  ews->ews_path = NULL;

  es_root_unregister(eres->er_ctx->ec_duk, ews);
  es_resource_unlink(eres);
}


/**
 *
 */
static void
es_websocket_server_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_websocket_server_t *ews = (es_websocket_server_t *)eres;
  snprintf(dst, dstsize, "%s", ews->ews_pathstr);
}



static void
es_websocket_server_finalizer(es_resource_t *eres)
{
  es_websocket_server_t *ews = (es_websocket_server_t *)eres;
  free(ews->ews_pathstr);
}


/**
 *
 */
static const es_resource_class_t es_resource_websocket_server = {
  .erc_name = "websocket_server",
  .erc_size = sizeof(es_websocket_server_t),
  .erc_destroy = es_websocket_server_destroy,
  .erc_info = es_websocket_server_info,
  .erc_finalizer = es_websocket_server_finalizer,
};


/**
 *
 */
typedef struct es_websocket_server_connection {
  es_resource_t super;

  es_websocket_server_t *ewsc_server;

  http_connection_t *ewsc_hc; // May only be accessed on asyncio thread

  task_group_t *ewsc_task_group;

} es_websocket_server_connection_t;




/**
 *
 */
static void
es_websocket_server_connection_destroy(es_resource_t *eres)
{
  es_websocket_server_connection_t *ewsc =
    (es_websocket_server_connection_t *)eres;

  es_root_unregister(eres->er_ctx->ec_duk, ewsc);

  es_resource_release(&ewsc->ewsc_server->super);
  ewsc->ewsc_server = NULL;

  es_resource_unlink(eres);
}


/**
 *
 */
static void
es_websocket_server_connection_info(es_resource_t *eres,
                                    char *dst, size_t dstsize)
{
  es_websocket_server_connection_t *ewsc =
    (es_websocket_server_connection_t *)eres;
  es_websocket_server_t *ews = ewsc->ewsc_server;
  snprintf(dst, dstsize, "%s", ews->ews_pathstr);
}


/**
 *
 */
static void
es_websocket_server_connection_finalizer(es_resource_t *eres)
{
  es_websocket_server_connection_t *ewsc =
    (es_websocket_server_connection_t *)eres;

  task_group_destroy(ewsc->ewsc_task_group);
}



/**
 *
 */
static const es_resource_class_t es_resource_websocket_server_connection = {
  .erc_name = "websocket_server_connection",
  .erc_size = sizeof(es_websocket_server_connection_t),
  .erc_destroy = es_websocket_server_connection_destroy,
  .erc_info = es_websocket_server_connection_info,
  .erc_finalizer = es_websocket_server_connection_finalizer,
};


/**
 *
 */
typedef struct es_websocket_server_connection_task {
  es_websocket_server_connection_t *ewsc;
  int opcode;
  void *buf;
  size_t bufsize;
} es_websocket_server_connection_task_t;


/**
 *
 */
static es_websocket_server_connection_task_t *
ewsc_maketask(es_websocket_server_connection_t *ewsc, int opcode)
{
  es_websocket_server_connection_task_t *t =
    calloc(1, sizeof(es_websocket_server_connection_task_t));

  es_resource_retain(&ewsc->super);
  t->ewsc = ewsc;
  t->opcode = opcode;
  return t;
}


/**
 *
 */
static void
ewsc_freetask(es_websocket_server_connection_task_t *t)
{
  es_resource_release(&t->ewsc->super);
  free(t->buf);
  free(t);
}


/**
 *
 */
static void
ews_removed(void *opaque)
{
  es_resource_release(opaque);
}

/**
 *
 */
static void
ewsc_send_task_fn(void *aux)
{
  es_websocket_server_connection_task_t *t = aux;
  es_websocket_server_connection_t *ewsc = t->ewsc;

  if(ewsc->ewsc_hc != NULL)
    websocket_send(ewsc->ewsc_hc, t->opcode, t->buf, t->bufsize);

  ewsc_freetask(t);
}



static int
ewsc_send(duk_context *ctx)
{
  duk_push_this(ctx);

  es_websocket_server_connection_t *ewsc =
    es_resource_get(ctx, -1, &es_resource_websocket_server_connection);

  duk_size_t bufsize;
  const void *buf;
  int opcode;
  buf = duk_get_buffer_data(ctx, 0, &bufsize);
  if(buf != NULL) {
    opcode = 2; // binary
  } else {
    buf = duk_to_string(ctx, 0);
    bufsize = strlen(buf);
    opcode = 1; // text
  }

  es_websocket_server_connection_task_t *t = ewsc_maketask(ewsc, opcode);
  t->buf = malloc(bufsize);
  memcpy(t->buf, buf, bufsize);
  t->bufsize = bufsize;

  asyncio_run_task(ewsc_send_task_fn, t);
  return 0;
}


/**
 *
 */
static int
ewsc_close(duk_context *ctx)
{
  duk_push_this(ctx);
  es_websocket_server_connection_t *ewsc =
    es_resource_get(ctx, -1, &es_resource_websocket_server_connection);
  es_websocket_server_connection_task_t *t = ewsc_maketask(ewsc, 8);

  int code = duk_is_number(ctx, 0) ? duk_to_int(ctx, 0) : 1001;
  const void *msg = NULL;
  duk_size_t msgsize = 0;
  if(duk_is_string(ctx, 1)) {
    msg = duk_to_string(ctx, 1);
    msgsize = strlen(msg);
  }

  t->bufsize = 2 + msgsize;
  t->buf = malloc(t->bufsize);
  wr16_be(t->buf, code);
  memcpy(t->buf + 2, msg, msgsize);

  asyncio_run_task(ewsc_send_task_fn, t);
  return 0;
}


/**
 *
 */
static void
es_websocket_server_connection_open_fn(void *aux)
{
  es_websocket_server_connection_task_t *t = aux;
  es_websocket_server_connection_t *ewsc = t->ewsc;
  es_context_t *ec = ewsc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);

  es_websocket_server_t *ews = ewsc->ewsc_server;
  if(ews != NULL && !ews->super.er_zombie) {
    es_push_root(ctx, ews);
    duk_get_prop_string(ctx, -1, "onOpen");

    int objidx = es_resource_push(ctx, &ewsc->super);

    es_root_register(ctx, objidx, ewsc);

    duk_push_c_lightfunc(ctx, ewsc_send, 1, 0, 0);
    duk_put_prop_string(ctx, objidx, "send");

    duk_push_c_lightfunc(ctx, ewsc_close, 2, 0, 0);
    duk_put_prop_string(ctx, objidx, "close");

    int rc = duk_pcall(ctx, 1);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);
  }
  ewsc_freetask(t);
  es_context_end(ec, 1, ctx);
}


/**
 *
 */
static int
ews_connected(http_connection_t *hc, void *path_opaque)
{
  es_websocket_server_t *ews = path_opaque;
  es_context_t *ec = ews->super.er_ctx;

  duk_context *ctx = es_context_begin(ec);

  es_websocket_server_connection_t *ewsc =
    es_resource_alloc(&es_resource_websocket_server_connection);

  ewsc->ewsc_task_group = task_group_create();

  es_resource_link(&ewsc->super, ec, 0);

  // Link to server definition
  es_resource_retain(&ews->super);
  ewsc->ewsc_server = ews;

  // Take on reference owned by http connection (released by ews_disconnected)
  es_resource_retain(&ewsc->super);
  ewsc->ewsc_hc = hc;
  http_set_opaque(hc, ewsc);

  task_run_in_group(es_websocket_server_connection_open_fn,
                    ewsc_maketask(ewsc, 0), ewsc->ewsc_task_group);

  es_context_end(ec, 1, ctx);
  return 0;
}


/**
 *
 */
static void
es_websocket_server_connection_close_fn(void *aux)
{
  es_websocket_server_connection_task_t *t = aux;
  es_websocket_server_connection_t *ewsc = t->ewsc;
  es_context_t *ec = ewsc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);
  if(!ewsc->super.er_zombie) {
    es_push_root(ctx, ewsc);
    duk_get_prop_string(ctx, -1, "onClose");
    int rc = duk_pcall(ctx, 0);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);

  }
  ewsc_freetask(t);
  es_resource_destroy(&ewsc->super);
  es_context_end(ec, 1, ctx);
}


/**
 *
 */
static void
ews_disconnected(http_connection_t *hc, void *connection_opaque)
{
  es_websocket_server_connection_t *ewsc = connection_opaque;

  task_run_in_group(es_websocket_server_connection_close_fn,
                    ewsc_maketask(ewsc, 0), ewsc->ewsc_task_group);

  es_resource_release(&ewsc->super);
  ewsc->ewsc_hc = NULL;
}


/**
 *
 */
static void
es_websocket_server_connection_input_fn(void *aux)
{
  es_websocket_server_connection_task_t *t = aux;
  es_websocket_server_connection_t *ewsc = t->ewsc;
  es_context_t *ec = ewsc->super.er_ctx;
  duk_context *ctx = es_context_begin(ec);

  if(!ewsc->super.er_zombie) {
    es_push_root(ctx, ewsc);
    duk_get_prop_string(ctx, -1, "onInput");

    if(t->opcode == 2) {

      void *ptr = duk_push_fixed_buffer(ctx, t->bufsize);
      memcpy(ptr, t->buf, t->bufsize);
      duk_push_buffer_object(ctx, -1, 0, t->bufsize, DUK_BUFOBJ_ARRAYBUFFER);
      duk_swap_top(ctx, -1);
      duk_pop(ctx);

    } else {
      duk_push_lstring(ctx, t->buf, t->bufsize);
    }

    int rc = duk_pcall(ctx, 1);
    if(rc)
      es_dump_err(ctx);
    duk_pop(ctx);

  }
  ewsc_freetask(t);
  es_context_end(ec, 0, ctx);
}


/**
 *
 */
static int
ews_input(http_connection_t *hc, int opcode,
          uint8_t *data, size_t len, void *connection_opaque)
{
  es_websocket_server_connection_t *ewsc = connection_opaque;
  es_websocket_server_connection_task_t *t = ewsc_maketask(ewsc, opcode);
  // data is always nul terminated with an extra allocated byte
  // not visible in 'len', pass that on
  t->buf = malloc(len + 1);
  memcpy(t->buf, data, len + 1);
  t->bufsize = len;
  task_run_in_group(es_websocket_server_connection_input_fn,
                    t, ewsc->ewsc_task_group);

  return 0;
}




/**
 *
 */
static int
es_websocket_server_create(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  const char *path = duk_safe_to_string(ctx, 0);

  es_websocket_server_t *ews = es_resource_alloc(&es_resource_websocket_server);

  es_resource_link(&ews->super, ec, 1);
  ews->ews_pathstr = strdup(path);

  // Take on reference owned by http server (released by ews_removed)
  es_resource_retain(&ews->super);
  ews->ews_path = http_add_websocket(path, ews,
                                     ews_connected,
                                     ews_input,
                                     ews_disconnected,
                                     ews_removed);
  es_root_register(ctx, 1, ews);
  es_resource_push(ctx, &ews->super);
  return 1;
}




static const duk_function_list_entry fnlist_websocket[] = {
  { "clientCreate",  es_websocket_client_create, 3 },
  { "clientSend",    es_websocket_client_send, 2 },

  { "serverCreate",  es_websocket_server_create, 2 },

  { NULL, NULL, 0}
};

ES_MODULE("websocket", fnlist_websocket);
