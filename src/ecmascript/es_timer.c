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
#include <assert.h>
#include "main.h"
#include "compiler.h"
#include "ecmascript.h"

LIST_HEAD(es_timer_list, es_timer);

typedef struct es_timer {
  es_resource_t super;
  LIST_ENTRY(es_timer) et_link;
  int64_t et_expire;
  int et_interval;  // in ms
} es_timer_t;

static int thread_running;
static struct es_timer_list timers;
static hts_mutex_t timer_mutex;
static hts_cond_t timer_cond;



INITIALIZER(es_timer_init)
{
  hts_mutex_init(&timer_mutex);
  hts_cond_init(&timer_cond, &timer_mutex);
}

/**
 *
 */
static void
es_timer_destroy(es_resource_t *eres)
{
  es_timer_t *et = (es_timer_t *)eres;

  es_root_unregister(eres->er_ctx->ec_duk, eres);

  if(et->et_expire) {
    hts_mutex_lock(&timer_mutex);
    LIST_REMOVE(et, et_link);
    hts_mutex_unlock(&timer_mutex);
  }

  es_resource_unlink(&et->super);
}


/**
 *
 */
static void
es_timer_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_timer_t *et = (es_timer_t *)eres;
  int64_t delta = et->et_expire - arch_get_ts();
  snprintf(dst, dstsize, "in %d ms repeat %d ms", (int)(delta / 1000),
           et->et_interval);
}


/**
 *
 */
static const es_resource_class_t es_resource_timer = {
  .erc_name = "timer",
  .erc_size = sizeof(es_timer_t),
  .erc_destroy = es_timer_destroy,
  .erc_info = es_timer_info,
};


/**
 *
 */
static int
estimercmp(const es_timer_t *a, const es_timer_t *b)
{
  if(a->et_expire < b->et_expire)
    return -1;
  else if(a->et_expire > b->et_expire)
    return 1;
 return 0;
}


/**
 *
 */
static void *
timer_thread(void *aux)
{
  int destroy = 0;
  es_timer_t *et;
  hts_mutex_lock(&timer_mutex);
  while(1) {

    et = LIST_FIRST(&timers);
    if(et == NULL)
      break;

    int64_t now = arch_get_ts();
    int64_t delta = et->et_expire - now;
    if(delta > 0) {
      int ms = (delta + 999) / 1000;
      hts_cond_wait_timeout(&timer_cond, &timer_mutex, ms);
      continue;
    }

    LIST_REMOVE(et, et_link);
    if(et->et_interval) {
      et->et_expire = now + et->et_interval * 1000LL;
      LIST_INSERT_SORTED(&timers, et, et_link, estimercmp, es_timer_t);
      destroy = 0;
    } else {
      et->et_expire = 0;
      destroy = 1;
    }

    es_resource_retain(&et->super);
    hts_mutex_unlock(&timer_mutex);

    es_context_t *ec = et->super.er_ctx;

    duk_context *ctx = es_context_begin(ec);

    es_push_root(ctx, et);
    int rc = duk_pcall(ctx, 0);
    if(rc)
      es_dump_err(ctx);

    duk_pop(ctx);

    if(destroy)
      es_resource_destroy(&et->super);

    es_context_end(ec, 0, ctx);

    hts_mutex_lock(&timer_mutex);
    es_resource_release(&et->super);
  }
  thread_running = 0;
  hts_mutex_unlock(&timer_mutex);
  return NULL;
}


/**
 *
 */
static int
set_timer(duk_context *duk, int repeat)
{
  es_context_t *ec = es_get(duk);

  es_timer_t *et = es_resource_create(ec, &es_resource_timer, 1);
  int val = duk_require_int(duk, 1);

  es_root_register(duk, 0, et);

  et->et_interval = val * repeat;

  int64_t now = arch_get_ts();
  et->et_expire = now + val * 1000LL;

  hts_mutex_lock(&timer_mutex);

  if(thread_running == 0) {
    thread_running = 1;
    hts_thread_create_detached("estimer", timer_thread, NULL,
                               THREAD_PRIO_MODEL);
  } else {
    hts_cond_signal(&timer_cond);
  }

  LIST_INSERT_SORTED(&timers, et, et_link, estimercmp, es_timer_t);

  hts_mutex_unlock(&timer_mutex);

  es_resource_push(duk, &et->super);
  return 1;
}


/**
 *
 */
static int
set_timeout(duk_context *duk)
{
  return set_timer(duk, 0);
}


/**
 *
 */
static int
set_interval(duk_context *duk)
{
  return set_timer(duk, 1);
}


/**
 *
 */
static int
clear_timer(duk_context *duk)
{
  es_timer_t *et = es_resource_get(duk, 0, &es_resource_timer);
  es_resource_destroy(&et->super);
  return 0;
}


/**
 *
 */
const duk_function_list_entry es_fnlist_timer[] = {
  { "setTimeout",              set_timeout,              2 },
  { "setInterval",             set_interval,             2 },
  { "clearTimeout",            clear_timer,              1 },
  { "clearInterval",           clear_timer,              1 },
  { NULL, NULL, 0}
};
