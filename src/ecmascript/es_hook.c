#include <assert.h>

#include "ecmascript.h"
#include "service.h"
#include "arch/threads.h"
#include "misc/regex.h"
#include "backend/backend.h"

LIST_HEAD(es_hook_list, es_hook);

typedef struct es_hook {
  es_resource_t super;
  LIST_ENTRY(es_hook) eh_link;
  enum {
    HOOK_SUBTITLE_PROVIDER,
  } eh_type;
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

  hts_mutex_lock(&hook_mutex);
  LIST_REMOVE(eh, eh_link);
  num_hooks--;
  hts_mutex_unlock(&hook_mutex);

  es_resource_unlink(&eh->super);
}


/**
 *
 */
static const es_resource_class_t es_resource_hook = {
  .erc_name = "hook",
  .erc_size = sizeof(es_hook_t),
  .erc_destroy = es_hook_destroy,
};


/**
 *
 */
static int
es_hook_create(int type)
{
  es_context_t *ec = es_get(ctx);
  hts_mutex_lock(&hook_mutex);
  es_hook_t *eh = es_resource_alloc(&es_resource_hook);
  eh->eh_type = type;
  LIST_INSERT_HEAD(&hooks, eh, eh_link);
  num_hooks++;
  es_resource_init(&eh->super, ec);
  es_resource_retain(&eh->super);
  hts_mutex_unlock(&hook_mutex);
  duk_push_pointer(ctx, eh);
  return 1;
}


/**
 *
 */
static int
es_hook_invoke(int type, int (*push_args)(duk_context *duk, void *opaque),
               void *opaque)
{
  es_hook_t *eh, **v = alloca(num_hooks * sizeof(es_hook_t *));
  int cnt = 0;

  hts_mutex_lock(&hook_mutex);

  LIST_FOREACH(eh, &hooks, eh_link) {
    if(eh->eh_type == type) {
      v[cnt++] = eh;
      es_resource_retain(&eh->super);
    }
  }

  hts_mutex_unlock(&hook_mutex);

  for(int i = 0; i < cnt; i++) {
    eh = v[i];
    es_context_t *ec = eh->super.er_ctx;
    es_context_begin(ec);

    duk_context *ctx = ec->ec_duk;

    duk_push_global_object(ctx);
    duk_get_prop_string(ctx, -1, "hookInvoke");

    if(duk_is_function(ctx, -1)) {
      duk_push_pointer(ctx, eh);
      int r = push_args(ctx, opaque);
      int rc = duk_pcall(ctx, r);
      if(rc)
        es_dump_err(ctx);

      duk_pop(ctx);

    } else {
      duk_pop_2(ctx);
    }

    es_resource_release(&eh->super);

    es_context_end(ec);
  }
  return 0;
}


/**
 *
 */
static int
es_subtitleProviderCreate(duk_context *ctx)
{
  const char *id    = duk_safe_to_string(ctx, 0);
  const char *title = duk_safe_to_string(ctx, 1);

  subtitle_provider_t *sp = calloc(1, sizeof(subtitle_provider_t));
  subtitle_provider_register(sp, id, title, 0, "plugin", 1, 1);

  



}

/**
 * mutex object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_page[] = {
  { "subtitleProviderCreate",         es_subtitleProviderCreate,      1 },
  { NULL, NULL, 0}
};
