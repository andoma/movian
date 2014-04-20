#include <assert.h>

#include "ecmascript.h"
#include "service.h"


typedef struct es_service {
  es_resource_t super;

  service_t *s;
  int enabled;
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

  service_destroy(es->s);
  es->s = NULL;
  es_resource_unlink(er);
}


/**
 *
 */
static const es_resource_class_t es_resource_service = {
  .erc_name = "service",
  .erc_size = sizeof(es_service_t),
  .erc_destroy = es_service_destroy,
};




/**
 *
 */
static int
es_service_create(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  es_service_t *es = es_resource_create(ec, &es_resource_service);

  const char *svcid  = duk_safe_to_string(ctx, 0);
  const char *title  = duk_safe_to_string(ctx, 1);
  const char *url    = duk_safe_to_string(ctx, 2);
  const char *type   = duk_safe_to_string(ctx, 3);
  int enabled        = duk_to_boolean    (ctx, 4);
  const char *icon   = duk_is_string(ctx, 5) ? duk_to_string(ctx, 5) : NULL;
  es->s = service_create(svcid,
                         title, url, type, icon, 0, enabled,
                         SVC_ORIGIN_APP);
  es->enabled = enabled;
  es_resource_retain(&es->super);
  duk_push_pointer(ctx, es);
  return 1;
}


/**
 *
 */
static int
es_service_enable(duk_context *ctx)
{
  es_service_t *es = duk_require_pointer(ctx, 0);
  assert(es->super.er_class == &es_resource_service);

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



const duk_function_list_entry fnlist_Showtime_service[] = {

  { "serviceCreate",           es_service_create,      6 },
  { "serviceEnable",           es_service_enable,      2 },
  { NULL, NULL, 0}
};
