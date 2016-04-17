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
#include "plugins.h"
#include "task.h"

typedef struct es_service {
  es_resource_t super;

  service_t *s;
  char *title;
  char *url;
  int enabled;
  prop_sub_t *delete_sub;
} es_service_t;


/**
 *
 */
static void
es_service_destroy(es_resource_t *er)
{
  es_service_t *es = (es_service_t *)er;
  if(es->s == NULL)
    return;

  prop_unsubscribe(es->delete_sub);
  service_destroy(es->s);
  es->s = NULL;
  free(es->title);
  free(es->url);
  es_resource_unlink(er);
}

/**
 *
 */
static void
es_service_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_service_t *es = (es_service_t *)eres;

  snprintf(dst, dstsize, "'%s' => '%s' (enabled:%s)",
           es->title, es->url, es->enabled ? "Yes" : "No");
}


/**
 *
 */
static const es_resource_class_t es_resource_service = {
  .erc_name = "service",
  .erc_size = sizeof(es_service_t),
  .erc_destroy = es_service_destroy,
  .erc_info = es_service_info,
};




static void
uninstall_plugin(void *aux)
{
#if ENABLE_PLUGINS
  plugin_uninstall(rstr_get(aux));
#endif
  rstr_release(aux);
}

/**
 *
 */
static void
es_service_delete_req(void *opaque, prop_event_t event)
{
  es_service_t *es = opaque;

  if(event == PROP_REQ_DELETE)
    task_run(uninstall_plugin, rstr_dup(es->super.er_ctx->ec_id));
}


/**
 *
 */
static int
es_service_create(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  es_service_t *es = es_resource_create(ec, &es_resource_service, 1);

  const char *svcid  = duk_safe_to_string(ctx, 0);
  const char *title  = duk_safe_to_string(ctx, 1);
  const char *url    = duk_safe_to_string(ctx, 2);
  const char *type   = duk_safe_to_string(ctx, 3);
  int enabled        = duk_to_boolean    (ctx, 4);
  const char *icon   = duk_is_string(ctx, 5) ? duk_to_string(ctx, 5) : NULL;

  es->title = strdup(title);
  es->url   = strdup(url);

  es->s = service_create(svcid,
                         title, url, type, icon, 0, enabled,
                         SVC_ORIGIN_APP);
  es->enabled = enabled;

  if(ec->ec_flags & ECMASCRIPT_PLUGIN) {
    prop_set(es->s->s_root, "deleteText", PROP_SET_LINK, _p("Uninstall"));

    es->delete_sub =
      prop_subscribe(0,
                     PROP_TAG_ROOT, es->s->s_root,
                     PROP_TAG_LOCKMGR, ecmascript_context_lockmgr,
                     PROP_TAG_MUTEX, ec,
                     PROP_TAG_CALLBACK, es_service_delete_req, es,
                     NULL);
  }

  es_resource_push(ctx, &es->super);
  return 1;
}


/**
 *
 */
static int
es_service_enable(duk_context *ctx)
{
  es_service_t *es = es_resource_get(ctx, 0, &es_resource_service);

  if(es->s == NULL)
    return 0;

  if(duk_is_boolean(ctx, 1)) {
    es->enabled = duk_require_boolean(ctx, 1);
    service_set_enabled(es->s, es->enabled);
    return 0;
  }

  duk_push_boolean(ctx, es->enabled);
  return 1;
}



static const duk_function_list_entry fnlist_service[] = {
  { "create",           es_service_create,      6 },
  { "enable",           es_service_enable,      2 },
  { NULL, NULL, 0}
};

ES_MODULE("service", fnlist_service);
