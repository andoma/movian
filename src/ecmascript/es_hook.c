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

#include "ecmascript.h"
#include "service.h"
#include "arch/threads.h"
#include "backend/backend.h"

LIST_HEAD(es_hook_list, es_hook);

typedef struct es_hook {
  es_resource_t super;
  LIST_ENTRY(es_hook) eh_link;
  char *eh_type;
} es_hook_t;

static int num_hooks;
static struct es_hook_list hooks;
static HTS_MUTEX_DECL(hook_mutex);


/**
 *
 */
static void
es_hook_destroy(es_resource_t *eres)
{
  es_hook_t *eh = (es_hook_t *)eres;

  es_root_unregister(eres->er_ctx->ec_duk, eres);

  hts_mutex_lock(&hook_mutex);
  LIST_REMOVE(eh, eh_link);
  num_hooks--;
  hts_mutex_unlock(&hook_mutex);
  free(eh->eh_type);

  es_resource_unlink(&eh->super);
}


/**
 *
 */
static void
es_hook_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_hook_t *eh = (es_hook_t *)eres;

  snprintf(dst, dstsize, "%s", eh->eh_type);
}


/**
 *
 */
static const es_resource_class_t es_resource_hook = {
  .erc_name = "hook",
  .erc_size = sizeof(es_hook_t),
  .erc_destroy = es_hook_destroy,
  .erc_info = es_hook_info,
};


/**
 *
 */
int
es_hook_invoke(const char *type,
               int (*push_args)(duk_context *duk, void *opaque),
               void *opaque)
{
  es_hook_t *eh, **v = alloca(num_hooks * sizeof(es_hook_t *));
  int cnt = 0;

  // First create an array with all matching hooks

  hts_mutex_lock(&hook_mutex);

  LIST_FOREACH(eh, &hooks, eh_link) {
    if(!strcmp(eh->eh_type, type)) {
      v[cnt++] = eh;
      es_resource_retain(&eh->super);
    }
  }

  hts_mutex_unlock(&hook_mutex);

  for(int i = 0; i < cnt; i++) {
    eh = v[i];
    es_context_t *ec = eh->super.er_ctx;
    duk_context *ctx = es_context_begin(ec);

    es_push_root(ctx, eh);
    int r = push_args(ctx, opaque);
    int rc = duk_pcall(ctx, r);
    if(rc)
      es_dump_err(ctx);

    duk_pop(ctx);

    es_context_end(ec, 1, ctx);
    es_resource_release(&eh->super);
  }
  return 0;
}


/**
 *
 */
static int
es_hook_register(duk_context *ctx)
{
  const char *type  = duk_safe_to_string(ctx, 0);
  es_context_t *ec = es_get(ctx);

  es_hook_t *eh = es_resource_create(ec, &es_resource_hook, 1);
  eh->eh_type = strdup(type);

  hts_mutex_lock(&hook_mutex);
  LIST_INSERT_HEAD(&hooks, eh, eh_link);
  num_hooks++;
  hts_mutex_unlock(&hook_mutex);

  es_root_register(ctx, 1, eh);

  es_resource_push(ctx, &eh->super);
  return 1;
}


static const duk_function_list_entry fnlist_hook[] = {
  { "register",         es_hook_register,      2 },
  { NULL, NULL, 0}
};

ES_MODULE("hook", fnlist_hook);
